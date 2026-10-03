#include "classic_service.h"

#include <stddef.h>
#include <string.h>

#include "command_policy.h"
#include "sm/radio_port.h"
#include "sm/session_port.h"

static bool service_ready;
static bool tune_outstanding;
static bool have_published;
static ClassicState published;
static ClassicServiceStats service_stats;

bool ClassicService_Init(const ClassicTerritory territories[CLASSIC_SCAN_BAND_COUNT])
{
  ClassicScanConfig config;

  ClassicScan_DefaultConfig(&config);
  service_ready = ClassicScan_Init(&config, territories);
  tune_outstanding = false;
  have_published = false;
  (void)memset(&published, 0, sizeof(published));
  (void)memset(&service_stats, 0, sizeof(service_stats));
  return service_ready;
}

/* Why Classic cannot retune now, in the order the user can act on it. */
static ClassicUnableReason UnableReason(const ClassicWorld *world)
{
  if (world->radio_state == (uint8_t)RAD_STATE_FAULTED)
  {
    return CLASSIC_UNABLE_RADIO_FAULTED;
  }
  if ((world->radio_state == (uint8_t)RAD_STATE_STOPPED) || !world->tune_valid)
  {
    return CLASSIC_UNABLE_RADIO_STOPPED;
  }
  if (CommandPolicy_Evaluate(COMMAND_ACTION_RADIO_TUNE, (SesState)world->session_state) !=
      COMMAND_POLICY_ALLOWED)
  {
    return CLASSIC_UNABLE_SESSION;
  }
  return CLASSIC_UNABLE_NONE;
}

bool ClassicService_GetState(const ClassicWorld *world, ClassicState *state)
{
  ClassicScanStatus status;

  if (!service_ready || (world == NULL) || (state == NULL) ||
      !ClassicScan_GetStatus(world->band, world->frequency_khz, &status))
  {
    return false;
  }
  state->unable_reason = (uint8_t)UnableReason(world);
  switch (status.run_state)
  {
    case CLASSIC_RUN_RUNNING:
      state->run_state = (state->unable_reason != (uint8_t)CLASSIC_UNABLE_NONE)
                       ? (uint8_t)CLASSIC_STATE_UNABLE : (uint8_t)CLASSIC_STATE_RUNNING;
      break;
    case CLASSIC_RUN_SWEEP_COMPLETE:
      state->run_state = (uint8_t)CLASSIC_STATE_SWEEP_COMPLETE;
      break;
    case CLASSIC_RUN_PAUSED:
    default:
      state->run_state = (uint8_t)CLASSIC_STATE_PAUSED;
      break;
  }
  state->band = world->band;
  state->edge = (uint8_t)status.edge;
  state->direction_up = status.direction_up;
  state->rate_limited = status.rate_limited;
  state->rate_setting_per_min = status.rate_setting_per_min;
  state->rate_per_min = status.rate_per_min;
  state->distance_channels = status.distance_channels;
  state->channel_index = status.channel_index;
  state->channel_count = status.channel_count;
  state->frequency_khz = world->frequency_khz;
  state->distance_khz = status.distance_khz;
  return true;
}

static void Publish(ClassicPublished event, uint32_t now_ms, const ClassicState *state)
{
  ++service_stats.events;
  classic_integration_publish(event, now_ms, state);
}

/* One event per changed fact, after any command or pass that may change one.
 * The first call publishes every fact, so a recording can start from them. */
static void PublishChanges(uint32_t now_ms, const ClassicWorld *world)
{
  ClassicState now;

  if (!ClassicService_GetState(world, &now))
  {
    return;
  }
  if (!have_published || (now.run_state != published.run_state) ||
      (now.unable_reason != published.unable_reason))
  {
    Publish(CLASSIC_PUB_RUN_STATE, now_ms, &now);
  }
  if (!have_published || (now.direction_up != published.direction_up))
  {
    Publish(CLASSIC_PUB_DIRECTION, now_ms, &now);
  }
  if (!have_published || (now.rate_setting_per_min != published.rate_setting_per_min) ||
      (now.rate_per_min != published.rate_per_min))
  {
    Publish(CLASSIC_PUB_RATE, now_ms, &now);
  }
  if (!have_published || (now.distance_channels != published.distance_channels) ||
      (now.distance_khz != published.distance_khz))
  {
    Publish(CLASSIC_PUB_DISTANCE, now_ms, &now);
  }
  if (!have_published || (now.edge != published.edge))
  {
    Publish(CLASSIC_PUB_EDGE, now_ms, &now);
  }
  published = now;
  have_published = true;
}

static bool IsRunning(const ClassicWorld *world)
{
  ClassicScanStatus status;

  return ClassicScan_GetStatus(world->band, world->frequency_khz, &status) &&
         (status.run_state == CLASSIC_RUN_RUNNING);
}

bool ClassicService_OnCommand(CtxCommand command, int32_t arg, uint32_t now_ms,
                              const ClassicWorld *world)
{
  if (!service_ready || (world == NULL) || (world->band >= CLASSIC_SCAN_BAND_COUNT))
  {
    return false;
  }
  switch (command)
  {
    case CTX_CMD_RUN_PAUSE:
      if ((arg == CLASSIC_RUN_TOGGLE) ||
          ((arg == CLASSIC_RUN_ENSURE_RUNNING) && !IsRunning(world)) ||
          ((arg == CLASSIC_RUN_ENSURE_PAUSED) && IsRunning(world)))
      {
        ClassicScan_ToggleRun();
      }
      else if ((arg != CLASSIC_RUN_ENSURE_RUNNING) && (arg != CLASSIC_RUN_ENSURE_PAUSED))
      {
        return false;
      }
      break;
    case CTX_CMD_TOGGLE_DIRECTION:
      ClassicScan_ToggleDirection();
      break;
    case CTX_CMD_JUMP_RATE:
      (void)ClassicScan_StepRate(arg);
      break;
    case CTX_CMD_JUMP_DISTANCE:
      (void)ClassicScan_StepDistance(world->band, arg);
      break;
    case CTX_CMD_EDGE_BEHAVIOR:
      (void)ClassicScan_StepEdge(arg);
      break;
    default:
      return false; /* the activity hold is full_spooky_proto-54w.32 */
  }
  PublishChanges(now_ms, world);
  return true;
}

void ClassicService_OnTuneAnswered(bool tuned)
{
  if (!tune_outstanding)
  {
    return;
  }
  tune_outstanding = false;
  if (!tuned)
  {
    /* The frequency did not change; the next jump starts from it. */
    ++service_stats.tunes_failed;
  }
}

void ClassicService_Service(uint32_t now_ms, const ClassicWorld *world)
{
  ClassicScanInput input;
  ClassicScanJump jump;

  if (!service_ready || (world == NULL) || (world->band >= CLASSIC_SCAN_BAND_COUNT))
  {
    return;
  }
  input.band = world->band;
  input.frequency_khz = world->frequency_khz;
  input.tuning_valid = world->tune_valid;
  /* Never a second tune in flight: neither behind the radio's own tune nor
   * behind Classic's command still waiting in the queue. */
  input.tune_in_flight = tune_outstanding || world->radio_command_pending ||
                         (world->radio_state == (uint8_t)RAD_STATE_TUNING);
  input.active = world->active;
  input.can_tune = UnableReason(world) == CLASSIC_UNABLE_NONE;

  if (ClassicScan_Service(now_ms, &input, &jump))
  {
    ++service_stats.jumps;
    if (jump.tune)
    {
      if (classic_integration_request_tune(jump.frequency_khz))
      {
        tune_outstanding = true;
        ++service_stats.tunes_requested;
      }
      else
      {
        ++service_stats.tunes_refused;
      }
    }
  }
  PublishChanges(now_ms, world);
}

void ClassicService_GetStats(ClassicServiceStats *stats)
{
  if (stats != NULL)
  {
    *stats = service_stats;
    stats->tune_outstanding = tune_outstanding;
  }
}

const char *ClassicService_RunName(uint8_t run_state)
{
  static const char *const names[] = {"RUNNING", "PAUSED", "SWEEP_COMPLETE", "UNABLE"};

  return (run_state < (sizeof(names) / sizeof(names[0]))) ? names[run_state] : "UNKNOWN";
}

const char *ClassicService_ReasonName(uint8_t reason)
{
  static const char *const names[] = {"NONE", "RADIO_STOPPED", "RADIO_FAULTED", "SESSION"};

  return (reason < (sizeof(names) / sizeof(names[0]))) ? names[reason] : "UNKNOWN";
}

const char *ClassicService_EdgeName(uint8_t edge)
{
  static const char *const names[] = {"WRAP", "BOUNCE", "STOP"};

  return (edge < (sizeof(names) / sizeof(names[0]))) ? names[edge] : "UNKNOWN";
}

const char *ClassicService_EventName(ClassicPublished event)
{
  static const char *const names[CLASSIC_PUB_COUNT] = {
    "RUN_STATE", "DIRECTION", "RATE", "DISTANCE", "EDGE"
  };

  return ((uint32_t)event < (uint32_t)CLASSIC_PUB_COUNT) ? names[event] : "UNKNOWN";
}
