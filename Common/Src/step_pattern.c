#include "step_pattern.h"

#include <stddef.h>

void StepPattern_InitSweep(StepPattern *pattern, uint16_t max_value)
{
  uint32_t index;

  if (pattern == NULL)
  {
    return;
  }
  for (index = 0U; index < STEP_PATTERN_MAX_STEPS; ++index)
  {
    pattern->steps[index].value = (uint16_t)((index * max_value) / STEP_PATTERN_MAX_STEPS);
    pattern->steps[index].on = true;
  }
  pattern->length = (uint8_t)STEP_PATTERN_MAX_STEPS;
  pattern->steps_per_beat = 4U;
}

uint32_t StepPattern_Playhead(const StepPattern *pattern, const Transport *transport)
{
  uint8_t length;

  if (pattern == NULL)
  {
    return 0U;
  }
  length = pattern->length;
  if ((length == 0U) || (length > STEP_PATTERN_MAX_STEPS))
  {
    length = (uint8_t)STEP_PATTERN_MAX_STEPS;
  }
  return Transport_Step(transport, pattern->steps_per_beat) % length;
}

uint16_t StepPattern_Offset(uint16_t value, uint16_t knob, uint16_t centre, uint16_t max_value)
{
  const int32_t offset = (int32_t)value + (int32_t)knob - (int32_t)centre;

  if (offset < 0)
  {
    return 0U;
  }
  return (offset > (int32_t)max_value) ? max_value : (uint16_t)offset;
}

uint8_t StepPattern_NextDivision(uint8_t steps_per_beat, int32_t detents)
{
  static const uint8_t divisions[] = {4U, 2U, 1U};
  int32_t index = 0;
  int32_t next;

  while ((index < 2) && (divisions[index] != steps_per_beat))
  {
    ++index;
  }
  next = index + detents;
  if (next < 0)
  {
    next = 0;
  }
  if (next > 2)
  {
    next = 2;
  }
  return divisions[next];
}
