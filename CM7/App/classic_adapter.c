#include "classic_adapter.h"

#include <stdio.h>
#include <string.h>

#include "app_events.h"
#include "audio_path_service.h"
#include "classic_service.h"
#include "main.h"
#include "radio_adapter.h"
#include "radio_control_service.h"
#include "sm/session_port.h"
#include "usb_test.h"

_Static_assert((uint32_t)RADIO_BAND_COUNT == CLASSIC_SCAN_BAND_COUNT,
               "Classic territories are indexed like RadioBand");

/* arg0 of APP_EVENT_CLASSIC_COMMAND: CtxCommand in bits 0-7, RadSource in 8-15. */
#define CLASSIC_COMMAND_MASK 0xFFU
#define CLASSIC_SOURCE_SHIFT 8U

static void ReadWorld(ClassicWorld *world)
{
  RadioControlStatus status;

  (void)RadioControl_GetStatus(&status);
  world->band = (uint8_t)status.tune.band;
  world->frequency_khz = status.tune.frequency_khz;
  /* status.tune.valid is the receiver's valid-station flag, not whether a tune
   * completed: empty channels are part of a scan. The boot sequence completes a
   * tune before the radio starts, and a band switch replaces it. */
  world->tune_valid = (status.tune.frequency_khz != 0U) &&
                      (status.tune.band == status.band);
  world->radio_state = (uint8_t)Radio_GetState();
  world->radio_command_pending = RadioAdapter_CliCommandPending();
  world->session_state = (uint8_t)Session_GetState();
  /* Classic is the only Field engine in firmware until Context is wired
   * (full_spooky_proto-54w.4, 54w.5; p04.3 for the demo). */
  world->active = true;
}

void ClassicAdapter_Init(void)
{
  ClassicTerritory territories[CLASSIC_SCAN_BAND_COUNT];
  uint32_t band;

  for (band = 0U; band < CLASSIC_SCAN_BAND_COUNT; ++band)
  {
    const RadioBandInfo *info = RadioControl_GetBandInfo((RadioBand)band);

    territories[band].minimum_khz = info->minimum_khz;
    territories[band].maximum_khz = info->maximum_khz;
    territories[band].step_khz = info->step_khz;
  }
  if (!ClassicService_Init(territories))
  {
    printf("[classic] FAIL: configuration rejected; Classic is off\r\n");
  }
}

void ClassicAdapter_Service(void)
{
  ClassicWorld world;

  ReadWorld(&world);
  ClassicService_Service(HAL_GetTick(), &world);
}

bool ClassicAdapter_RequestCommand(CtxCommand command, int32_t arg)
{
  return AppEvents_Post(EVQ_CLASS_EXTERNAL_COMMAND, APP_EVENT_CLASSIC_COMMAND,
                        ((uint32_t)command & CLASSIC_COMMAND_MASK) |
                        ((uint32_t)RAD_SOURCE_CLI << CLASSIC_SOURCE_SHIFT),
                        (uint32_t)arg);
}

void ClassicAdapter_Dispatch(const EvqEvent *event)
{
  ClassicWorld world;
  bool handled;

  if (event->type != (uint16_t)APP_EVENT_CLASSIC_COMMAND)
  {
    return;
  }
  ReadWorld(&world);
  handled = ClassicService_OnCommand((CtxCommand)(event->arg0 & CLASSIC_COMMAND_MASK),
                                     (int32_t)event->arg1, HAL_GetTick(), &world);
  if (((event->arg0 >> CLASSIC_SOURCE_SHIFT) & 0xFFU) == (uint32_t)RAD_SOURCE_CLI)
  {
    if (handled)
    {
      ClassicAdapter_SendStatus();
    }
    else
    {
      (void)UsbTest_SendText("ERR CLASSIC command unavailable\r\n");
    }
  }
}

void ClassicAdapter_OnRadioAnswer(RadPublished event, const RadCommand *command)
{
  if ((command == NULL) || (command->source != (uint8_t)RAD_SOURCE_INTERNAL) ||
      (command->kind != (uint8_t)RAD_CMD_TUNE))
  {
    return;
  }
  switch (event)
  {
    case RAD_PUB_TUNED:
      ClassicService_OnTuneAnswered(true);
      break;
    case RAD_PUB_TUNE_FAILED:
    case RAD_PUB_REJECTED_RANGE:
    case RAD_PUB_REJECTED_UNAVAILABLE:
    case RAD_PUB_SUPERSEDED:
    case RAD_PUB_ABANDONED:
      ClassicService_OnTuneAnswered(false);
      break;
    default:
      break; /* not an answer: started, tune started, faults */
  }
}

/* A signed decimal detent count of at most four digits, alone on the line. */
static bool ParseDetents(const char *text, int32_t *detents)
{
  int32_t value = 0;
  bool negative = false;
  uint32_t digits = 0U;

  if ((*text == '+') || (*text == '-'))
  {
    negative = *text == '-';
    ++text;
  }
  while ((*text >= '0') && (*text <= '9') && (digits < 5U))
  {
    value = (value * 10) + (int32_t)(*text - '0');
    ++text;
    ++digits;
  }
  if ((digits == 0U) || (digits > 4U) || (*text != '\0'))
  {
    return false;
  }
  *detents = negative ? -value : value;
  return true;
}

bool ClassicAdapter_HandleCommand(const char *command)
{
  static const struct
  {
    const char *word;
    CtxCommand command;
    int32_t arg;
    bool detents;
  } commands[] =
  {
    {"RUN", CTX_CMD_RUN_PAUSE, CLASSIC_RUN_ENSURE_RUNNING, false},
    {"PAUSE", CTX_CMD_RUN_PAUSE, CLASSIC_RUN_ENSURE_PAUSED, false},
    {"TOGGLE", CTX_CMD_RUN_PAUSE, CLASSIC_RUN_TOGGLE, false},   /* C-103 */
    {"DIR", CTX_CMD_TOGGLE_DIRECTION, 0, false},                /* C-105 */
    {"RATE ", CTX_CMD_JUMP_RATE, 0, true},                      /* C-104 */
    {"DIST ", CTX_CMD_JUMP_DISTANCE, 0, true},                  /* C-106 */
    {"EDGE ", CTX_CMD_EDGE_BEHAVIOR, 0, true}                   /* C-110 */
  };
  const char *rest;
  uint32_t index;

  if (strncmp(command, "CLASSIC", 7U) != 0)
  {
    return false;
  }
  if (command[7] == '\0')
  {
    ClassicAdapter_SendStatus();
    return true;
  }
  if (command[7] != ' ')
  {
    return false;
  }
  rest = &command[8];
  for (index = 0U; index < (sizeof(commands) / sizeof(commands[0])); ++index)
  {
    const size_t length = strlen(commands[index].word);
    int32_t arg = commands[index].arg;

    if (commands[index].detents
        ? ((strncmp(rest, commands[index].word, length) != 0) ||
           !ParseDetents(&rest[length], &arg))
        : (strcmp(rest, commands[index].word) != 0))
    {
      continue;
    }
    if (!ClassicAdapter_RequestCommand(commands[index].command, arg))
    {
      (void)UsbTest_SendText("ERR BUSY\r\n");
    }
    return true;
  }
  (void)UsbTest_SendText(
    "ERR usage: CLASSIC [RUN|PAUSE|TOGGLE|DIR|RATE n|DIST n|EDGE n]\r\n");
  return true;
}

static const char *BandName(uint8_t band)
{
  const RadioBandInfo *info = RadioControl_GetBandInfo((RadioBand)band);

  return (info != NULL) ? info->name : "?";
}

void ClassicAdapter_SendStatus(void)
{
  ClassicWorld world;
  ClassicState state;
  ClassicServiceStats stats;
  char response[256];

  ReadWorld(&world);
  if (!ClassicService_GetState(&world, &state))
  {
    (void)UsbTest_SendText("ERR CLASSIC unavailable\r\n");
    return;
  }
  ClassicService_GetStats(&stats);
  (void)snprintf(response, sizeof(response),
                 "OK CLASSIC STATE=%s REASON=%s BAND=%s FREQ=%lu CH=%u/%u DIR=%s "
                 "RATE=%u SET=%u LIMITED=%u DIST=%u DIST_KHZ=%lu EDGE=%s "
                 "JUMPS=%lu REFUSED=%lu FAILED=%lu\r\n",
                 ClassicService_RunName(state.run_state),
                 ClassicService_ReasonName(state.unable_reason),
                 BandName(state.band), (unsigned long)state.frequency_khz,
                 (unsigned)state.channel_index, (unsigned)state.channel_count,
                 state.direction_up ? "UP" : "DOWN",
                 (unsigned)state.rate_per_min, (unsigned)state.rate_setting_per_min,
                 state.rate_limited ? 1U : 0U, (unsigned)state.distance_channels,
                 (unsigned long)state.distance_khz,
                 ClassicService_EdgeName(state.edge),
                 (unsigned long)stats.jumps, (unsigned long)stats.tunes_refused,
                 (unsigned long)stats.tunes_failed);
  (void)UsbTest_SendText(response);
}

/* --- Integration called only by classic_service.c ----------------------------- */

bool classic_integration_request_tune(uint32_t frequency_khz)
{
  return RadioAdapter_RequestInternalTune(frequency_khz);
}

/* Decimal text of a 64-bit frame count; newlib-nano printf has no %llu. */
static void FormatFrame(uint64_t frame, char text[21])
{
  char digits[21];
  uint32_t count = 0U;
  uint32_t index;

  do
  {
    digits[count++] = (char)('0' + (frame % 10U));
    frame /= 10U;
  } while ((frame != 0U) && (count < 20U));
  for (index = 0U; index < count; ++index)
  {
    text[index] = digits[count - 1U - index];
  }
  text[count] = '\0';
}

/* One log line per event: the integration clock and, while the radio stream
 * runs, its sample position, so a session can place the event on its timeline
 * (decision 0012). Storing events in the session belongs to the session format
 * (full_spooky_proto-hpq.3). */
void classic_integration_publish(ClassicPublished event, uint32_t now_ms,
                                 const ClassicState *state)
{
  AudioTimelinePosition position;
  char frame[21] = "-";
  unsigned long epoch = 0UL;

  if (AudioPath_GetPosition(&position))
  {
    epoch = (unsigned long)position.epoch;
    FormatFrame(position.frame, frame);
  }
  printf("[classic] t=%lu pos=%lu:%s %s STATE=%s REASON=%s DIR=%s RATE=%u/%u "
         "LIMITED=%u DIST=%u EDGE=%s BAND=%s FREQ=%lu\r\n",
         (unsigned long)now_ms, epoch, frame, ClassicService_EventName(event),
         ClassicService_RunName(state->run_state),
         ClassicService_ReasonName(state->unable_reason),
         state->direction_up ? "UP" : "DOWN", (unsigned)state->rate_per_min,
         (unsigned)state->rate_setting_per_min, state->rate_limited ? 1U : 0U,
         (unsigned)state->distance_channels, ClassicService_EdgeName(state->edge),
         BandName(state->band), (unsigned long)state->frequency_khz);
}
