#include "classic_scan.h"

#include <stddef.h>

#define CLASSIC_MS_PER_MINUTE 60000U
/* Larger turns clamp at the table ends anyway; this keeps index sums small. */
#define CLASSIC_DETENT_LIMIT 255

static ClassicScanConfig scan_config;
static ClassicTerritory scan_territories[CLASSIC_SCAN_BAND_COUNT];
static bool scan_ready;

/* Classic's own parameters and run state (spec 001 Key Entities). */
static bool scan_running;
static bool scan_up;
static ClassicEdge scan_edge;
static uint8_t scan_rate_index;
static uint16_t scan_distance[CLASSIC_SCAN_BAND_COUNT];

/* Jump schedule (decision 0016 items 7, 8, 21). due_frac counts the
 * remainder of 60000 / due_rate in 1/due_rate ms, so periods that are not a
 * whole number of milliseconds do not drift. */
static bool schedule_armed;
static bool resume_pending;
static bool waited_for_tune;
static uint32_t due_ms;
static uint32_t due_frac;
static uint16_t due_rate;

uint16_t ClassicScan_ChannelCount(const ClassicTerritory *territory)
{
  if ((territory == NULL) || (territory->step_khz == 0U) ||
      (territory->maximum_khz < territory->minimum_khz))
  {
    return 0U;
  }
  return (uint16_t)(((territory->maximum_khz - territory->minimum_khz) /
                     territory->step_khz) + 1U);
}

uint16_t ClassicScan_NearestChannel(const ClassicTerritory *territory,
                                    uint32_t frequency_khz)
{
  const uint16_t count = ClassicScan_ChannelCount(territory);

  if ((count == 0U) || (frequency_khz <= territory->minimum_khz))
  {
    return 0U;
  }
  if (frequency_khz >= territory->maximum_khz)
  {
    return (uint16_t)(count - 1U);
  }
  return (uint16_t)((frequency_khz - territory->minimum_khz +
                     (territory->step_khz / 2U)) / territory->step_khz);
}

uint32_t ClassicScan_ChannelKhz(const ClassicTerritory *territory,
                                uint16_t channel_index)
{
  const uint16_t count = ClassicScan_ChannelCount(territory);

  if (count == 0U)
  {
    return 0U;
  }
  if (channel_index >= count)
  {
    channel_index = (uint16_t)(count - 1U);
  }
  return territory->minimum_khz + ((uint32_t)channel_index * territory->step_khz);
}

void ClassicScan_DefaultConfig(ClassicScanConfig *config)
{
  static const uint16_t rates[] =
  {
    6U, 10U, 15U, 20U, 30U, 40U, 60U, 80U, 100U, 120U, 150U, 180U, 240U, 300U,
    400U
  };
  static const uint16_t distances[] =
  {
    1U, 2U, 3U, 4U, 5U, 7U, 10U, 15U, 20U, 30U, 50U, 70U, 100U, 150U, 200U,
    300U, 500U, 700U, 1000U, 1500U, 2000U
  };
  uint32_t index;

  if (config == NULL)
  {
    return;
  }
  *config = (ClassicScanConfig){0};
  for (index = 0U; index < (sizeof(rates) / sizeof(rates[0])); ++index)
  {
    config->rate_per_min[index] = rates[index];
  }
  config->rate_count = (uint8_t)(sizeof(rates) / sizeof(rates[0]));
  config->default_rate_index = 9U; /* 120 per minute, one jump every 500 ms */
  /* FM, AM, SW, LW: period above the worst tune round trip measured while
   * recording, with margin. LW takes AM's until it is measured. */
  config->band_max_rate_per_min[0] = 400U;
  config->band_max_rate_per_min[1] = 200U;
  config->band_max_rate_per_min[2] = 240U;
  config->band_max_rate_per_min[3] = 200U;
  for (index = 0U; index < (sizeof(distances) / sizeof(distances[0])); ++index)
  {
    config->distance_channels[index] = distances[index];
  }
  config->distance_count = (uint8_t)(sizeof(distances) / sizeof(distances[0]));
  config->default_distance[0] = 1U;  /* FM 100 kHz */
  config->default_distance[1] = 1U;  /* AM 10 kHz */
  config->default_distance[2] = 20U; /* SW 100 kHz */
  config->default_distance[3] = 1U;  /* LW 9 kHz */
  config->start_running = true;
  config->start_up = true;
  config->start_edge = CLASSIC_EDGE_WRAP;
}

/* Distance sequence of one band: the table entries below the cap floor(N / 2),
 * then the cap itself (decision 0016 item 9). */
static uint16_t DistanceCap(const ClassicTerritory *territory)
{
  return (uint16_t)(ClassicScan_ChannelCount(territory) / 2U);
}

static uint8_t DistanceStepsBelowCap(const ClassicScanConfig *config,
                                     uint16_t cap)
{
  uint8_t below = 0U;

  while ((below < config->distance_count) &&
         (config->distance_channels[below] < cap))
  {
    ++below;
  }
  return below;
}

static bool DistancePosition(const ClassicScanConfig *config,
                             const ClassicTerritory *territory,
                             uint16_t distance, uint8_t *position)
{
  const uint16_t cap = DistanceCap(territory);
  const uint8_t below = DistanceStepsBelowCap(config, cap);
  uint8_t index;

  if (distance == cap)
  {
    *position = below;
    return true;
  }
  for (index = 0U; index < below; ++index)
  {
    if (config->distance_channels[index] == distance)
    {
      *position = index;
      return true;
    }
  }
  return false;
}

static bool TerritoryValid(const ClassicTerritory *territory)
{
  return (territory->step_khz != 0U) &&
         (territory->maximum_khz > territory->minimum_khz) &&
         (((territory->maximum_khz - territory->minimum_khz) %
           territory->step_khz) == 0U) &&
         (((territory->maximum_khz - territory->minimum_khz) /
           territory->step_khz) < 0xFFFFU);
}

static bool StrictlyIncreasing(const uint16_t *values, uint8_t count)
{
  uint8_t index;

  if (values[0] == 0U)
  {
    return false;
  }
  for (index = 1U; index < count; ++index)
  {
    if (values[index] <= values[index - 1U])
    {
      return false;
    }
  }
  return true;
}

bool ClassicScan_Init(const ClassicScanConfig *config,
                      const ClassicTerritory territories[CLASSIC_SCAN_BAND_COUNT])
{
  uint8_t band;
  uint8_t position;

  if ((config == NULL) || (territories == NULL) ||
      (config->rate_count == 0U) ||
      (config->rate_count > CLASSIC_SCAN_RATE_STEPS_MAX) ||
      (config->default_rate_index >= config->rate_count) ||
      (config->distance_count == 0U) ||
      (config->distance_count > CLASSIC_SCAN_DISTANCE_STEPS_MAX) ||
      ((uint32_t)config->start_edge >= (uint32_t)CLASSIC_EDGE_COUNT) ||
      !StrictlyIncreasing(config->rate_per_min, config->rate_count) ||
      !StrictlyIncreasing(config->distance_channels, config->distance_count))
  {
    return false;
  }
  for (band = 0U; band < CLASSIC_SCAN_BAND_COUNT; ++band)
  {
    /* At least two channels, so the cap is at least one channel. */
    if ((config->band_max_rate_per_min[band] == 0U) ||
        !TerritoryValid(&territories[band]) ||
        !DistancePosition(config, &territories[band],
                          config->default_distance[band], &position))
    {
      return false;
    }
  }

  scan_config = *config;
  for (band = 0U; band < CLASSIC_SCAN_BAND_COUNT; ++band)
  {
    scan_territories[band] = territories[band];
    scan_distance[band] = config->default_distance[band];
  }
  scan_running = config->start_running;
  scan_up = config->start_up;
  scan_edge = config->start_edge;
  scan_rate_index = config->default_rate_index;
  schedule_armed = false;
  resume_pending = false;
  waited_for_tune = false;
  due_ms = 0U;
  due_frac = 0U;
  due_rate = 0U;
  scan_ready = true;
  return true;
}

static uint16_t EffectiveRate(uint8_t band)
{
  const uint16_t setting = scan_config.rate_per_min[scan_rate_index];
  const uint16_t maximum = scan_config.band_max_rate_per_min[band];

  return (setting > maximum) ? maximum : setting;
}

static int32_t ClampDetents(int32_t detents)
{
  if (detents > CLASSIC_DETENT_LIMIT)
  {
    return CLASSIC_DETENT_LIMIT;
  }
  if (detents < -CLASSIC_DETENT_LIMIT)
  {
    return -CLASSIC_DETENT_LIMIT;
  }
  return detents;
}

/* Moves position by detents within 0..count-1, stopping at both ends. */
static uint8_t StepWithin(uint8_t position, uint8_t count, int32_t detents)
{
  int32_t target = (int32_t)position + ClampDetents(detents);

  if (target < 0)
  {
    target = 0;
  }
  if (target > ((int32_t)count - 1))
  {
    target = (int32_t)count - 1;
  }
  return (uint8_t)target;
}

void ClassicScan_ToggleRun(void)
{
  if (!scan_ready)
  {
    return;
  }
  scan_running = !scan_running;
  /* A resume makes its first jump at once (item 8); a pause cancels it. */
  resume_pending = scan_running;
  schedule_armed = false;
}

void ClassicScan_ToggleDirection(void)
{
  if (scan_ready)
  {
    scan_up = !scan_up;
  }
}

bool ClassicScan_StepRate(int32_t detents)
{
  const uint8_t previous = scan_rate_index;

  if (!scan_ready)
  {
    return false;
  }
  scan_rate_index = StepWithin(scan_rate_index, scan_config.rate_count, detents);
  return scan_rate_index != previous;
}

bool ClassicScan_StepDistance(uint8_t band, int32_t detents)
{
  const ClassicTerritory *territory;
  uint8_t position;
  uint8_t below;
  uint16_t cap;
  uint16_t previous;

  if (!scan_ready || (band >= CLASSIC_SCAN_BAND_COUNT))
  {
    return false;
  }
  territory = &scan_territories[band];
  cap = DistanceCap(territory);
  below = DistanceStepsBelowCap(&scan_config, cap);
  previous = scan_distance[band];
  if (!DistancePosition(&scan_config, territory, previous, &position))
  {
    return false; /* unreachable: only sequence values are ever stored */
  }
  position = StepWithin(position, (uint8_t)(below + 1U), detents);
  scan_distance[band] = (position < below)
                      ? scan_config.distance_channels[position] : cap;
  return scan_distance[band] != previous;
}

bool ClassicScan_StepEdge(int32_t detents)
{
  const ClassicEdge previous = scan_edge;

  if (!scan_ready)
  {
    return false;
  }
  scan_edge = (ClassicEdge)StepWithin((uint8_t)scan_edge,
                                      (uint8_t)CLASSIC_EDGE_COUNT, detents);
  return scan_edge != previous;
}

/* Next due time, one period of `rate` after the current one. */
static void AdvanceDue(uint16_t rate)
{
  if (rate != due_rate)
  {
    due_rate = rate;
    due_frac = 0U;
  }
  due_ms += CLASSIC_MS_PER_MINUTE / rate;
  due_frac += CLASSIC_MS_PER_MINUTE % rate;
  if (due_frac >= rate)
  {
    due_frac -= rate;
    ++due_ms;
  }
}

static void RestartScheduleAt(uint32_t now_ms, uint16_t rate)
{
  due_ms = now_ms;
  due_frac = 0U;
  due_rate = rate;
  AdvanceDue(rate);
}

static bool Reached(uint32_t now_ms, uint32_t when_ms)
{
  return (int32_t)(now_ms - when_ms) >= 0;
}

/* The landing of one jump from channel `from` (items 12 to 15). */
static uint16_t Land(uint16_t from, uint16_t count, uint16_t distance,
                     ClassicScanJump *jump)
{
  const uint16_t last = (uint16_t)(count - 1U);

  if (resume_pending)
  {
    resume_pending = false;
    /* On an edge pointing out of the band, a resume starts a new sweep from
     * the opposite edge, in every edge mode (item 15). */
    if (scan_up && (from == last))
    {
      return 0U;
    }
    if (!scan_up && (from == 0U))
    {
      return last;
    }
  }

  switch (scan_edge)
  {
    case CLASSIC_EDGE_BOUNCE:
      /* distance <= count / 2, so one reflection always suffices. */
      if (scan_up)
      {
        const uint32_t target = (uint32_t)from + distance;

        if (target <= last)
        {
          return (uint16_t)target;
        }
        scan_up = false;
        jump->direction_changed = true;
        return (uint16_t)((2U * (uint32_t)last) - target);
      }
      if (from >= distance)
      {
        return (uint16_t)(from - distance);
      }
      scan_up = true;
      jump->direction_changed = true;
      return (uint16_t)(distance - from);

    case CLASSIC_EDGE_STOP:
      if (scan_up)
      {
        if (((uint32_t)from + distance) <= last)
        {
          return (uint16_t)(from + distance);
        }
        scan_running = false;
        jump->sweep_complete = true;
        return last;
      }
      if (from >= distance)
      {
        return (uint16_t)(from - distance);
      }
      scan_running = false;
      jump->sweep_complete = true;
      return 0U;

    case CLASSIC_EDGE_WRAP:
    default:
      return scan_up ? (uint16_t)(((uint32_t)from + distance) % count)
                     : (uint16_t)(((uint32_t)from + count - distance) % count);
  }
}

bool ClassicScan_Service(uint32_t now_ms, const ClassicScanInput *input,
                         ClassicScanJump *jump)
{
  const ClassicTerritory *territory;
  uint16_t rate;
  uint16_t from;
  uint16_t landing;

  if (!scan_ready || (input == NULL) || (jump == NULL) ||
      (input->band >= CLASSIC_SCAN_BAND_COUNT))
  {
    return false;
  }
  /* Nothing is issued or retried while Classic cannot move (item 21). */
  if (!scan_running || !input->active || !input->can_tune ||
      !input->tuning_valid)
  {
    schedule_armed = false;
    waited_for_tune = false;
    return false;
  }

  rate = EffectiveRate(input->band);
  if (!schedule_armed)
  {
    /* A resume jumps at once; startup, return to Classic and recovery wait
     * one period (items 8, 21). */
    schedule_armed = true;
    waited_for_tune = false;
    if (resume_pending)
    {
      due_ms = now_ms;
      due_frac = 0U;
      due_rate = rate;
    }
    else
    {
      RestartScheduleAt(now_ms, rate);
    }
  }
  if (!Reached(now_ms, due_ms))
  {
    return false;
  }
  if (input->tune_in_flight)
  {
    waited_for_tune = true; /* never two tunes in flight (item 7) */
    return false;
  }

  territory = &scan_territories[input->band];
  from = ClassicScan_NearestChannel(territory, input->frequency_khz);
  jump->direction_changed = false;
  jump->sweep_complete = false;
  landing = Land(from, ClassicScan_ChannelCount(territory),
                 scan_distance[input->band], jump);
  jump->channel_index = landing;
  jump->frequency_khz = ClassicScan_ChannelKhz(territory, landing);
  jump->tune = jump->frequency_khz != input->frequency_khz;

  if (!scan_running)
  {
    schedule_armed = false; /* stop completed the sweep */
  }
  else
  {
    /* On time, the next jump is one period after this one was due, so
     * timing does not drift. After waiting for a tune, or once a whole period
     * has been missed, the schedule starts again from now: no catch-up
     * burst (item 7). */
    AdvanceDue(rate);
    if (waited_for_tune || Reached(now_ms, due_ms))
    {
      RestartScheduleAt(now_ms, rate);
    }
  }
  waited_for_tune = false;
  return true;
}

bool ClassicScan_GetStatus(uint8_t band, uint32_t frequency_khz,
                           ClassicScanStatus *status)
{
  const ClassicTerritory *territory;
  uint16_t count;
  uint16_t index;

  if (!scan_ready || (status == NULL) || (band >= CLASSIC_SCAN_BAND_COUNT))
  {
    return false;
  }
  territory = &scan_territories[band];
  count = ClassicScan_ChannelCount(territory);
  index = ClassicScan_NearestChannel(territory, frequency_khz);

  if (scan_running)
  {
    status->run_state = CLASSIC_RUN_RUNNING;
  }
  else if ((scan_up && (index == (uint16_t)(count - 1U))) ||
           (!scan_up && (index == 0U)))
  {
    status->run_state = CLASSIC_RUN_SWEEP_COMPLETE;
  }
  else
  {
    status->run_state = CLASSIC_RUN_PAUSED;
  }
  status->direction_up = scan_up;
  status->edge = scan_edge;
  status->rate_setting_per_min = scan_config.rate_per_min[scan_rate_index];
  status->rate_per_min = EffectiveRate(band);
  status->rate_limited = status->rate_per_min != status->rate_setting_per_min;
  status->distance_channels = scan_distance[band];
  status->distance_khz = (uint32_t)scan_distance[band] * territory->step_khz;
  status->channel_index = index;
  status->channel_count = count;
  return true;
}
