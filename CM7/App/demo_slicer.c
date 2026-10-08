#include "demo_slicer.h"

#include <string.h>

#define LENGTH_STEP_SAMPLES ((SLICER_SOURCE_RATE_HZ / 1000U) * DEMO_SLICER_LENGTH_STEP_MS)

static bool Is(Gesture gesture, GestureKind kind, uint8_t encoder)
{
  return (gesture.kind == (uint8_t)kind) && (gesture.encoder == encoder);
}

static int32_t Clamp(int32_t value, int32_t low, int32_t high)
{
  if (value < low)
  {
    return low;
  }
  return (value > high) ? high : value;
}

void DemoSlicer_Init(DemoSlicer *slicer)
{
  if (slicer == NULL)
  {
    return;
  }
  (void)memset(slicer, 0, sizeof(*slicer));
  slicer->count = (uint8_t)SLICE_MAP_MAX_SLICES;
  slicer->count_choice = slicer->count;
  Slicer_DefaultSetup(&slicer->setup, 0U, slicer->count);
}

void DemoSlicer_Reset(DemoSlicer *slicer)
{
  if (slicer == NULL)
  {
    return;
  }
  slicer->focus = (uint8_t)DEMO_SLICER_BROWSE;
  slicer->count_choice = slicer->count;
}

void DemoSlicer_OnClip(DemoSlicer *slicer, uint32_t clip_samples)
{
  if (slicer == NULL)
  {
    return;
  }
  (void)SliceMap_InitEqual(&slicer->setup.map, clip_samples, slicer->count);
  if (slicer->selected >= slicer->count)
  {
    slicer->selected = 0U;
  }
  DemoSlicer_Reset(slicer);
}

bool DemoSlicer_Modal(const DemoSlicer *slicer)
{
  return (slicer != NULL) && (slicer->focus != (uint8_t)DEMO_SLICER_BROWSE);
}

uint8_t DemoSlicer_NextCount(uint8_t count, int32_t detents)
{
  static const uint8_t counts[] = {4U, 8U, 16U};
  int32_t index = 2;

  while ((index > 0) && (counts[index] > count))
  {
    --index;
  }
  return counts[Clamp(index + detents, 0, 2)];
}

bool DemoSlicer_Edited(const DemoSlicer *slicer)
{
  SlicerSetup equal;
  uint32_t slice;

  if (slicer == NULL)
  {
    return false;
  }
  Slicer_DefaultSetup(&equal, slicer->setup.map.clip_samples, slicer->count);
  if (memcmp(equal.map.starts, slicer->setup.map.starts, sizeof(equal.map.starts)) != 0)
  {
    return true;
  }
  for (slice = 0U; slice < slicer->count; ++slice)
  {
    const SlicerSlice *now = &slicer->setup.slices[slice];
    const SlicerSlice *was = &equal.slices[slice];

    if ((now->pitch_semitones != was->pitch_semitones) ||
        (now->gate_percent != was->gate_percent) || (now->level_percent != was->level_percent) ||
        (slicer->setup.map.enabled[slice] != equal.map.enabled[slice]))
    {
      return true;
    }
  }
  return false;
}

uint32_t DemoSlicer_LengthMs(const DemoSlicer *slicer, uint8_t slice)
{
  if (slicer == NULL)
  {
    return 0U;
  }
  return (SliceMap_Length(&slicer->setup.map, slice) * 1000U) / SLICER_SOURCE_RATE_HZ;
}

void DemoSlicer_ClipPattern(const SliceMap *map, StepPattern *pattern)
{
  uint32_t length;
  uint32_t step;
  uint8_t previous = SLICER_NO_SLICE;

  if ((map == NULL) || (pattern == NULL) || (map->count == 0U) || (map->clip_samples == 0U))
  {
    return;
  }
  length = ((pattern->length == 0U) || (pattern->length > STEP_PATTERN_MAX_STEPS))
             ? STEP_PATTERN_MAX_STEPS
             : pattern->length;
  for (step = 0U; step < length; ++step)
  {
    const uint32_t at = (uint32_t)(((uint64_t)map->clip_samples * step) / length);
    const uint8_t slice = SliceMap_SliceAt(map, at);

    pattern->steps[step].value = slice;
    pattern->steps[step].on = slice != previous;
    previous = slice;
  }
}

/* Equal slices at the chosen count with default parameters (0022 item 9). */
static void ApplyCount(DemoSlicer *slicer, DemoSlicerAction *action)
{
  action->old_map = slicer->setup.map;
  slicer->count = slicer->count_choice;
  Slicer_DefaultSetup(&slicer->setup, action->old_map.clip_samples, slicer->count);
  slicer->selected = SliceMap_Remap(&action->old_map, slicer->selected, &slicer->setup.map);
  action->count_changed = true;
  action->setup_changed = true;
  slicer->focus = (uint8_t)DEMO_SLICER_BROWSE;
}

static void Browse(DemoSlicer *slicer, Gesture gesture, bool running, DemoSlicerAction *action)
{
  if (Is(gesture, GESTURE_TURN, 0U))
  {
    const uint8_t before = slicer->selected;

    slicer->selected = (uint8_t)Clamp((int32_t)slicer->selected + gesture.detents, 0,
                                      (int32_t)slicer->count - 1);
    if (!running && (slicer->selected != before))
    {
      action->audition = true;
      action->audition_slice = slicer->selected;
    }
  }
  else if (Is(gesture, GESTURE_HOLD, 0U))
  {
    slicer->focus = (uint8_t)DEMO_SLICER_SLICE;
  }
  else if (Is(gesture, GESTURE_TURN, 1U))
  {
    slicer->count_choice = DemoSlicer_NextCount(slicer->count_choice, gesture.detents);
  }
  else if (Is(gesture, GESTURE_CLICK, 1U) && (slicer->count_choice != slicer->count))
  {
    if (DemoSlicer_Edited(slicer))
    {
      slicer->focus = (uint8_t)DEMO_SLICER_CONFIRM;
    }
    else
    {
      ApplyCount(slicer, action);
    }
  }
}

static void Slice(DemoSlicer *slicer, Gesture gesture, DemoSlicerAction *action)
{
  SlicerSlice *params = &slicer->setup.slices[slicer->selected % SLICE_MAP_MAX_SLICES];
  const SlicerSlice before = *params;

  if (gesture.kind == (uint8_t)GESTURE_TURN)
  {
    switch (gesture.encoder)
    {
      case 0U:
        params->pitch_semitones = (int8_t)Clamp((int32_t)params->pitch_semitones + gesture.detents,
                                                SLICER_PITCH_MIN, SLICER_PITCH_MAX);
        break;
      case 1U:
        params->gate_percent = (uint8_t)Clamp((int32_t)params->gate_percent +
                                                (gesture.detents * (int32_t)DEMO_SLICER_GATE_STEP),
                                              (int32_t)SLICER_GATE_MIN, 100);
        break;
      case 2U:
        params->level_percent = (uint8_t)Clamp((int32_t)params->level_percent +
                                                 (gesture.detents *
                                                  (int32_t)DEMO_SLICER_LEVEL_STEP), 0, 100);
        break;
      case 3U:
      {
        const int64_t length = (int64_t)SliceMap_Length(&slicer->setup.map, slicer->selected) +
                               ((int64_t)gesture.detents * LENGTH_STEP_SAMPLES);

        if (SliceMap_SetLength(&slicer->setup.map, slicer->selected,
                               (length < 0) ? 0U : (uint32_t)length, SLICER_MIN_SLICE_SAMPLES))
        {
          action->setup_changed = true;
        }
        break;
      }
      default:
        break;
    }
    if ((params->pitch_semitones != before.pitch_semitones) ||
        (params->gate_percent != before.gate_percent) ||
        (params->level_percent != before.level_percent))
    {
      action->setup_changed = true;
    }
  }
  else if (Is(gesture, GESTURE_CLICK, 0U))
  {
    slicer->focus = (uint8_t)DEMO_SLICER_BROWSE;
  }
  else if (Is(gesture, GESTURE_CLICK, 2U))
  {
    action->toggle_run = true;
  }
}

bool DemoSlicer_OnGesture(DemoSlicer *slicer, Gesture gesture, bool running,
                          DemoSlicerAction *action)
{
  if ((slicer == NULL) || (action == NULL))
  {
    return false;
  }
  (void)memset(action, 0, sizeof(*action));
  switch (slicer->focus)
  {
    case DEMO_SLICER_SLICE:
      Slice(slicer, gesture, action);
      return true;
    case DEMO_SLICER_CONFIRM:
      if (Is(gesture, GESTURE_CLICK, 1U))
      {
        ApplyCount(slicer, action);
      }
      else
      {
        slicer->count_choice = slicer->count; /* cancelled */
        slicer->focus = (uint8_t)DEMO_SLICER_BROWSE;
      }
      return true;
    case DEMO_SLICER_BROWSE:
    default:
      slicer->focus = (uint8_t)DEMO_SLICER_BROWSE;
      if (Is(gesture, GESTURE_CLICK, 0U) || Is(gesture, GESTURE_HOLD, 1U) ||
          Is(gesture, GESTURE_HOLD, 2U))
      {
        return false; /* the sequencer shell's */
      }
      if ((gesture.kind == (uint8_t)GESTURE_TURN) || (gesture.kind == (uint8_t)GESTURE_CLICK) ||
          (gesture.kind == (uint8_t)GESTURE_HOLD))
      {
        Browse(slicer, gesture, running, action);
        return true; /* encoder gestures without a binding are swallowed */
      }
      return false;
  }
}
