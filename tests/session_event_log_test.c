/*
 * Host tests for the session event log (decision 0030, full_spooky_proto-hpq.11):
 * record encoding against docs/design/session-format.md, the frame conversion,
 * the bounded queue with loss and resynchronization, and sector assembly.
 * Host results only; not hardware evidence.
 */

#include <stdio.h>
#include <string.h>

#include "session_event_log.h"

static int failures;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            printf("FAIL line %d: %s\n", __LINE__, #cond);                       \
            ++failures;                                                          \
        }                                                                        \
    } while (0)

static SessionEventLog log_under_test;
static uint8_t sector[SESSION_EVENT_SECTOR_BYTES];

static SessionEvent Event(uint16_t kind, uint64_t frame)
{
  SessionEvent event;

  memset(&event, 0, sizeof(event));
  event.kind = kind;
  event.frame = frame;
  return event;
}

static uint32_t Le32(const uint8_t *in)
{
  return (uint32_t)in[0] | ((uint32_t)in[1] << 8) | ((uint32_t)in[2] << 16) |
         ((uint32_t)in[3] << 24);
}

/* The worked example in docs/design/session-format.md. */
static void TestEncodingMatchesTheDesignExample(void)
{
  static const uint8_t header[32] = {
    0x53, 0x50, 0x4B, 0x59, 0x45, 0x56, 0x54, 0x53, 0x01, 0x00, 0x00, 0x00,
    0x20, 0x00, 0x20, 0x00, 0x80, 0xBB, 0x00, 0x00};
  static const uint8_t record[32] = {
    0x09, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x0E, 0x00, 0x00,
    0x11, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x01, 0x17, 0x02,
    0x80, 0x83, 0x01, 0x00, 0x80, 0x83, 0x01, 0x00};
  uint8_t out[32];
  SessionEvent event = Event(SESSION_EVENT_TUNE_END, 1033U);

  SessionEventLog_EncodeHeader(out);
  CHECK(memcmp(out, header, sizeof(header)) == 0);
  event.uncertainty = 3600U;
  SessionEvent_SetTuneEnd(&event, 0U, SESSION_EVENT_TUNE_TUNED, 23U, 2U, 99200U, 99200U);
  SessionEvent_Encode(&event, 5U, out);
  CHECK(memcmp(out, record, sizeof(record)) == 0);
}

static void TestClassicPayloadOffsets(void)
{
  uint8_t out[32];
  SessionEvent event = Event(SESSION_EVENT_CLASSIC, 0U);

  SessionEvent_SetClassic(&event, SESSION_EVENT_CLASSIC_SNAPSHOT, 4U, 0U, 1U, true, true,
                          300U, 400U, 180U, 20U);
  SessionEvent_Encode(&event, 0U, out);
  CHECK(out[20] == 255U && out[21] == 4U && out[22] == 0U && out[23] == 1U);
  CHECK(out[24] == 3U);   /* direction up, rate limited */
  CHECK(out[25] == 255U); /* hold seconds saturate */
  CHECK(out[26] == 0x90U && out[27] == 0x01U); /* 400 */
  CHECK(out[28] == 180U && out[29] == 0U);
  CHECK(out[30] == 20U && out[31] == 0U);
}

static void TestGapPayload(void)
{
  uint8_t out[32];
  SessionEvent event = Event(SESSION_EVENT_GAP_END, 51200U);

  SessionEvent_SetGap(&event, SESSION_EVENT_GAP_BAND_SWITCH, 1U, 1000U);
  SessionEvent_Encode(&event, 3U, out);
  CHECK(out[12] == 0x21U && out[20] == 1U && out[21] == 1U);
  CHECK(Le32(&out[24]) == 1000U && Le32(&out[8]) == 0U);
}

static void TestStampConversion(void)
{
  SessionEvent event = Event(SESSION_EVENT_PTT_ON, 0U);

  CHECK(!SessionEvent_At(1000U, 999U, 0U, &event));
  CHECK(SessionEvent_At(1000U, 5000U, 3600U, &event));
  CHECK(event.frame == 4000U && event.uncertainty == 3600U && event.flags == 0U);
  CHECK(SessionEvent_At(1000U, 2000U, 3600U, &event));
  CHECK(event.frame == 1000U && event.uncertainty == 1000U);
  CHECK((event.flags & SESSION_EVENT_FLAG_CLIPPED) != 0U);
}

static void TestSectorAndSparseFlush(void)
{
  SessionEventLog *log = &log_under_test;
  SessionEvent event = Event(SESSION_EVENT_PTT_ON, 10U);

  SessionEventLog_Reset(log);
  CHECK(SessionEventLog_Prepare(log, 0U, 2000U, false, sector) == 0U);
  CHECK(SessionEventLog_Post(log, &event, 100U));
  /* Sparse: nothing until the oldest has waited 2 s, unless flushing. */
  CHECK(SessionEventLog_Prepare(log, 2099U, 2000U, false, sector) == 0U);
  CHECK(SessionEventLog_Prepare(log, 2100U, 2000U, false, sector) == 1U);
  CHECK(SessionEventLog_Prepare(log, 100U, 2000U, true, sector) == 1U);
  SessionEventLog_Commit(log, 1U);
  CHECK(log->count == 0U && log->written == 1U);

  for (uint32_t index = 0U; index < 20U; ++index)
  {
    event.frame = index;
    CHECK(SessionEventLog_Post(log, &event, 200U));
  }
  /* A whole sector is due at once, oldest first, sequences continuing. */
  CHECK(SessionEventLog_Prepare(log, 200U, 2000U, false, sector) == 16U);
  CHECK(Le32(&sector[16]) == 1U);
  CHECK(Le32(&sector[15U * 32U + 16U]) == 16U);
  SessionEventLog_Commit(log, 16U);
  CHECK(SessionEventLog_Prepare(log, 200U, 2000U, false, sector) == 0U);
  CHECK(SessionEventLog_Prepare(log, 200U, 2000U, true, sector) == 4U);
  CHECK(sector[0] == 16U); /* frame of the 17th record */
}

static void TestLossInsertsARecordAndRequestsResync(void)
{
  SessionEventLog *log = &log_under_test;
  SessionEvent event = Event(SESSION_EVENT_CLASSIC, 0U);
  uint32_t prepared;

  SessionEventLog_Reset(log);
  for (uint32_t index = 0U; index < SESSION_EVENT_QUEUE_RECORDS; ++index)
  {
    CHECK(SessionEventLog_Post(log, &event, 0U));
  }
  CHECK(!SessionEventLog_Post(log, &event, 0U));
  CHECK(!SessionEventLog_Post(log, &event, 0U));
  CHECK(log->lost == 2U && log->pending_loss == 2U && log->high_water == 64U);
  CHECK(!SessionEventLog_TakeResync(log));

  /* One free slot is not enough for the lost record and the event. */
  SessionEventLog_Commit(log, 1U);
  event.frame = 777U;
  CHECK(!SessionEventLog_Post(log, &event, 0U));
  CHECK(log->lost == 3U);

  SessionEventLog_Commit(log, 15U);
  for (prepared = 0U; prepared < 3U; ++prepared)
  {
    SessionEventLog_Commit(log, 16U);
  }
  CHECK(log->count == 0U);
  CHECK(SessionEventLog_Post(log, &event, 0U));
  CHECK(log->count == 2U && log->pending_loss == 0U);
  CHECK(SessionEventLog_TakeResync(log));
  CHECK(!SessionEventLog_TakeResync(log));
  CHECK(SessionEventLog_Prepare(log, 0U, 2000U, true, sector) == 2U);
  /* events lost: kind 0x00F0, count 3, sequence 67 after 64 kept and 3 dropped */
  CHECK(sector[12] == 0xF0U && Le32(&sector[20]) == 3U && Le32(&sector[16]) == 67U);
  CHECK(sector[0] == 0x09U && sector[1] == 0x03U); /* frame 777 */
  CHECK(sector[32U + 12U] == 0x40U && Le32(&sector[32U + 16U]) == 68U);
}

static void TestDropWaitingCountsLoss(void)
{
  SessionEventLog *log = &log_under_test;
  SessionEvent event = Event(SESSION_EVENT_PTT_OFF, 1U);

  SessionEventLog_Reset(log);
  CHECK(SessionEventLog_Post(log, &event, 0U));
  CHECK(SessionEventLog_Post(log, &event, 0U));
  SessionEventLog_DropWaiting(log);
  CHECK(log->count == 0U && log->lost == 2U && log->written == 0U);
  CHECK(SessionEventLog_Prepare(log, 0U, 2000U, true, sector) == 0U);
  CHECK(!SessionEventLog_Post(NULL, &event, 0U));
  CHECK(!SessionEventLog_TakeResync(NULL));
}

int main(void)
{
  TestEncodingMatchesTheDesignExample();
  TestClassicPayloadOffsets();
  TestGapPayload();
  TestStampConversion();
  TestSectorAndSparseFlush();
  TestLossInsertsARecordAndRequestsResync();
  TestDropWaitingCountsLoss();
  if (failures != 0)
  {
    printf("%d failure(s)\n", failures);
    return 1;
  }
  printf("session_event_log_test: all checks passed\n");
  return 0;
}
