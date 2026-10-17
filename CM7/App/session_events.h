#ifndef SPOOKY_SESSION_EVENTS_H
#define SPOOKY_SESSION_EVENTS_H

/* The session event log's producers and writer interface on the M7 (decision
 * 0030 items 7-17). Producers post only while a session captures; their
 * stamps are radio-timeline positions converted to session frames. Foreground
 * only. The recorder owns EVENTS.BIN and writes the sectors this prepares. */

#include <stdbool.h>
#include <stdint.h>

#include "audio_timeline.h"
#include "classic_service.h"
#include "session_event_log.h"

/* Foreground observation bound, decision 0012 item 6. */
#define SESSION_EVENTS_OBSERVED_FRAMES 3600U

/* Capture started with this origin: posts session start and the frame-0
 * state (item 11), then accepts producer events. */
void SessionEvents_Begin(const AudioTimelinePosition *origin, uint32_t now_ms);
/* Posts session end at last_frame and stops accepting producer events. */
void SessionEvents_End(uint64_t last_frame, uint8_t end_reason, uint32_t now_ms);
bool SessionEvents_Active(void);

/* Producers. A stamp before the origin, or from another epoch, is not logged. */
void SessionEvents_TuneStart(uint8_t band, uint32_t target_khz,
                             const AudioTimelinePosition *stamp);
void SessionEvents_TuneEnd(uint8_t band, uint8_t outcome, uint8_t rssi_dbuv,
                           uint8_t snr_db, uint32_t target_khz, uint32_t frequency_khz,
                           const AudioTimelinePosition *stamp);
void SessionEvents_Ptt(bool on, const AudioTimelinePosition *stamp, uint32_t uncertainty);
void SessionEvents_Classic(uint8_t published, const ClassicState *state,
                           const AudioTimelinePosition *stamp);
/* Decision 0004 gap edges: exact frames (decision 0012 item 6). */
void SessionEvents_GapStart(uint8_t band_before, const AudioTimelinePosition *first);
void SessionEvents_GapEnd(uint8_t band_after, uint32_t frequency_khz,
                          const AudioTimelinePosition *last);

/* Writer: the records due now (see SessionEventLog_Prepare) below horizon,
 * the frames already in the audio file, at most one sector; then the outcome
 * of writing them. */
uint32_t SessionEvents_Prepare(uint32_t now_ms, bool flush_all, uint64_t horizon,
                               uint8_t out[SESSION_EVENT_SECTOR_BYTES]);
void SessionEvents_Commit(uint32_t records);
void SessionEvents_WriteFailed(void);
void SessionEvents_GetCounts(uint32_t *written, uint32_t *lost, uint32_t *high_water);

/* End reasons, docs/design/session-format.md. */
uint8_t SessionEvents_EndReasonCode(const char *reason);

#endif /* SPOOKY_SESSION_EVENTS_H */
