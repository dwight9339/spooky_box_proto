#include "radio_activity.h"

#include <stddef.h>
#include <string.h>

#define RADIO_ACTIVITY_ALPHA_ONE 65536U

static RadioActivityConfig activity_config;
static RadioActivityStatus activity;
static uint8_t hold_blocks;

static uint32_t Smooth(uint32_t average_q8, uint32_t level_q8, uint32_t alpha)
{
  if (level_q8 >= average_q8)
  {
    return average_q8 +
      (uint32_t)(((uint64_t)(level_q8 - average_q8) * alpha) >> 16U);
  }
  return average_q8 -
    (uint32_t)(((uint64_t)(average_q8 - level_q8) * alpha) >> 16U);
}

static uint8_t LevelCrossed(uint32_t ratio_q8)
{
  uint8_t level = 0U;

  while ((level < RADIO_ACTIVITY_ONSET_LEVELS) &&
         (ratio_q8 >= activity_config.onset_ratio_q8[level]))
  {
    ++level;
  }
  return level;
}

void RadioActivity_DefaultConfig(RadioActivityConfig *config)
{
  if (config == NULL)
  {
    return;
  }
  config->fast_alpha_q16 = 19608U;
  config->slow_alpha_q16 = 695U;
  config->onset_ratio_q8[0] = 384U;
  config->onset_ratio_q8[1] = 640U;
  config->onset_ratio_q8[2] = 1024U;
  config->rearm_ratio_q8 = 320U;
  config->level_floor = 16U;
  config->max_hold_blocks = 4U;
}

bool RadioActivity_Init(const RadioActivityConfig *config)
{
  uint32_t index;

  if ((config == NULL) ||
      (config->fast_alpha_q16 == 0U) ||
      (config->fast_alpha_q16 > RADIO_ACTIVITY_ALPHA_ONE) ||
      (config->slow_alpha_q16 == 0U) ||
      (config->slow_alpha_q16 > RADIO_ACTIVITY_ALPHA_ONE) ||
      (config->rearm_ratio_q8 >= config->onset_ratio_q8[0]) ||
      (config->level_floor == 0U) ||
      (config->level_floor > (UINT32_MAX >> 8U)) ||
      (config->max_hold_blocks == 0U))
  {
    return false;
  }
  for (index = 1U; index < RADIO_ACTIVITY_ONSET_LEVELS; ++index)
  {
    if (config->onset_ratio_q8[index] <= config->onset_ratio_q8[index - 1U])
    {
      return false;
    }
  }
  activity_config = *config;
  (void)memset(&activity, 0, sizeof(activity));
  hold_blocks = 0U;
  return true;
}

void RadioActivity_Reset(void)
{
  activity.seeded = false;
  activity.armed = false;
  activity.pending = 0U;
  activity.fast_q8 = 0U;
  activity.slow_q8 = 0U;
  activity.ratio_q8 = 0U;
  hold_blocks = 0U;
}

uint32_t RadioActivity_MeanAbs(const int16_t *samples, uint32_t count)
{
  uint32_t sum = 0U;
  uint32_t index;

  if ((samples == NULL) || (count == 0U))
  {
    return 0U;
  }
  for (index = 0U; index < count; ++index)
  {
    const int32_t sample = samples[index];

    sum += (uint32_t)((sample < 0) ? -sample : sample);
  }
  return sum / count;
}

RadioOnset RadioActivity_OnBlock(uint32_t mean_abs, bool measuring)
{
  const uint32_t level_q8 =
    (mean_abs > 0xFFFFU) ? (0xFFFFU << 8U) : (mean_abs << 8U);
  const uint32_t floor_q8 = activity_config.level_floor << 8U;
  uint32_t previous_ratio;
  uint8_t crossed;
  RadioOnset onset = RADIO_ONSET_NONE;

  if (!measuring)
  {
    ++activity.unmeasured_blocks;
    activity.pending = 0U;
    hold_blocks = 0U;
    return RADIO_ONSET_NONE;
  }
  ++activity.measured_blocks;
  if (!activity.seeded)
  {
    /* Seed both averages from the first block so start-up reads as steady. */
    activity.seeded = true;
    activity.armed = true;
    activity.fast_q8 = level_q8;
    activity.slow_q8 = level_q8;
    activity.ratio_q8 = 256U;
    return RADIO_ONSET_NONE;
  }

  activity.fast_q8 =
    Smooth(activity.fast_q8, level_q8, activity_config.fast_alpha_q16);
  activity.slow_q8 =
    Smooth(activity.slow_q8, level_q8, activity_config.slow_alpha_q16);
  previous_ratio = activity.ratio_q8;
  activity.ratio_q8 = (uint32_t)(((uint64_t)activity.fast_q8 << 8U) /
    ((activity.slow_q8 > floor_q8) ? activity.slow_q8 : floor_q8));
  crossed = LevelCrossed(activity.ratio_q8);

  if (activity.armed && ((crossed > 0U) || (activity.pending > 0U)))
  {
    if (crossed > activity.pending)
    {
      activity.pending = crossed;
    }
    ++hold_blocks;
    /* Report once the ratio stops rising, the top level is reached or the
     * hold expires; the first crossing block itself always waits one block
     * unless it already reached the top. */
    if ((activity.pending >= RADIO_ACTIVITY_ONSET_LEVELS) ||
        ((hold_blocks > 1U) && (activity.ratio_q8 <= previous_ratio)) ||
        (hold_blocks >= activity_config.max_hold_blocks))
    {
      onset = (RadioOnset)activity.pending;
      ++activity.onsets[activity.pending - 1U];
      activity.pending = 0U;
      activity.armed = false;
      hold_blocks = 0U;
    }
  }
  else if (!activity.armed &&
           (activity.ratio_q8 < activity_config.rearm_ratio_q8))
  {
    activity.armed = true;
  }
  return onset;
}

bool RadioActivity_GetStatus(RadioActivityStatus *status)
{
  if (status == NULL)
  {
    return false;
  }
  *status = activity;
  return true;
}
