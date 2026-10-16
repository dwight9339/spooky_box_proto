#ifndef SPOOKY_SESSION_EVENT_LOG_H
#define SPOOKY_SESSION_EVENT_LOG_H

/* The session event log's records, bounded queue and sector assembly
 * (decision 0030 items 7-17; byte layouts in docs/design/session-format.md).
 * Portable: no FatFs or HAL, foreground only, so the host tests cover it. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SESSION_EVENT_RECORD_BYTES   32U
#define SESSION_EVENT_PAYLOAD_BYTES  12U
#define SESSION_EVENT_QUEUE_RECORDS  64U  /* decision 0030 item 14 */
#define SESSION_EVENT_SECTOR_RECORDS 16U  /* one 512-byte sector */
#define SESSION_EVENT_SECTOR_BYTES \
  (SESSION_EVENT_SECTOR_RECORDS * SESSION_EVENT_RECORD_BYTES)

#define SESSION_EVENT_SESSION_START 0x0001U
#define SESSION_EVENT_SESSION_END   0x0002U
#define SESSION_EVENT_TUNE_START    0x0010U
#define SESSION_EVENT_TUNE_END      0x0011U
#define SESSION_EVENT_GAP_START     0x0020U
#define SESSION_EVENT_GAP_END       0x0021U
#define SESSION_EVENT_PTT_ON        0x0030U
#define SESSION_EVENT_PTT_OFF       0x0031U
#define SESSION_EVENT_CLASSIC       0x0040U
#define SESSION_EVENT_EVENTS_LOST   0x00F0U

#define SESSION_EVENT_FLAG_CLIPPED        0x0001U /* uncertainty clipped at frame 0 */
#define SESSION_EVENT_FLAG_BEFORE_ORIGIN  0x0002U /* began before the origin */

#define SESSION_EVENT_TUNE_TUNED     1U
#define SESSION_EVENT_TUNE_FAILED    2U
#define SESSION_EVENT_TUNE_ABANDONED 3U
#define SESSION_EVENT_CLASSIC_SNAPSHOT 255U

typedef struct
{
  uint64_t frame;
  uint32_t uncertainty;
  uint16_t kind;
  uint16_t flags;
  uint8_t payload[SESSION_EVENT_PAYLOAD_BYTES];
} SessionEvent;

typedef struct
{
  SessionEvent records[SESSION_EVENT_QUEUE_RECORDS];
  uint32_t sequences[SESSION_EVENT_QUEUE_RECORDS];
  uint32_t posted_ms[SESSION_EVENT_QUEUE_RECORDS];
  uint32_t head;
  uint32_t count;
  uint32_t next_sequence;  /* counts every record produced, dropped ones too */
  uint32_t pending_loss;   /* dropped since the last events-lost record */
  uint32_t lost;
  uint32_t written;
  uint32_t high_water;
  bool resync_requested;
} SessionEventLog;

/* The 32-byte file header: magic, version 1.0, sizes and sample rate. */
void SessionEventLog_EncodeHeader(uint8_t out[SESSION_EVENT_RECORD_BYTES]);
void SessionEvent_Encode(const SessionEvent *event, uint32_t sequence,
                         uint8_t out[SESSION_EVENT_RECORD_BYTES]);

/* Converts a stamp on the radio timeline to the session timeline whose frame 0
 * is origin_frame. False for a stamp before the origin, which the log does not
 * hold (decision 0030 item 10). An uncertainty reaching before frame 0 is
 * clipped there and flagged. */
bool SessionEvent_At(uint64_t origin_frame, uint64_t stamp_frame,
                     uint32_t uncertainty, SessionEvent *event);

void SessionEventLog_Reset(SessionEventLog *log);
/* Queues a record and assigns its sequence. A full queue drops the record and
 * counts it. The first record accepted after a loss is preceded by an
 * events-lost record and raises a resynchronization request. False when the
 * record was dropped. */
bool SessionEventLog_Post(SessionEventLog *log, const SessionEvent *event, uint32_t now_ms);
/* True once per loss: the caller posts the state snapshot (item 13). */
bool SessionEventLog_TakeResync(SessionEventLog *log);
/* Encodes the oldest waiting records into out when a whole sector is waiting,
 * when the oldest has waited flush_ms, or when flush_all is set. Returns the
 * number of records encoded, 0 when nothing is due. */
uint32_t SessionEventLog_Prepare(const SessionEventLog *log, uint32_t now_ms,
                                 uint32_t flush_ms, bool flush_all,
                                 uint8_t out[SESSION_EVENT_SECTOR_BYTES]);
/* The prepared records reached the card. */
void SessionEventLog_Commit(SessionEventLog *log, uint32_t records);
/* The card refused them: every waiting record is counted lost. */
void SessionEventLog_DropWaiting(SessionEventLog *log);

/* Payload builders (docs/design/session-format.md). */
void SessionEvent_SetBandFrequency(SessionEvent *event, uint8_t band, uint32_t frequency_khz);
void SessionEvent_SetTuneEnd(SessionEvent *event, uint8_t band, uint8_t outcome,
                             uint8_t rssi_dbuv, uint8_t snr_db, uint32_t target_khz,
                             uint32_t frequency_khz);
void SessionEvent_SetClassic(SessionEvent *event, uint8_t published, uint8_t run_state,
                             uint8_t unable_reason, uint8_t edge, bool direction_up,
                             bool rate_limited, uint16_t hold_seconds,
                             uint16_t rate_setting_per_min, uint16_t rate_per_min,
                             uint16_t distance_channels);
void SessionEvent_SetCount(SessionEvent *event, uint32_t count);

#endif /* SPOOKY_SESSION_EVENT_LOG_H */
