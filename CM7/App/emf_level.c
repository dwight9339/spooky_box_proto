#include "emf_level.h"

#include <stddef.h>

static EmfLevelConfig level_config;
static bool have_sample;
static bool sensor_fault;
static bool baseline_valid;
static uint32_t sample_uT;
static uint32_t sample_ms;

void EmfLevel_DefaultConfig(EmfLevelConfig *config)
{
  if (config == NULL)
  {
    return;
  }
  config->boundary_uT[0] = 150U;
  config->boundary_uT[1] = 400U;
  config->boundary_uT[2] = 1200U;
  config->stale_ms = 500U;
}

bool EmfLevel_Init(const EmfLevelConfig *config)
{
  uint32_t index;

  if ((config == NULL) || (config->stale_ms == 0U) ||
      (config->boundary_uT[0] == 0U))
  {
    return false;
  }
  for (index = 1U; index < EMF_LEVEL_BOUNDARY_COUNT; ++index)
  {
    if (config->boundary_uT[index] <= config->boundary_uT[index - 1U])
    {
      return false;
    }
  }
  level_config = *config;
  have_sample = false;
  sensor_fault = false;
  baseline_valid = false;
  sample_uT = 0U;
  sample_ms = 0U;
  return true;
}

void EmfLevel_OnSample(uint32_t now_ms, uint32_t emf_uT, bool valid)
{
  have_sample = true;
  sensor_fault = false;
  baseline_valid = valid;
  sample_uT = emf_uT;
  sample_ms = now_ms;
}

void EmfLevel_OnSensorFault(void)
{
  sensor_fault = true;
}

uint8_t EmfLevel_Bucket(const EmfLevelConfig *config, uint32_t emf_uT)
{
  uint8_t bucket = 0U;

  if (config == NULL)
  {
    return 0U;
  }
  while ((bucket < EMF_LEVEL_BOUNDARY_COUNT) &&
         (emf_uT >= config->boundary_uT[bucket]))
  {
    ++bucket;
  }
  return bucket;
}

bool EmfLevel_Get(uint32_t now_ms, EmfLevelReading *reading)
{
  if (reading == NULL)
  {
    return false;
  }
  reading->bucket = 0U;
  reading->emf_uT = sample_uT;
  reading->age_ms = have_sample ? (now_ms - sample_ms) : 0U;
  if (sensor_fault)
  {
    reading->state = EMF_LEVEL_SENSOR_FAULT;
  }
  else if (!have_sample)
  {
    reading->state = EMF_LEVEL_NO_SAMPLE;
  }
  else if (reading->age_ms > level_config.stale_ms)
  {
    reading->state = EMF_LEVEL_STALE;
  }
  else if (!baseline_valid)
  {
    reading->state = EMF_LEVEL_NO_BASELINE;
  }
  else
  {
    reading->state = EMF_LEVEL_VALID;
    reading->bucket = EmfLevel_Bucket(&level_config, sample_uT);
  }
  return true;
}
