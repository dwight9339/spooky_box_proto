#include "session_event_log.h"

#include <string.h>

static void PutLe16(uint8_t *out, uint16_t value)
{
  out[0] = (uint8_t)value;
  out[1] = (uint8_t)(value >> 8);
}

static void PutLe32(uint8_t *out, uint32_t value)
{
  out[0] = (uint8_t)value;
  out[1] = (uint8_t)(value >> 8);
  out[2] = (uint8_t)(value >> 16);
  out[3] = (uint8_t)(value >> 24);
}

static void PutLe64(uint8_t *out, uint64_t value)
{
  PutLe32(out, (uint32_t)value);
  PutLe32(&out[4], (uint32_t)(value >> 32));
}

void SessionEventLog_EncodeHeader(uint8_t out[SESSION_EVENT_RECORD_BYTES])
{
  (void)memset(out, 0, SESSION_EVENT_RECORD_BYTES);
  (void)memcpy(out, "SPKYEVTS", 8U);
  PutLe16(&out[8], 1U);  /* major version */
  PutLe16(&out[10], 0U); /* minor version */
  PutLe16(&out[12], (uint16_t)SESSION_EVENT_RECORD_BYTES);
  PutLe16(&out[14], (uint16_t)SESSION_EVENT_RECORD_BYTES);
  PutLe32(&out[16], 48000U);
}

void SessionEvent_Encode(const SessionEvent *event, uint32_t sequence,
                         uint8_t out[SESSION_EVENT_RECORD_BYTES])
{
  PutLe64(&out[0], event->frame);
  PutLe32(&out[8], event->uncertainty);
  PutLe16(&out[12], event->kind);
  PutLe16(&out[14], event->flags);
  PutLe32(&out[16], sequence);
  (void)memcpy(&out[20], event->payload, SESSION_EVENT_PAYLOAD_BYTES);
}

bool SessionEvent_At(uint64_t origin_frame, uint64_t stamp_frame,
                     uint32_t uncertainty, SessionEvent *event)
{
  uint64_t frame;

  if ((event == NULL) || (stamp_frame < origin_frame))
  {
    return false;
  }
  frame = stamp_frame - origin_frame;
  event->frame = frame;
  event->uncertainty = uncertainty;
  if ((uint64_t)uncertainty > frame)
  {
    event->uncertainty = (uint32_t)frame;
    event->flags |= SESSION_EVENT_FLAG_CLIPPED;
  }
  return true;
}

void SessionEventLog_Reset(SessionEventLog *log)
{
  if (log != NULL)
  {
    (void)memset(log, 0, sizeof(*log));
  }
}

static void Push(SessionEventLog *log, const SessionEvent *event, uint32_t now_ms)
{
  const uint32_t tail = (log->head + log->count) % SESSION_EVENT_QUEUE_RECORDS;

  log->records[tail] = *event;
  log->sequences[tail] = log->next_sequence++;
  log->posted_ms[tail] = now_ms;
  ++log->count;
  if (log->count > log->high_water)
  {
    log->high_water = log->count;
  }
}

bool SessionEventLog_Post(SessionEventLog *log, const SessionEvent *event, uint32_t now_ms)
{
  const uint32_t needed = (log != NULL) && (log->pending_loss != 0U) ? 2U : 1U;

  if ((log == NULL) || (event == NULL))
  {
    return false;
  }
  if ((log->count + needed) > SESSION_EVENT_QUEUE_RECORDS)
  {
    ++log->next_sequence; /* the sequence gap shows the loss to a reader */
    ++log->pending_loss;
    ++log->lost;
    return false;
  }
  if (log->pending_loss != 0U)
  {
    SessionEvent lost;

    (void)memset(&lost, 0, sizeof(lost));
    lost.kind = SESSION_EVENT_EVENTS_LOST;
    lost.frame = event->frame; /* when writing resumes */
    SessionEvent_SetCount(&lost, log->pending_loss);
    Push(log, &lost, now_ms);
    log->pending_loss = 0U;
    log->resync_requested = true;
  }
  Push(log, event, now_ms);
  return true;
}

bool SessionEventLog_TakeResync(SessionEventLog *log)
{
  const bool requested = (log != NULL) && log->resync_requested;

  if (requested)
  {
    log->resync_requested = false;
  }
  return requested;
}

uint32_t SessionEventLog_Prepare(const SessionEventLog *log, uint32_t now_ms,
                                 uint32_t flush_ms, bool flush_all, uint64_t horizon,
                                 uint8_t out[SESSION_EVENT_SECTOR_BYTES])
{
  uint32_t records = 0U;

  if ((log == NULL) || (out == NULL) || (log->count == 0U))
  {
    return 0U;
  }
  while ((records < log->count) && (records < SESSION_EVENT_SECTOR_RECORDS) &&
         (log->records[(log->head + records) % SESSION_EVENT_QUEUE_RECORDS].frame <
          horizon))
  {
    ++records;
  }
  if ((records == 0U) ||
      ((records < SESSION_EVENT_SECTOR_RECORDS) && !flush_all &&
       ((uint32_t)(now_ms - log->posted_ms[log->head]) < flush_ms)))
  {
    return 0U;
  }
  for (uint32_t index = 0U; index < records; ++index)
  {
    const uint32_t slot = (log->head + index) % SESSION_EVENT_QUEUE_RECORDS;
    SessionEvent_Encode(&log->records[slot], log->sequences[slot],
                        &out[index * SESSION_EVENT_RECORD_BYTES]);
  }
  return records;
}

uint32_t SessionEventLog_DropAfter(SessionEventLog *log, uint64_t last_frame)
{
  uint32_t kept = 0U;
  uint32_t dropped = 0U;

  if (log == NULL)
  {
    return 0U;
  }
  for (uint32_t index = 0U; index < log->count; ++index)
  {
    const uint32_t from = (log->head + index) % SESSION_EVENT_QUEUE_RECORDS;
    const uint32_t to = (log->head + kept) % SESSION_EVENT_QUEUE_RECORDS;

    if (log->records[from].frame > last_frame)
    {
      ++dropped;
      continue;
    }
    log->records[to] = log->records[from];
    log->sequences[to] = log->sequences[from] - dropped;
    log->posted_ms[to] = log->posted_ms[from];
    ++kept;
  }
  log->count = kept;
  log->next_sequence -= dropped;
  return dropped;
}

void SessionEventLog_Commit(SessionEventLog *log, uint32_t records)
{
  if ((log == NULL) || (records > log->count))
  {
    return;
  }
  log->head = (log->head + records) % SESSION_EVENT_QUEUE_RECORDS;
  log->count -= records;
  log->written += records;
}

void SessionEventLog_DropWaiting(SessionEventLog *log)
{
  if (log == NULL)
  {
    return;
  }
  log->lost += log->count;
  log->head = 0U;
  log->count = 0U;
}

void SessionEvent_SetBandFrequency(SessionEvent *event, uint8_t band, uint32_t frequency_khz)
{
  event->payload[0] = band;
  PutLe32(&event->payload[4], frequency_khz);
}

void SessionEvent_SetTuneEnd(SessionEvent *event, uint8_t band, uint8_t outcome,
                             uint8_t rssi_dbuv, uint8_t snr_db, uint32_t target_khz,
                             uint32_t frequency_khz)
{
  event->payload[0] = band;
  event->payload[1] = outcome;
  event->payload[2] = rssi_dbuv;
  event->payload[3] = snr_db;
  PutLe32(&event->payload[4], target_khz);
  PutLe32(&event->payload[8], frequency_khz);
}

void SessionEvent_SetClassic(SessionEvent *event, uint8_t published, uint8_t run_state,
                             uint8_t unable_reason, uint8_t edge, bool direction_up,
                             bool rate_limited, uint16_t hold_seconds,
                             uint16_t rate_setting_per_min, uint16_t rate_per_min,
                             uint16_t distance_channels)
{
  event->payload[0] = published;
  event->payload[1] = run_state;
  event->payload[2] = unable_reason;
  event->payload[3] = edge;
  event->payload[4] = (uint8_t)((direction_up ? 1U : 0U) | (rate_limited ? 2U : 0U));
  event->payload[5] = (uint8_t)((hold_seconds > 255U) ? 255U : hold_seconds);
  PutLe16(&event->payload[6], rate_setting_per_min);
  PutLe16(&event->payload[8], rate_per_min);
  PutLe16(&event->payload[10], distance_channels);
}

void SessionEvent_SetCount(SessionEvent *event, uint32_t count)
{
  PutLe32(&event->payload[0], count);
}

void SessionEvent_SetGap(SessionEvent *event, uint8_t cause, uint8_t band,
                         uint32_t frequency_khz)
{
  event->payload[0] = cause;
  event->payload[1] = band;
  PutLe32(&event->payload[4], frequency_khz);
}
