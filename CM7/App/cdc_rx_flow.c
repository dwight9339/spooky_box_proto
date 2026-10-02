#include "cdc_rx_flow.h"

#include <stddef.h>

/* Producer and consumer run on the same core, so ordering the data accesses
 * against the index update only needs a compiler barrier. */
#if defined(__GNUC__)
#define CDC_RX_FLOW_BARRIER() __asm__ volatile("" ::: "memory")
#else
#define CDC_RX_FLOW_BARRIER() ((void)0)
#endif

void CdcRxFlow_Init(CdcRxFlow *flow, uint16_t packet_size)
{
  if (flow == NULL)
  {
    return;
  }
  flow->head = 0U;
  flow->tail = 0U;
  flow->paused = false;
  flow->packet_size = packet_size;
  flow->packets = 0U;
  flow->pauses = 0U;
  flow->overruns = 0U;
}

uint16_t CdcRxFlow_Free(const CdcRxFlow *flow)
{
  const uint16_t used = (uint16_t)((flow->head + CDC_RX_FLOW_CAPACITY - flow->tail) %
                                   CDC_RX_FLOW_CAPACITY);

  return (uint16_t)(CDC_RX_FLOW_CAPACITY - 1U - used);
}

bool CdcRxFlow_Push(CdcRxFlow *flow, const uint8_t *packet, uint32_t length)
{
  uint16_t head;

  if ((flow == NULL) || ((packet == NULL) && (length != 0U)))
  {
    return false;
  }
  ++flow->packets;
  if (length > CdcRxFlow_Free(flow))
  {
    ++flow->overruns;
    length = CdcRxFlow_Free(flow);
  }
  head = flow->head;
  for (uint32_t index = 0U; index < length; ++index)
  {
    flow->data[head] = packet[index];
    head = (uint16_t)((head + 1U) % CDC_RX_FLOW_CAPACITY);
  }
  CDC_RX_FLOW_BARRIER();
  flow->head = head;
  if (CdcRxFlow_Free(flow) >= flow->packet_size)
  {
    return true;
  }
  flow->paused = true;
  ++flow->pauses;
  return false;
}

bool CdcRxFlow_Pop(CdcRxFlow *flow, uint8_t *byte)
{
  const uint16_t tail = flow->tail;

  if ((byte == NULL) || (tail == flow->head))
  {
    return false;
  }
  CDC_RX_FLOW_BARRIER();
  *byte = flow->data[tail];
  CDC_RX_FLOW_BARRIER();
  flow->tail = (uint16_t)((tail + 1U) % CDC_RX_FLOW_CAPACITY);
  return true;
}

bool CdcRxFlow_Resume(CdcRxFlow *flow)
{
  if ((flow == NULL) || !flow->paused || (CdcRxFlow_Free(flow) < flow->packet_size))
  {
    return false;
  }
  flow->paused = false;
  return true;
}
