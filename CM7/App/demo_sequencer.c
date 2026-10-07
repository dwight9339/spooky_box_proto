#include "demo_sequencer.h"

#include <stddef.h>

#include "transport.h"

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

void DemoSequencer_Init(DemoSequencer *seq)
{
  if (seq == NULL)
  {
    return;
  }
  seq->step = 0U;
  seq->setting = (uint8_t)DEMO_SEQ_SETTING_TEMPO;
  seq->edit_backup = 0U;
  seq->last_input_ms = 0U;
  DemoSequencer_Reset(seq);
}

void DemoSequencer_Reset(DemoSequencer *seq)
{
  if (seq == NULL)
  {
    return;
  }
  seq->view = (uint8_t)DEMO_SEQ_VIEW_ENGINE;
  seq->focus = (uint8_t)DEMO_SEQ_FOCUS_STEPS;
  seq->item = (uint8_t)DEMO_SEQ_ITEM_ENGINE;
  seq->menu_return = (uint8_t)DEMO_SEQ_VIEW_ENGINE;
}

static void OpenMenu(DemoSequencer *seq, uint32_t now_ms)
{
  const bool from_steps = seq->view == (uint8_t)DEMO_SEQ_VIEW_STEPS;

  seq->menu_return = seq->view;
  seq->item = from_steps ? (uint8_t)DEMO_SEQ_ITEM_SEQUENCER : (uint8_t)DEMO_SEQ_ITEM_ENGINE;
  seq->view = (uint8_t)DEMO_SEQ_VIEW_MENU;
  seq->last_input_ms = now_ms;
}

static void Menu(DemoSequencer *seq, Gesture gesture)
{
  if (Is(gesture, GESTURE_TURN, 0U))
  {
    seq->item = (uint8_t)Clamp((int32_t)seq->item + gesture.detents, 0,
                               (int32_t)DEMO_SEQ_ITEM_COUNT - 1);
  }
  else if (Is(gesture, GESTURE_CLICK, 0U))
  {
    if (seq->item == (uint8_t)DEMO_SEQ_ITEM_SEQUENCER)
    {
      if (seq->menu_return != (uint8_t)DEMO_SEQ_VIEW_STEPS)
      {
        seq->focus = (uint8_t)DEMO_SEQ_FOCUS_STEPS;
      }
      seq->view = (uint8_t)DEMO_SEQ_VIEW_STEPS;
    }
    else
    {
      seq->view = (uint8_t)DEMO_SEQ_VIEW_ENGINE;
    }
  }
  else if (Is(gesture, GESTURE_CLICK, 1U))
  {
    seq->view = seq->menu_return;
  }
}

static uint32_t SettingValue(const DemoSequencer *seq, const DemoSeqTarget *target)
{
  return (seq->setting == (uint8_t)DEMO_SEQ_SETTING_TEMPO) ? target->tempo_x100
                                                           : target->pattern->steps_per_beat;
}

static void SetSetting(const DemoSequencer *seq, DemoSeqTarget *target, uint32_t value)
{
  if (seq->setting == (uint8_t)DEMO_SEQ_SETTING_TEMPO)
  {
    target->tempo_x100 = value;
    target->tempo_changed = true;
  }
  else
  {
    target->pattern->steps_per_beat = (uint8_t)value;
  }
}

static void TurnSetting(const DemoSequencer *seq, DemoSeqTarget *target, int32_t detents)
{
  if (seq->setting == (uint8_t)DEMO_SEQ_SETTING_TEMPO)
  {
    SetSetting(seq, target,
               (uint32_t)Clamp((int32_t)target->tempo_x100 +
                                 (detents * (int32_t)DEMO_SEQ_TEMPO_STEP_X100),
                               (int32_t)TRANSPORT_BPM_MIN_X100, (int32_t)TRANSPORT_BPM_MAX_X100));
  }
  else
  {
    SetSetting(seq, target, StepPattern_NextDivision(target->pattern->steps_per_beat, detents));
  }
}

static void Steps(DemoSequencer *seq, Gesture gesture, DemoSeqTarget *target)
{
  StepPattern *pattern = target->pattern;
  PatternStep *step = &pattern->steps[seq->step % STEP_PATTERN_MAX_STEPS];

  switch (seq->focus)
  {
    case DEMO_SEQ_FOCUS_STEPS:
      if (Is(gesture, GESTURE_TURN, 0U))
      {
        seq->step = (uint8_t)Clamp((int32_t)seq->step + gesture.detents, 0,
                                   (int32_t)pattern->length - 1);
      }
      else if (Is(gesture, GESTURE_CLICK, 0U))
      {
        seq->focus = (uint8_t)DEMO_SEQ_FOCUS_STEP_EDIT;
      }
      else if (Is(gesture, GESTURE_HOLD, 0U))
      {
        seq->focus = (uint8_t)DEMO_SEQ_FOCUS_SETTINGS;
      }
      break;
    case DEMO_SEQ_FOCUS_STEP_EDIT:
      if (Is(gesture, GESTURE_TURN, 0U))
      {
        step->value = (uint16_t)Clamp((int32_t)step->value +
                                        (gesture.detents * (int32_t)DEMO_SEQ_VALUE_STEP),
                                      0, (int32_t)DEMO_SEQ_VALUE_MAX);
      }
      else if (Is(gesture, GESTURE_CLICK, 1U))
      {
        step->on = !step->on;
      }
      else if (Is(gesture, GESTURE_CLICK, 0U))
      {
        seq->focus = (uint8_t)DEMO_SEQ_FOCUS_STEPS;
      }
      break;
    case DEMO_SEQ_FOCUS_SETTINGS:
      if (Is(gesture, GESTURE_TURN, 0U))
      {
        seq->setting = (uint8_t)Clamp((int32_t)seq->setting + gesture.detents, 0,
                                      (int32_t)DEMO_SEQ_SETTING_COUNT - 1);
      }
      else if (Is(gesture, GESTURE_CLICK, 0U))
      {
        seq->edit_backup = SettingValue(seq, target);
        seq->focus = (uint8_t)DEMO_SEQ_FOCUS_SETTING_EDIT;
      }
      else if (Is(gesture, GESTURE_HOLD, 0U))
      {
        seq->focus = (uint8_t)DEMO_SEQ_FOCUS_STEPS;
      }
      break;
    case DEMO_SEQ_FOCUS_SETTING_EDIT:
      if (Is(gesture, GESTURE_TURN, 0U))
      {
        TurnSetting(seq, target, gesture.detents);
      }
      else if (Is(gesture, GESTURE_CLICK, 0U))
      {
        seq->focus = (uint8_t)DEMO_SEQ_FOCUS_SETTINGS;
      }
      else if (Is(gesture, GESTURE_CLICK, 1U))
      {
        if (SettingValue(seq, target) != seq->edit_backup)
        {
          SetSetting(seq, target, seq->edit_backup);
        }
        seq->focus = (uint8_t)DEMO_SEQ_FOCUS_SETTINGS;
      }
      break;
    default:
      seq->focus = (uint8_t)DEMO_SEQ_FOCUS_STEPS;
      break;
  }
}

bool DemoSequencer_OnGesture(DemoSequencer *seq, Gesture gesture, DemoSeqTarget *target,
                             uint32_t now_ms)
{
  if ((seq == NULL) || (target == NULL) || (target->pattern == NULL))
  {
    return false;
  }
  switch (seq->view)
  {
    case DEMO_SEQ_VIEW_MENU:
      seq->last_input_ms = now_ms;
      Menu(seq, gesture);
      return true;
    case DEMO_SEQ_VIEW_STEPS:
      if (Is(gesture, GESTURE_CLICK, 2U))
      {
        target->toggle_run = true;
      }
      else if (Is(gesture, GESTURE_CLICK, 3U) || Is(gesture, GESTURE_HOLD, 2U))
      {
        if (seq->focus == (uint8_t)DEMO_SEQ_FOCUS_SETTING_EDIT)
        {
          seq->focus = (uint8_t)DEMO_SEQ_FOCUS_SETTINGS; /* the edit is kept */
        }
        OpenMenu(seq, now_ms);
      }
      else
      {
        Steps(seq, gesture, target);
      }
      return true;
    case DEMO_SEQ_VIEW_ENGINE:
    default:
      if (Is(gesture, GESTURE_CLICK, 0U))
      {
        target->toggle_run = true;
        return true;
      }
      if (Is(gesture, GESTURE_HOLD, 2U))
      {
        OpenMenu(seq, now_ms);
        return true;
      }
      return false;
  }
}

bool DemoSequencer_Tick(DemoSequencer *seq, uint32_t now_ms)
{
  if ((seq == NULL) || (seq->view != (uint8_t)DEMO_SEQ_VIEW_MENU) ||
      ((now_ms - seq->last_input_ms) < DEMO_SEQ_MENU_TIMEOUT_MS))
  {
    return false;
  }
  seq->view = seq->menu_return;
  return true;
}

const char *DemoSequencer_ItemName(uint8_t item)
{
  static const char *const names[DEMO_SEQ_ITEM_COUNT] = {"ENGINE", "SEQUENCER"};

  return (item < (uint8_t)DEMO_SEQ_ITEM_COUNT) ? names[item] : "?";
}

const char *DemoSequencer_SettingName(uint8_t setting)
{
  static const char *const names[DEMO_SEQ_SETTING_COUNT] = {"TEMPO", "DIV"};

  return (setting < (uint8_t)DEMO_SEQ_SETTING_COUNT) ? names[setting] : "?";
}

const char *DemoSequencer_DivisionName(uint8_t steps_per_beat)
{
  return (steps_per_beat == 4U) ? "1/16" : (steps_per_beat == 2U) ? "1/8"
         : (steps_per_beat == 1U) ? "1/4" : "?";
}
