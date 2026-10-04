#include "radio_adapter.h"

#include <stdio.h>

#include "app_events.h"
#include "audio_path_service.h"
#include "classic_adapter.h"
#include "codec_volume_service.h"
#if defined(SPOOKY_DEMO)
#include "demo_field.h"
#endif
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

void RadioAdapter_ServiceActivity(void)
{
  const RadState state = Radio_GetState();
  uint32_t block = 0U;
  const bool known = AudioPath_GetBlockInProgress(&block);

  RadioActivityFeed_Service(HAL_GetTick(),
                            (state == RAD_STATE_SETTLED) || (state == RAD_STATE_TUNING),
                            state == RAD_STATE_TUNING, known, block);
}

/* Start stamp of a retune interval on the radio sample timeline. */
static void StartRetune(void)
{
  uint32_t block = 0U;
  const bool known = AudioPath_GetBlockInProgress(&block);

  RadioActivityFeed_OnRetuneStart(known, block);
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
  StartRetune();
  return RadioControl_BeginTune(frequency_khz);
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
  StartRetune();
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
#if defined(SPOOKY_DEMO)
  if ((command != NULL) && (command->source == (uint8_t)RAD_SOURCE_INTERNAL) &&
      (command->kind == (uint8_t)RAD_CMD_BAND) && (event != RAD_PUB_TUNE_STARTED) &&
      (internal_band_pending != 0U))
  {
    --internal_band_pending;
  }
  DemoField_OnRadioAnswer(event, command);
#endif
  switch (event)
  {
    case RAD_PUB_TUNED:
      RadioControl_LogTune("tuned", &status.tune);
      if (to_cli)
      {
        SendTuneStatus(&status.tune);
      }
      break;
    case RAD_PUB_TUNE_FAILED:
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
