#include "radio_adapter.h"

#include <stdio.h>

#include "app_events.h"
#include "audio_path_service.h"
#include "classic_adapter.h"
#include "codec_volume_service.h"
#include "main.h"
#include "radio_activity_feed.h"
#include "radio_control_service.h"
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

bool RadioAdapter_CliCommandPending(void)
{
  return cli_commands_pending != 0U;
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

void RadioAdapter_ServiceActivity(void)
{
  const RadState state = Radio_GetState();
  uint32_t block = 0U;
  const bool known = AudioPath_GetBlockInProgress(&block);

  RadioActivityFeed_Service(HAL_GetTick(),
                            (state == RAD_STATE_SETTLED) || (state == RAD_STATE_TUNING),
                            state == RAD_STATE_TUNING, known, block);
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

/* The end stamp is when the foreground observed the outcome; the receiver
 * settled up to RETUNE_END_UNCERTAINTY_FRAMES earlier. The start stamp precedes
 * the receiver's ready wait and the command write, at most
 * RADIO_FAST_CTS_TIMEOUT_MS. */
static void LogRetune(const char *outcome, uint32_t frequency_khz)
{
  AudioTimelinePosition end;
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
  if (AudioPath_GetPosition(&end))
  {
    end_epoch = (unsigned long)end.epoch;
    FormatFrame(end.frame, end_text);
  }
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
    case APP_EVENT_RADIO_AUDIO_FAULT:
      Radio_OnAudioFault();
      break;
    default:
      break;
  }
}

/* The completion of the tune in flight, once the service reports it, and whether
 * it has been posted. A completion the queue refuses is posted again on the next
 * pass rather than polled again, so the machine gets exactly one completion per
 * tune and a tune that finished is never reported as failed. */
static bool completion_known;
static uint16_t completion_type;
static bool completion_posted;

void RadioAdapter_Service(void)
{
  if (Radio_GetState() != RAD_STATE_TUNING)
  {
    return;
  }
  if (!completion_known)
  {
    switch (RadioControl_PollTune(NULL))
    {
      case RADIO_TUNE_POLL_DONE:
        completion_type = APP_EVENT_RADIO_TUNE_DONE;
        break;
      case RADIO_TUNE_POLL_FAILED:
        completion_type = APP_EVENT_RADIO_TUNE_FAILED;
        break;
      case RADIO_TUNE_POLL_IDLE:
        /* The machine waits for a tune the service no longer has: fail it
         * rather than wait forever. */
        completion_type = APP_EVENT_RADIO_TUNE_FAILED;
        break;
      case RADIO_TUNE_POLL_PENDING:
      default:
        return;
    }
    completion_known = true;
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

void RadioAdapter_SendStatus(void)
{
  RadioControlStatus status;

  (void)RadioControl_GetStatus(&status);
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
  return retune_issued;
}

/* Sequences the monitored output around a receiver function change. The radio
 * service only changes the Si4735; this adapter owns muting and the DMA copy. */
bool rad_integration_switch_band(uint32_t band_value)
{
  const RadioBand band = (RadioBand)band_value;
  RadioTuneStatus tune_status;

  if (band >= RADIO_BAND_COUNT)
  {
    return false;
  }
  /* A band switch is also not a measurement, and the new band's level is not
   * comparable with the old one's. */
  /* A band switch is a decision 0004 gap, not yet qualified (54w.12). */
  StartRetune(RADIO_ACTIVITY_FEED_SETTLE_BLOCKS);
  if (!CodecVolume_SetTransitionMuted(true))
  {
    goto failed;
  }
  AudioPath_SetStreamEnabled(false);

  if (!RadioControl_SwitchBand(band, &tune_status))
  {
    goto failed;
  }

  RadioActivityFeed_ResetAverages();
  AudioPath_SetStreamEnabled(true);
  if (!CodecVolume_SetTransitionMuted(false))
  {
    goto failed;
  }
  return true;

failed:
  AudioPath_SetStreamEnabled(false);
  AudioPath_MarkStopped();
  (void)CodecVolume_SetTransitionMuted(true);
  RadioControl_HoldReset();
  BSP_LED_Off(LED_GREEN);
  BSP_LED_On(LED_RED);
  printf("[radio] FAIL: band switch; receiver reset and output muted\r\n");
  return false;
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
  if (to_cli && (event != RAD_PUB_TUNE_STARTED) && (cli_commands_pending != 0U))
  {
    --cli_commands_pending; /* every other event with a command answers it */
  }
  switch (event)
  {
    case RAD_PUB_TUNED:
      LogRetune("TUNED", status.tune.frequency_khz);
      RadioControl_LogTune("tuned", &status.tune);
      if (to_cli)
      {
        SendTuneStatus(&status.tune);
      }
      break;
    case RAD_PUB_TUNE_FAILED:
      LogRetune("FAILED", 0U);
      printf("[radio] tune failed at %lu kHz\r\n", (unsigned long)status.target_khz);
      if (to_cli)
      {
        (void)UsbTest_SendText("ERR RADIO tune failed\r\n");
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
        (void)UsbTest_SendText("ERR RADIO band switch failed; reset required\r\n");
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
        (void)UsbTest_SendText("ERR RADIO audio path is not running\r\n");
      }
      break;
    case RAD_PUB_SUPERSEDED:
      if (to_cli)
      {
        (void)UsbTest_SendText("OK RADIO SUPERSEDED\r\n");
      }
      break;
    case RAD_PUB_ABANDONED:
      LogRetune("ABANDONED", 0U);
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
