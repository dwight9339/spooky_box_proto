#include "radio_adapter.h"

#include <stdio.h>

#include "app_events.h"
#include "audio_path_service.h"
#include "classic_adapter.h"
#include "codec_volume_service.h"
#include "diagnostics.h"
#if defined(SPOOKY_DEMO)
#include "demo_field.h"
#endif
#include "main.h"
#include "radio_activity_feed.h"
#include "radio_control_service.h"
#include "session_events.h"
#include "sm/radio_port.h"
#include "usb_test.h"

/* arg0 of APP_EVENT_RADIO_COMMAND: kind in bits 0-7, source in 8-15, up in bit 16,
 * wrap in bit 17. */
static uint32_t PackCommand(RadCommandKind kind, RadSource source, bool up, bool wrap)
{
  return (uint32_t)kind | ((uint32_t)source << 8) |
         (up ? (1UL << 16) : 0UL) | (wrap ? (1UL << 17) : 0UL);
}

/* CLI radio commands posted and not yet answered. The Radio machine answers every
 * command exactly once, so a scan engine can wait for them instead of issuing a
 * jump computed from a frequency the CLI is about to change. */
static uint32_t cli_commands_pending;

static bool PostCommand(RadCommandKind kind, bool up, bool wrap, uint32_t arg)
{
  if (!AppEvents_Post(EVQ_CLASS_EXTERNAL_COMMAND, APP_EVENT_RADIO_COMMAND,
                      PackCommand(kind, RAD_SOURCE_CLI, up, wrap), arg))
  {
    return false;
  }
  ++cli_commands_pending;
  return true;
}

#if defined(SPOOKY_DEMO)
/* Band changes from the band menu, posted and not yet answered. */
static uint32_t internal_band_pending;

bool RadioAdapter_RequestInternalBand(uint32_t band)
{
  if ((band >= (uint32_t)RADIO_BAND_COUNT) ||
      !AppEvents_Post(EVQ_CLASS_INTERNAL, APP_EVENT_RADIO_COMMAND,
                      PackCommand(RAD_CMD_BAND, RAD_SOURCE_INTERNAL, false, false), band))
  {
    return false;
  }
  ++internal_band_pending;
  return true;
}
#endif

bool RadioAdapter_CliCommandPending(void)
{
#if defined(SPOOKY_DEMO)
  return (cli_commands_pending != 0U) || (internal_band_pending != 0U);
#else
  return cli_commands_pending != 0U;
#endif
}

bool RadioAdapter_RequestInternalTune(uint32_t frequency_khz)
{
  return AppEvents_Post(EVQ_CLASS_INTERNAL, APP_EVENT_RADIO_COMMAND,
                        PackCommand(RAD_CMD_TUNE, RAD_SOURCE_INTERNAL, false, false),
                        frequency_khz);
}

bool RadioAdapter_RequestTune(uint32_t frequency_khz)
{
  return PostCommand(RAD_CMD_TUNE, false, false, frequency_khz);
}

bool RadioAdapter_RequestStep(bool up)
{
  /* CLI UP and DOWN stop at the band edge (RadioSm STEP with wrap 0). */
  return PostCommand(RAD_CMD_STEP, up, false, 0U);
}

bool RadioAdapter_RequestBand(uint32_t band)
{
  /* Callers parse the band first; an invalid one would fault the radio. */
  if (band >= (uint32_t)RADIO_BAND_COUNT)
  {
    return false;
  }
  return PostCommand(RAD_CMD_BAND, false, false, band);
}

static void FormatFrame(uint64_t frame, char text[21])
{
  char digits[21];
  uint32_t count = 0U;

  do
  {
    digits[count++] = (char)('0' + (uint32_t)(frame % 10U));
    frame /= 10U;
  } while ((frame != 0U) && (count < 20U));
  for (uint32_t index = 0U; index < count; ++index)
  {
    text[index] = digits[count - 1U - index];
  }
  text[count] = '\0';
}

/* Decision 0004 gaps: the band before the switch, and each edge reported once
 * to the session event log and the UART log. */
static RadioGapReporter gap_reporter;
static bool gap_reporter_ready;
static uint8_t gap_band_before;

static void ReportGapEdges(void)
{
  RadioGapSnapshot snapshot;
  RadioGapEdges edges;
  AudioTimelinePosition now;
  AudioTimelinePosition edge;
  RadioControlStatus status;
  char first_text[21];
  char last_text[21];

  if (!AudioPath_GetGap(&snapshot))
  {
    return;
  }
  if (!gap_reporter_ready)
  {
    RadioGap_ResetReporter(&gap_reporter, &snapshot);
    gap_reporter_ready = true;
    return;
  }
  edges = RadioGap_NewEdges(&gap_reporter, &snapshot);
  if ((!edges.start && !edges.end) || !AudioPath_GetPosition(&now))
  {
    return;
  }
  edge.epoch = now.epoch;
  if (edges.start)
  {
    edge.frame = edges.first_frame;
    SessionEvents_GapStart(gap_band_before, &edge);
  }
  if (edges.end)
  {
    (void)RadioControl_GetStatus(&status);
    edge.frame = edges.last_frame;
    SessionEvents_GapEnd((uint8_t)status.band, status.tune.frequency_khz, &edge);
    FormatFrame(edges.first_frame, first_text);
    FormatFrame(edges.last_frame, last_text);
    printf("[gap] start=%lu:%s end=%lu:%s band=%s->%s freq=%lu missed=%lu\r\n",
           (unsigned long)now.epoch, first_text, (unsigned long)now.epoch, last_text,
           RadioControl_GetBandInfo((RadioBand)gap_band_before)->name,
           RadioControl_GetBandInfo(status.band)->name,
           (unsigned long)status.tune.frequency_khz,
           (unsigned long)gap_reporter.missed);
  }
}

bool RadioAdapter_GapAtOrigin(const AudioTimelinePosition *origin, uint8_t *band_before)
{
  RadioGapSnapshot snapshot;
  AudioTimelinePosition now;

  if ((origin == NULL) || !AudioPath_GetGap(&snapshot) || !AudioPath_GetPosition(&now) ||
      (now.epoch != origin->epoch) || !RadioGap_SpansOrigin(&snapshot, origin->frame))
  {
    return false;
  }
  if (band_before != NULL)
  {
    *band_before = gap_band_before;
  }
  return true;
}

void RadioAdapter_ServiceActivity(void)
{
  const RadState state = Radio_GetState();
  uint32_t block = 0U;
  const bool known = AudioPath_GetBlockInProgress(&block);

  /* A band switch keeps the retune interval it opened until it completes. */
  RadioActivityFeed_Service(HAL_GetTick(),
                            (state == RAD_STATE_SETTLED) || (state == RAD_STATE_TUNING),
                            (state == RAD_STATE_TUNING) || (state == RAD_STATE_SWITCHING),
                            known, block);
  ReportGapEdges();
}

/* Decision 0015 items 2 and 3: the tune in flight, stamped on the radio sample
 * timeline when it is issued and again when its outcome is published. The Radio
 * machine has at most one tune in flight (item 8). Storing the pair with the
 * session belongs to the session format (full_spooky_proto-hpq.3); until then
 * each pair is one [retune] log line. */
#define RETUNE_END_UNCERTAINTY_FRAMES 3600U /* decision 0012 item 6 */

static bool retune_issued;
static bool retune_start_known;
static AudioTimelinePosition retune_start;
static RadioBand retune_band;
static uint32_t retune_target_khz;


/* The end stamp is when the foreground observed the outcome; the receiver
 * settled up to RETUNE_END_UNCERTAINTY_FRAMES earlier. The start stamp precedes
 * the receiver's ready wait and the command write, at most
 * RADIO_FAST_CTS_TIMEOUT_MS. */
static void LogRetune(const char *outcome, uint8_t outcome_code,
                      const RadioTuneStatus *tuned)
{
  AudioTimelinePosition end;
  const bool end_known = AudioPath_GetPosition(&end);
  const uint32_t frequency_khz = (tuned != NULL) ? tuned->frequency_khz : 0U;
  char start_text[21] = "-";
  char end_text[21] = "-";
  unsigned long start_epoch = 0UL;
  unsigned long end_epoch = 0UL;

  if (!retune_issued)
  {
    return;
  }
  retune_issued = false;
  if (retune_start_known)
  {
    start_epoch = (unsigned long)retune_start.epoch;
    FormatFrame(retune_start.frame, start_text);
  }
  if (end_known)
  {
    end_epoch = (unsigned long)end.epoch;
    FormatFrame(end.frame, end_text);
  }
  /* Decision 0030 item 9: the same pair, stored with the session. */
  SessionEvents_TuneEnd((uint8_t)retune_band, outcome_code,
                        (tuned != NULL) ? tuned->rssi_dbuv : 0U,
                        (tuned != NULL) ? tuned->snr_db : 0U,
                        retune_target_khz, frequency_khz, end_known ? &end : NULL);
  printf("[retune] start=%lu:%s end=%lu:%s unc=%lu band=%s target=%lu "
         "outcome=%s freq=%lu\r\n",
         start_epoch, start_text, end_epoch, end_text,
         (unsigned long)RETUNE_END_UNCERTAINTY_FRAMES,
         RadioControl_GetBandInfo(retune_band)->name,
         (unsigned long)retune_target_khz, outcome,
         (unsigned long)frequency_khz);
}

/* Decision 0015 item 5: the settle margin after an in-band tune's end, in
 * radio half-buffers. FM and AM are set by the item 10 qualification
 * (docs/evidence/2026-10-09-retune-qualification.md: no zero run outlasted its
 * end stamp). SW and LW keep the starting value until qualified with usable
 * reception. */
static const uint32_t retune_settle_blocks[RADIO_BAND_COUNT] =
{
  [RADIO_BAND_FM] = 0U,
  [RADIO_BAND_AM] = 0U,
  [RADIO_BAND_SW] = RADIO_ACTIVITY_FEED_SETTLE_BLOCKS,
  [RADIO_BAND_LW] = RADIO_ACTIVITY_FEED_SETTLE_BLOCKS
};

bool RadioAdapter_TuneInFlight(uint8_t *band, uint32_t *target_khz)
{
  if (!retune_issued)
  {
    return false;
  }
  if (band != NULL) *band = (uint8_t)retune_band;
  if (target_khz != NULL) *target_khz = retune_target_khz;
  return true;
}

uint32_t RadioAdapter_RetuneSettleFrames(uint32_t band)
{
  const uint32_t blocks = (band < (uint32_t)RADIO_BAND_COUNT) ?
    retune_settle_blocks[band] : RADIO_ACTIVITY_FEED_SETTLE_BLOCKS;

  return blocks * 512U; /* one radio half-buffer per block */
}

/* Start stamp of a retune interval on the radio sample timeline. */
static void StartRetune(uint32_t settle_blocks)
{
  uint32_t block = 0U;
  const bool known = AudioPath_GetBlockInProgress(&block);

  RadioActivityFeed_OnRetuneStart(known, block, settle_blocks);
}

void RadioAdapter_ReportStarted(bool ok)
{
  (void)AppEvents_Post(EVQ_CLASS_INTERNAL, APP_EVENT_RADIO_STARTED, ok ? 1U : 0U, 0U);
}

void RadioAdapter_ReportAudioFault(void)
{
  (void)AppEvents_Post(EVQ_CLASS_INTERNAL, APP_EVENT_RADIO_AUDIO_FAULT, 0U, 0U);
}

void RadioAdapter_Init(void)
{
  Radio_Init();
}

void RadioAdapter_Dispatch(const EvqEvent *event)
{
  RadCommand command;

  switch (event->type)
  {
    case APP_EVENT_RADIO_COMMAND:
      command.kind = (uint8_t)(event->arg0 & 0xFFU);
      command.source = (uint8_t)((event->arg0 >> 8) & 0xFFU);
      command.up = (uint8_t)((event->arg0 >> 16) & 1U);
      command.wrap = (uint8_t)((event->arg0 >> 17) & 1U);
      command.arg = event->arg1;
      command.target_khz = 0U;
      Radio_OnCommand(&command);
      break;
    case APP_EVENT_RADIO_STARTED:
      Radio_OnStarted(event->arg0 != 0U);
      break;
    case APP_EVENT_RADIO_TUNE_DONE:
      Radio_OnTuneDone();
      break;
    case APP_EVENT_RADIO_TUNE_FAILED:
      Radio_OnTuneFailed();
      break;
    case APP_EVENT_RADIO_BAND_DONE:
      Radio_OnBandDone();
      break;
    case APP_EVENT_RADIO_BAND_FAILED:
      Radio_OnBandFailed();
      break;
    case APP_EVENT_RADIO_AUDIO_FAULT:
      Radio_OnAudioFault();
      break;
    default:
      break;
  }
}

/* The completion of the tune or band switch in flight, once the service reports
 * it, and whether it has been posted. A completion the queue refuses is posted
 * again on the next pass rather than polled again, so the machine gets exactly
 * one completion per tune or switch and one that finished is never reported as
 * failed. */
static bool completion_known;
static uint16_t completion_type;
static bool completion_posted;
static uint32_t switch_target_band; /* the band switch in flight */

static void BandSwitchFailed(void);

static bool PollTuneCompletion(void)
{
  switch (RadioControl_PollTune(NULL))
  {
    case RADIO_TUNE_POLL_DONE:
      completion_type = APP_EVENT_RADIO_TUNE_DONE;
      return true;
    case RADIO_TUNE_POLL_FAILED:
      completion_type = APP_EVENT_RADIO_TUNE_FAILED;
      return true;
    case RADIO_TUNE_POLL_IDLE:
      /* The machine waits for a tune the service no longer has: fail it
       * rather than wait forever. */
      completion_type = APP_EVENT_RADIO_TUNE_FAILED;
      return true;
    case RADIO_TUNE_POLL_PENDING:
    default:
      return false;
  }
}

/* Once the receiver is in the new band with its digital output running again:
 * reseed the activity averages, open the stream gate (which ends the decision
 * 0004 gap) and unmute. A failure here, or anywhere in the switch, faults the
 * radio. */
static bool PollBandCompletion(void)
{
  switch (RadioControl_PollSwitchBand(NULL))
  {
    case RADIO_BAND_POLL_PENDING:
      return false;
    case RADIO_BAND_POLL_DONE:
      RadioActivityFeed_ResetAverages();
      AudioPath_SetStreamEnabled(true);
      if (CodecVolume_SetTransitionMuted(false))
      {
        completion_type = APP_EVENT_RADIO_BAND_DONE;
        return true;
      }
      break;
    case RADIO_BAND_POLL_FAILED:
    case RADIO_BAND_POLL_IDLE: /* as for a tune the service no longer has */
    default:
      break;
  }
  BandSwitchFailed();
  completion_type = APP_EVENT_RADIO_BAND_FAILED;
  return true;
}

void RadioAdapter_Service(void)
{
  const RadState state = Radio_GetState();

  if ((state != RAD_STATE_TUNING) && (state != RAD_STATE_SWITCHING))
  {
    return;
  }
  if (!completion_known)
  {
    completion_known = (state == RAD_STATE_TUNING) ? PollTuneCompletion()
                                                   : PollBandCompletion();
    if (!completion_known)
    {
      return;
    }
  }
  if (!completion_posted)
  {
    completion_posted = AppEvents_Post(EVQ_CLASS_INTERNAL, completion_type, 0U, 0U);
  }
}

static void SendTuneStatus(const RadioTuneStatus *tune_status)
{
  const RadioBandInfo *config;
  char response[160];

  if ((tune_status == NULL) || (tune_status->band >= RADIO_BAND_COUNT))
  {
    (void)UsbTest_SendText("ERR RADIO status unavailable\r\n");
    return;
  }
  config = RadioControl_GetBandInfo(tune_status->band);
  (void)snprintf(response, sizeof(response),
                 "OK RADIO BAND=%s FREQ=%lu kHz (%lu.%03lu MHz) "
                 "RSSI=%u SNR=%u VALID=%u\r\n",
                 config->name,
                 (unsigned long)tune_status->frequency_khz,
                 (unsigned long)(tune_status->frequency_khz / 1000U),
                 (unsigned long)(tune_status->frequency_khz % 1000U),
                 tune_status->rssi_dbuv,
                 tune_status->snr_db,
                 tune_status->valid);
  (void)UsbTest_SendText(response);
}

/* After a failed band switch the receiver is held in reset; its last result
 * would not describe what the radio track holds (Principle II). */
static const char radio_fault_reply[] = "ERR RADIO band switch failed; reset required\r\n";

void RadioAdapter_SendStatus(void)
{
  RadioControlStatus status;

  (void)RadioControl_GetStatus(&status);
  if (!status.powered && (status.last_fault == RADIO_CONTROL_FAULT_BAND_SWITCH))
  {
    (void)UsbTest_SendText(radio_fault_reply);
    return;
  }
  SendTuneStatus(&status.tune);
}

/* --- Integration called only by radio_port.c ---------------------------------- */

bool rad_integration_in_range(uint32_t frequency_khz)
{
  return RadioControl_TuneInRange(frequency_khz);
}

uint32_t rad_integration_step_target(bool up, bool wrap)
{
  return RadioControl_StepTarget(up, wrap);
}

bool rad_integration_begin_tune(uint32_t frequency_khz)
{
  completion_known = false;
  completion_posted = false;
  /* The start stamp of the retune interval, before the receiver can change
   * (decision 0015 item 3). It ends once the machine leaves Tuning, including
   * when the tune cannot be issued. */
  {
    RadioControlStatus status;

    (void)RadioControl_GetStatus(&status);
    retune_band = status.band;
  }
  StartRetune((retune_band < RADIO_BAND_COUNT) ? retune_settle_blocks[retune_band]
                                               : RADIO_ACTIVITY_FEED_SETTLE_BLOCKS);
  retune_target_khz = frequency_khz;
  retune_start_known = AudioPath_GetPosition(&retune_start);
  /* A tune that cannot be issued produces neither event (decision 0015 item 2). */
  retune_issued = RadioControl_BeginTune(frequency_khz);
  if (retune_issued)
  {
    SessionEvents_TuneStart((uint8_t)retune_band, retune_target_khz,
                            retune_start_known ? &retune_start : NULL);
  }
  return retune_issued;
}

/* A band switch that cannot start or finish (full_spooky_proto-54w.12.3, user
 * decision Q6): the receiver is held in reset and the output stays muted, but
 * the radio capture keeps running with its stream gate closed. Each radio block
 * reaches the recorder as silence, so a session keeps recording its microphone
 * track, and the decision 0004 gap the switch opened stays open: no gap end is
 * written, and a reader closes it at session end. Only a reset clears the
 * fault. */
static void BandSwitchFailed(void)
{
  AudioPath_SetStreamEnabled(false);
  (void)CodecVolume_SetTransitionMuted(true);
  RadioControl_HoldReset();
  Diagnostics_Record(DIAG_RADIO_FAULT, (uint32_t)RADIO_CONTROL_FAULT_BAND_SWITCH,
                     switch_target_band);
  BSP_LED_Off(LED_GREEN);
  BSP_LED_On(LED_RED);
  printf("[radio] FAIL: band switch; receiver reset, output muted, "
         "radio track silent until reset\r\n");
}

bool RadioAdapter_AudioReady(void)
{
  RadioControlStatus status;

  return AudioPath_IsRunning() && RadioControl_GetStatus(&status) && status.powered;
}

/* Sequences the monitored output around a receiver function change. The radio
 * service only changes the Si4735; this adapter owns muting and the DMA copy.
 * The switch itself advances in RadioAdapter_Service, one bounded bus step a
 * pass (full_spooky_proto-54w.12.2), while the closed stream gate substitutes
 * silence. */
bool rad_integration_begin_band_switch(uint32_t band_value)
{
  const RadioBand band = (RadioBand)band_value;

  completion_known = false;
  completion_posted = false;
  switch_target_band = band_value;
  if (band >= RADIO_BAND_COUNT)
  {
    BandSwitchFailed();
    return false;
  }
  /* A band switch is also not a measurement, and the new band's level is not
   * comparable with the old one's. */
  /* A band switch is a decision 0004 gap, not yet qualified (54w.12). */
  StartRetune(RADIO_ACTIVITY_FEED_SETTLE_BLOCKS);
  {
    RadioControlStatus status;

    (void)RadioControl_GetStatus(&status);
    gap_band_before = (uint8_t)status.band;
  }
  if (!CodecVolume_SetTransitionMuted(true))
  {
    BandSwitchFailed();
    return false;
  }
  AudioPath_SetStreamEnabled(false);
  if (!RadioControl_BeginSwitchBand(band))
  {
    BandSwitchFailed();
    return false;
  }
  return true;
}

static void SendRange(void)
{
  RadioControlStatus status;
  const RadioBandInfo *config;
  char response[96];

  (void)RadioControl_GetStatus(&status);
  config = RadioControl_GetBandInfo(status.band);
  (void)snprintf(response, sizeof(response), "ERR %s range: %lu..%lu kHz\r\n",
                 config->name, (unsigned long)config->minimum_khz,
                 (unsigned long)config->maximum_khz);
  (void)UsbTest_SendText(response);
}

void rad_integration_publish(RadPublished event, const RadCommand *command)
{
  RadioControlStatus status;
  const bool to_cli = (command != NULL) && (command->source == RAD_SOURCE_CLI);

  (void)RadioControl_GetStatus(&status);
  ClassicAdapter_OnRadioAnswer(event, command);
  if (to_cli && (event != RAD_PUB_TUNE_STARTED) && (event != RAD_PUB_BAND_STARTED) &&
      (cli_commands_pending != 0U))
  {
    --cli_commands_pending; /* every other event with a command answers it */
  }
#if defined(SPOOKY_DEMO)
  if ((command != NULL) && (command->source == (uint8_t)RAD_SOURCE_INTERNAL) &&
      (command->kind == (uint8_t)RAD_CMD_BAND) && (event != RAD_PUB_TUNE_STARTED) &&
      (event != RAD_PUB_BAND_STARTED) && (internal_band_pending != 0U))
  {
    --internal_band_pending;
  }
  DemoField_OnRadioAnswer(event, command);
#endif
  switch (event)
  {
    case RAD_PUB_TUNED:
      LogRetune("TUNED", SESSION_EVENT_TUNE_TUNED, &status.tune);
      RadioControl_LogTune("tuned", &status.tune);
      if (to_cli)
      {
        SendTuneStatus(&status.tune);
      }
      break;
    case RAD_PUB_TUNE_FAILED:
      LogRetune("FAILED", SESSION_EVENT_TUNE_FAILED, NULL);
      printf("[radio] tune failed at %lu kHz\r\n", (unsigned long)status.target_khz);
      if (to_cli)
      {
        (void)UsbTest_SendText("ERR RADIO tune failed\r\n");
      }
      break;
    case RAD_PUB_BAND_STARTED:
      /* PRES-RAD-06: no CLI reply, which waits for the outcome (one reply per
       * command); the monitor is already muted for the switch. */
      if ((command != NULL) && (command->arg < (uint32_t)RADIO_BAND_COUNT))
      {
        printf("[radio] band switch started %s->%s\r\n",
               RadioControl_GetBandInfo((RadioBand)gap_band_before)->name,
               RadioControl_GetBandInfo((RadioBand)command->arg)->name);
      }
      break;
    case RAD_PUB_BAND_CHANGED:
      RadioControl_LogTune("switched to", &status.tune);
      if (to_cli)
      {
        SendTuneStatus(&status.tune);
      }
      break;
    case RAD_PUB_FAULT_BAND:
      if (to_cli)
      {
        (void)UsbTest_SendText(radio_fault_reply);
      }
      break;
    case RAD_PUB_REJECTED_RANGE:
      if (to_cli)
      {
        SendRange();
      }
      break;
    case RAD_PUB_REJECTED_UNAVAILABLE:
      if (to_cli)
      {
        /* The capture still runs after a failed band switch; say why. */
        (void)UsbTest_SendText(
          (!status.powered && (status.last_fault == RADIO_CONTROL_FAULT_BAND_SWITCH))
            ? radio_fault_reply : "ERR RADIO audio path is not running\r\n");
      }
      break;
    case RAD_PUB_SUPERSEDED:
      if (to_cli)
      {
        (void)UsbTest_SendText("OK RADIO SUPERSEDED\r\n");
      }
      break;
    case RAD_PUB_ABANDONED:
      LogRetune("ABANDONED", SESSION_EVENT_TUNE_ABANDONED, NULL);
      if (to_cli)
      {
        (void)UsbTest_SendText("ERR RADIO abandoned; radio fault\r\n");
      }
      break;
    case RAD_PUB_FAULT_AUDIO:
      printf("[radio] audio fault; radio commands unavailable until reset\r\n");
      break;
    case RAD_PUB_STARTED:
    case RAD_PUB_TUNE_STARTED:
    case RAD_PUB_FAULT_START:
    default:
      break;
  }
}
