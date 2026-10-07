#include "demo_instrument.h"

#include <stdio.h>

/* Grain sizes and densities step through these values, roughly evenly on a
 * logarithmic scale, one detent each. */
static const uint16_t sizes_ms[] = {
  10U, 12U, 15U, 20U, 25U, 30U, 40U, 50U, 60U, 80U,
  100U, 120U, 150U, 200U, 250U, 300U, 400U, 500U
};
static const uint16_t densities[] = {
  1U, 2U, 3U, 4U, 5U, 6U, 8U, 10U, 12U, 15U,
  20U, 25U, 30U, 40U, 50U, 60U, 80U, 100U
};

static const DemoParam pages[DEMO_INSTRUMENT_PAGES][DEMO_INSTRUMENT_ENCODERS] = {
  {DEMO_PARAM_POSITION, DEMO_PARAM_SIZE, DEMO_PARAM_DENSITY, DEMO_PARAM_PITCH},
  {DEMO_PARAM_SPRAY, DEMO_PARAM_COUNT, DEMO_PARAM_ENVELOPE, DEMO_PARAM_LEVEL}
};

_Static_assert(sizeof(sizes_ms) / sizeof(sizes_ms[0]) == 18U, "size steps");
_Static_assert(sizeof(densities) / sizeof(densities[0]) == 18U, "density steps");
/* demo_instrument_test checks that the steps span the engine's ranges. */

void DemoInstrument_Init(DemoInstrument *instrument)
{
  if (instrument == NULL)
  {
    return;
  }
  instrument->page = 0U;
  Granular_DefaultParams(&instrument->params);
}

/* A linear value moved by detents times step, held within low..high. */
static int32_t Linear(int32_t value, int32_t detents, int32_t step, int32_t low, int32_t high)
{
  int64_t next = (int64_t)value + ((int64_t)detents * step);

  if (next < low)
  {
    next = low;
  }
  if (next > high)
  {
    next = high;
  }
  return (int32_t)next;
}

/* A value from a table, moved by detents from the nearest entry at or above it. */
static uint32_t Stepped(uint32_t value, int32_t detents, const uint16_t *table, uint32_t count)
{
  uint32_t index = 0U;
  int64_t next;

  while ((index + 1U < count) && (table[index] < value))
  {
    ++index;
  }
  next = (int64_t)index + detents;
  if (next < 0)
  {
    next = 0;
  }
  if (next >= (int64_t)count)
  {
    next = (int64_t)count - 1;
  }
  return table[next];
}

bool DemoInstrument_Turn(DemoInstrument *instrument, uint8_t encoder, int32_t detents)
{
  GranularParams *params;
  GranularParams before;
  DemoParam param;

  if ((instrument == NULL) || (detents == 0))
  {
    return false;
  }
  param = DemoInstrument_Param(instrument->page, encoder);
  params = &instrument->params;
  before = *params;
  switch (param)
  {
    case DEMO_PARAM_POSITION:
      params->position_permille = (uint16_t)Linear(params->position_permille, detents, 10, 0, 1000);
      break;
    case DEMO_PARAM_SIZE:
      params->size_ms = (uint16_t)Stepped(params->size_ms, detents, sizes_ms,
                                          sizeof(sizes_ms) / sizeof(sizes_ms[0]));
      break;
    case DEMO_PARAM_DENSITY:
      params->density = (uint16_t)Stepped(params->density, detents, densities,
                                          sizeof(densities) / sizeof(densities[0]));
      break;
    case DEMO_PARAM_PITCH:
      params->pitch_semitones = (int8_t)Linear(params->pitch_semitones, detents, 1,
                                               GRANULAR_PITCH_MIN, GRANULAR_PITCH_MAX);
      break;
    case DEMO_PARAM_SPRAY:
      params->spray_permille = (uint16_t)Linear(params->spray_permille, detents, 10, 0, 1000);
      break;
    case DEMO_PARAM_ENVELOPE:
      params->envelope_percent = (uint8_t)Linear(params->envelope_percent, detents, 5, 0, 100);
      break;
    case DEMO_PARAM_LEVEL:
      params->level_percent = (uint8_t)Linear(params->level_percent, detents, 5, 0, 100);
      break;
    case DEMO_PARAM_COUNT:
    default:
      return false;
  }
  return (params->position_permille != before.position_permille) ||
         (params->size_ms != before.size_ms) || (params->density != before.density) ||
         (params->pitch_semitones != before.pitch_semitones) ||
         (params->spray_permille != before.spray_permille) ||
         (params->envelope_percent != before.envelope_percent) ||
         (params->level_percent != before.level_percent);
}

void DemoInstrument_NextPage(DemoInstrument *instrument)
{
  if (instrument != NULL)
  {
    instrument->page = (uint8_t)((instrument->page + 1U) % DEMO_INSTRUMENT_PAGES);
  }
}

DemoParam DemoInstrument_Param(uint8_t page, uint8_t encoder)
{
  if ((page >= DEMO_INSTRUMENT_PAGES) || (encoder >= DEMO_INSTRUMENT_ENCODERS))
  {
    return DEMO_PARAM_COUNT;
  }
  return pages[page][encoder];
}

const char *DemoInstrument_Name(DemoParam param)
{
  static const char *const names[DEMO_PARAM_COUNT] = {
    "POS", "SIZE", "DENS", "PITCH", "SPRAY", "ENV", "LEVEL"
  };

  if (param == DEMO_PARAM_COUNT)
  {
    return "-";
  }
  return ((uint32_t)param < DEMO_PARAM_COUNT) ? names[param] : "?";
}

void DemoInstrument_FormatValue(const GranularParams *params, DemoParam param, char *text,
                                size_t size)
{
  if ((text == NULL) || (size == 0U))
  {
    return;
  }
  if (params == NULL)
  {
    text[0] = '\0';
    return;
  }
  switch (param)
  {
    case DEMO_PARAM_POSITION:
      (void)snprintf(text, size, "%u%%", (unsigned)(params->position_permille / 10U));
      break;
    case DEMO_PARAM_SIZE:
      (void)snprintf(text, size, "%uMS", (unsigned)params->size_ms);
      break;
    case DEMO_PARAM_DENSITY:
      (void)snprintf(text, size, "%u/S", (unsigned)params->density);
      break;
    case DEMO_PARAM_PITCH:
      (void)snprintf(text, size, "%+dST", (int)params->pitch_semitones);
      break;
    case DEMO_PARAM_SPRAY:
      (void)snprintf(text, size, "%u%%", (unsigned)(params->spray_permille / 10U));
      break;
    case DEMO_PARAM_ENVELOPE:
      (void)snprintf(text, size, "%u%%", (unsigned)params->envelope_percent);
      break;
    case DEMO_PARAM_LEVEL:
      (void)snprintf(text, size, "%u%%", (unsigned)params->level_percent);
      break;
    case DEMO_PARAM_COUNT:
      text[0] = '\0';
      break;
    default:
      (void)snprintf(text, size, "?");
      break;
  }
}
