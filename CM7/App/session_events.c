#include "session_events.h"

#include <string.h>

#include "audio_path_service.h"
#include "classic_adapter.h"
#include "main.h"
#include "radio_adapter.h"
#include "radio_control_service.h"

static SessionEventLog session_log;
static bool session_active;
static AudioTimelinePosition session_origin;

static SessionEvent Blank(uint16_t kind)
{
  SessionEvent event;

  (void)memset(&event, 0, sizeof(event));
  event.kind = kind;
  return event;
}

/* A stamp on the session timeline; false for one before the origin or from
 * another epoch (decision 0012 item 7: a capture never spans an epoch). */
static bool Place(const AudioTimelinePosition *stamp, uint32_t uncertainty,
                  SessionEvent *event)
{
  return (stamp != NULL) && (stamp->epoch == session_origin.epoch) &&
         SessionEvent_At(session_origin.frame, stamp->frame, uncertainty, event);
}

static void PostClassicSnapshot(uint64_t frame, uint32_t uncertainty, uint32_t now_ms);

/* Posts one record; after a loss, the state snapshot follows (item 13). */
static void Post(const SessionEvent *event, uint32_t now_ms)
{
  (void)SessionEventLog_Post(&session_log, event, now_ms);
  if (SessionEventLog_TakeResync(&session_log))
  {
    PostClassicSnapshot(event->frame, event->uncertainty, now_ms);
  }
}

static void ClassicPayload(SessionEvent *event, uint8_t published, const ClassicState *state)
{
  SessionEvent_SetClassic(event, published, state->run_state, state->unable_reason,
                          state->edge, state->direction_up, state->rate_limited,
                          state->hold_seconds, state->rate_setting_per_min,
                          state->rate_per_min, state->distance_channels);
}

/* The state a reader needs from this frame on: Classic, PTT if held, and a
 * tune in flight (items 11 and 13). At frame 0 the tune began before the
 * origin and is flagged. */
static void PostState(uint64_t frame, uint32_t uncertainty, uint16_t flags, uint32_t now_ms)
{
  ClassicState classic;
  AudioPathPttStatus ptt;
  SessionEvent event;
  uint8_t band;
  uint32_t target_khz;

  if (ClassicAdapter_GetState(&classic))
  {
    event = Blank(SESSION_EVENT_CLASSIC);
    event.frame = frame;
    event.uncertainty = uncertainty;
    ClassicPayload(&event, SESSION_EVENT_CLASSIC_SNAPSHOT, &classic);
    (void)SessionEventLog_Post(&session_log, &event, now_ms);
  }
  if (AudioPath_GetPttStatus(&ptt) && ptt.on)
  {
    event = Blank(SESSION_EVENT_PTT_ON);
    event.frame = frame;
    event.uncertainty = uncertainty;
    event.flags = flags;
    (void)SessionEventLog_Post(&session_log, &event, now_ms);
  }
  if (RadioAdapter_TuneInFlight(&band, &target_khz))
  {
    event = Blank(SESSION_EVENT_TUNE_START);
    event.frame = frame;
    event.uncertainty = uncertainty;
    event.flags = flags;
    SessionEvent_SetBandFrequency(&event, band, target_khz);
    (void)SessionEventLog_Post(&session_log, &event, now_ms);
  }
}

static void PostClassicSnapshot(uint64_t frame, uint32_t uncertainty, uint32_t now_ms)
{
  PostState(frame, uncertainty, 0U, now_ms);
}

void SessionEvents_Begin(const AudioTimelinePosition *origin, uint32_t now_ms)
{
  RadioControlStatus radio;
  SessionEvent event;

  SessionEventLog_Reset(&session_log);
  session_active = false;
  if (origin == NULL)
  {
    return;
  }
  session_origin = *origin;
  session_active = true;
  (void)RadioControl_GetStatus(&radio);
  event = Blank(SESSION_EVENT_SESSION_START);
  SessionEvent_SetBandFrequency(&event, (uint8_t)radio.tune.band, radio.tune.frequency_khz);
  (void)SessionEventLog_Post(&session_log, &event, now_ms);
  PostState(0U, 0U, SESSION_EVENT_FLAG_BEFORE_ORIGIN, now_ms);
}

void SessionEvents_End(uint64_t last_frame, uint8_t end_reason, uint32_t now_ms)
{
  SessionEvent event;

  if (!session_active)
  {
    return;
  }
  event = Blank(SESSION_EVENT_SESSION_END);
  event.frame = last_frame;
  event.payload[0] = end_reason;
  Post(&event, now_ms);
  session_active = false;
}

bool SessionEvents_Active(void)
{
  return session_active;
}

void SessionEvents_TuneStart(uint8_t band, uint32_t target_khz,
                             const AudioTimelinePosition *stamp)
{
  /* Taken before the tune is issued: the receiver cannot change before it. */
  SessionEvent event = Blank(SESSION_EVENT_TUNE_START);

  if (session_active && Place(stamp, 0U, &event))
  {
    SessionEvent_SetBandFrequency(&event, band, target_khz);
    Post(&event, HAL_GetTick());
  }
}

void SessionEvents_TuneEnd(uint8_t band, uint8_t outcome, uint8_t rssi_dbuv,
                           uint8_t snr_db, uint32_t target_khz, uint32_t frequency_khz,
                           const AudioTimelinePosition *stamp)
{
  SessionEvent event = Blank(SESSION_EVENT_TUNE_END);

  if (session_active && Place(stamp, SESSION_EVENTS_OBSERVED_FRAMES, &event))
  {
    SessionEvent_SetTuneEnd(&event, band, outcome, rssi_dbuv, snr_db, target_khz,
                            frequency_khz);
    Post(&event, HAL_GetTick());
  }
}

void SessionEvents_Ptt(bool on, const AudioTimelinePosition *stamp, uint32_t uncertainty)
{
  SessionEvent event = Blank(on ? SESSION_EVENT_PTT_ON : SESSION_EVENT_PTT_OFF);

  if (session_active && Place(stamp, uncertainty, &event))
  {
    Post(&event, HAL_GetTick());
  }
}

void SessionEvents_Classic(uint8_t published, const ClassicState *state,
                           const AudioTimelinePosition *stamp)
{
  SessionEvent event = Blank(SESSION_EVENT_CLASSIC);

  if (session_active && (state != NULL) &&
      Place(stamp, SESSION_EVENTS_OBSERVED_FRAMES, &event))
  {
    ClassicPayload(&event, published, state);
    Post(&event, HAL_GetTick());
  }
}

uint32_t SessionEvents_Prepare(uint32_t now_ms, bool flush_all,
                               uint8_t out[SESSION_EVENT_SECTOR_BYTES])
{
#ifndef SPOOKY_SESSION_EVENT_FLUSH_MS
#define SPOOKY_SESSION_EVENT_FLUSH_MS 2000U /* decision 0030 item 14 */
#endif
  return SessionEventLog_Prepare(&session_log, now_ms, SPOOKY_SESSION_EVENT_FLUSH_MS,
                                 flush_all, out);
}

void SessionEvents_Commit(uint32_t records)
{
  SessionEventLog_Commit(&session_log, records);
}

void SessionEvents_WriteFailed(void)
{
  SessionEventLog_DropWaiting(&session_log);
}

void SessionEvents_GetCounts(uint32_t *written, uint32_t *lost, uint32_t *high_water)
{
  if (written != NULL) *written = session_log.written;
  if (lost != NULL) *lost = session_log.lost;
  if (high_water != NULL) *high_water = session_log.high_water;
}

uint8_t SessionEvents_EndReasonCode(const char *reason)
{
  static const char *const reasons[] =
  {
    "stopped", "duration complete", "card full", "SD card removed",
    "three-channel file write failed", "radio queue overrun", "PDM queue overrun",
    "radio DMA error", "PDM DMA error", "radio timeline unavailable",
    "PDM DMA did not start"
  };

  for (uint32_t index = 0U; (reason != NULL) &&
       (index < (sizeof(reasons) / sizeof(reasons[0]))); ++index)
  {
    if (strcmp(reason, reasons[index]) == 0)
    {
      return (uint8_t)(index + 1U);
    }
  }
  return 255U; /* other: the manifest's end_reason names it */
}
