/*
 * Host tests for the USB CDC receive flow control (8lw.24): reception pauses
 * before a packet could be lost, resumes once a full packet fits again, keeps
 * every byte in order across a flood, and counts the overrun it should never
 * see. Host results only; not hardware evidence.
 */

#include <stdio.h>
#include <string.h>

#include "cdc_rx_flow.h"

static int failures;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            printf("FAIL line %d: %s\n", __LINE__, #cond);                       \
            ++failures;                                                          \
        }                                                                        \
    } while (0)

#define PACKET 64U

static void TestPausesBeforeAPacketWouldNotFit(void)
{
  CdcRxFlow flow;
  uint8_t packet[PACKET];
  uint8_t byte;

  memset(packet, 'x', sizeof(packet));
  CdcRxFlow_Init(&flow, PACKET);
  CHECK(CdcRxFlow_Free(&flow) == (CDC_RX_FLOW_CAPACITY - 1U));
  CHECK(CdcRxFlow_Push(&flow, packet, PACKET));  /* 191 free */
  CHECK(CdcRxFlow_Push(&flow, packet, PACKET));  /* 127 free */
  CHECK(!CdcRxFlow_Push(&flow, packet, PACKET)); /* 63 free: pause */
  CHECK(flow.paused && (flow.pauses == 1U) && (flow.overruns == 0U));
  CHECK(!CdcRxFlow_Resume(&flow)); /* 63 free: a packet would not fit yet. */
  /* One byte drained leaves 64 free, exactly one packet. */
  CHECK(CdcRxFlow_Pop(&flow, &byte) && (byte == 'x'));
  CHECK(CdcRxFlow_Free(&flow) == PACKET);
  CHECK(CdcRxFlow_Resume(&flow));
  CHECK(!flow.paused);
  CHECK(!CdcRxFlow_Resume(&flow)); /* Re-arm exactly once. */
}

static void TestNotPausedNoResume(void)
{
  CdcRxFlow flow;

  CdcRxFlow_Init(&flow, PACKET);
  CHECK(!CdcRxFlow_Resume(&flow));
}

/* A host streaming short command lines faster than they are handled: every byte
 * arrives, in order, as long as the endpoint is armed only when allowed. */
static void TestFloodLosesNothing(void)
{
  CdcRxFlow flow;
  uint8_t stream[8192];
  uint8_t received[8192];
  uint32_t sent = 0U;
  uint32_t got = 0U;
  int armed = 1;

  for (uint32_t i = 0U; i < sizeof(stream); ++i)
  {
    stream[i] = (uint8_t)((i % 13U == 12U) ? '\n' : ('a' + (i % 26U)));
  }
  CdcRxFlow_Init(&flow, PACKET);
  while (got < sizeof(stream))
  {
    /* The host sends a full packet whenever the endpoint is armed. */
    if (armed && (sent < sizeof(stream)))
    {
      uint32_t length = sizeof(stream) - sent;
      if (length > PACKET) length = PACKET;
      armed = CdcRxFlow_Push(&flow, &stream[sent], length) ? 1 : 0;
      sent += length;
    }
    /* The foreground drains a little each pass, then re-arms if it may. */
    for (int k = 0; k < 13; ++k)
    {
      uint8_t byte;
      if (!CdcRxFlow_Pop(&flow, &byte)) break;
      received[got++] = byte;
    }
    if (CdcRxFlow_Resume(&flow))
    {
      CHECK(!armed);
      armed = 1;
    }
  }
  CHECK(got == sizeof(stream));
  CHECK(memcmp(stream, received, sizeof(stream)) == 0);
  CHECK(flow.overruns == 0U);
  CHECK(flow.pauses > 0U);
}

static void TestOverrunIsTruncatedAndCounted(void)
{
  CdcRxFlow flow;
  uint8_t packet[PACKET];

  memset(packet, 'y', sizeof(packet));
  CdcRxFlow_Init(&flow, PACKET);
  (void)CdcRxFlow_Push(&flow, packet, PACKET);
  (void)CdcRxFlow_Push(&flow, packet, PACKET);
  (void)CdcRxFlow_Push(&flow, packet, PACKET);
  /* A misbehaving caller pushes while paused: only what fits is kept. */
  CHECK(!CdcRxFlow_Push(&flow, packet, PACKET));
  CHECK(flow.overruns == 1U);
  CHECK(CdcRxFlow_Free(&flow) == 0U);
}

int main(void)
{
  TestPausesBeforeAPacketWouldNotFit();
  TestNotPausedNoResume();
  TestFloodLosesNothing();
  TestOverrunIsTruncatedAndCounted();
  if (failures != 0)
  {
    printf("cdc_rx_flow_test: %d failure(s)\n", failures);
    return 1;
  }
  puts("cdc_rx_flow_test: all tests passed");
  return 0;
}
