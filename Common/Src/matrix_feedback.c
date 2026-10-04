#include "matrix_feedback.h"

#include <stddef.h>
#include <string.h>

#define MATRIX_LOOP_STEPS_TRAIL 7U
#define MATRIX_LOOP_STEPS_PLAIN 6U

static MatrixFeedbackConfig feedback_config;
static bool trail_enabled;
static uint8_t step;
static uint32_t next_step_ms;
static bool input_known;
static uint8_t input_bucket;
static bool latched_known;
static uint8_t latched_bucket;
static uint8_t kick_px;
static int8_t kick_direction;
static uint32_t next_decay_ms;
static bool recording;
static bool fault_active;
static uint32_t fault_start_ms;
static bool fault_lit;
static uint32_t dropped_steps;
static bool dirty;

static bool Reached(uint32_t now_ms, uint32_t deadline_ms)
{
  return (int32_t)(now_ms - deadline_ms) >= 0;
}

static int32_t Distance(uint8_t a, uint8_t b)
{
  return (a > b) ? (int32_t)(a - b) : (int32_t)(b - a);
}

static uint8_t LoopSteps(void)
{
  return trail_enabled ? MATRIX_LOOP_STEPS_TRAIL : MATRIX_LOOP_STEPS_PLAIN;
}

static uint32_t StepPeriod(void)
{
  return feedback_config.emf_step_ms[latched_known ? latched_bucket : 0U];
}

static void LatchEmf(void)
{
  latched_known = input_known;
  latched_bucket = input_known ? input_bucket : 0U;
}

static MatrixBorder Border(void)
{
  if (fault_active)
  {
    return fault_lit ? MATRIX_BORDER_FAULT_ON : MATRIX_BORDER_FAULT_OFF;
  }
  if (recording)
  {
    return (kick_px > 0U) ? MATRIX_BORDER_RECORDING_KICK
                          : MATRIX_BORDER_RECORDING;
  }
  return MATRIX_BORDER_NONE;
}

static MatrixFeedbackRgb Scale(MatrixFeedbackRgb colour, uint8_t percent)
{
  MatrixFeedbackRgb scaled;

  scaled.red = (uint8_t)(((uint16_t)colour.red * percent) / 100U);
  scaled.green = (uint8_t)(((uint16_t)colour.green * percent) / 100U);
  scaled.blue = (uint8_t)(((uint16_t)colour.blue * percent) / 100U);
  return scaled;
}

void MatrixFeedback_DefaultConfig(MatrixFeedbackConfig *config)
{
  static const MatrixFeedbackRgb emf_colours[MATRIX_FEEDBACK_EMF_BUCKETS] = {
    {0U, 175U, 230U},   /* cyan */
    {0U, 255U, 0U},     /* green */
    {255U, 165U, 0U},   /* amber */
    {255U, 30U, 20U}    /* red */
  };
  static const uint16_t emf_steps[MATRIX_FEEDBACK_EMF_BUCKETS] =
    {160U, 125U, 95U, 70U};
  uint32_t index;

  if (config == NULL)
  {
    return;
  }
  for (index = 0U; index < MATRIX_FEEDBACK_EMF_BUCKETS; ++index)
  {
    config->emf_colour[index] = emf_colours[index];
    config->emf_step_ms[index] = emf_steps[index];
  }
  /* Dim grey as it looks on the IS31FL3741: red reads strongest, so it is
   * driven lowest (bench, 2026-10-03). Too faint for a visible trail. */
  config->unknown_colour.red = 1U;
  config->unknown_colour.green = 2U;
  config->unknown_colour.blue = 2U;
  /* Yellow as it looks on the IS31FL3741, clearly apart from the amber
   * bucket (bench, 2026-10-03). */
  config->recording_colour.red = 120U;
  config->recording_colour.green = 240U;
  config->recording_colour.blue = 10U;
  config->fault_colour.red = 255U;
  config->fault_colour.green = 30U;
  config->fault_colour.blue = 20U;
  config->trail_percent = 35U;
  config->kick_px[0] = 1U;
  config->kick_px[1] = 2U;
  config->kick_px[2] = 3U;
  config->kick_decay_ms = 80U;
  config->fault_blink_ms = 250U;
  config->fault_blinks = 3U;
}

bool MatrixFeedback_Init(const MatrixFeedbackConfig *config, uint32_t now_ms)
{
  uint32_t index;

  if ((config == NULL) || (config->trail_percent > 100U) ||
      (config->kick_decay_ms == 0U) || (config->fault_blink_ms == 0U) ||
      (config->fault_blinks == 0U))
  {
    return false;
  }
  for (index = 0U; index < MATRIX_FEEDBACK_EMF_BUCKETS; ++index)
  {
    if (config->emf_step_ms[index] == 0U)
    {
      return false;
    }
  }
  for (index = 0U; index < MATRIX_FEEDBACK_ONSET_SIZES; ++index)
  {
    if ((config->kick_px[index] == 0U) ||
        (config->kick_px[index] >= MATRIX_FEEDBACK_SIZE) ||
        ((index > 0U) && (config->kick_px[index] < config->kick_px[index - 1U])))
    {
      return false;
    }
  }
  feedback_config = *config;
  trail_enabled = true;
  step = 0U;
  input_known = false;
  input_bucket = 0U;
  LatchEmf();
  next_step_ms = now_ms + StepPeriod();
  kick_px = 0U;
  kick_direction = -1;
  next_decay_ms = now_ms;
  recording = false;
  fault_active = false;
  fault_start_ms = now_ms;
  fault_lit = false;
  dropped_steps = 0U;
  dirty = true;
  return true;
}

void MatrixFeedback_SetTrail(bool enabled)
{
  if (enabled == trail_enabled)
  {
    return;
  }
  trail_enabled = enabled;
  step = 0U;
  LatchEmf();
  dirty = true;
}

void MatrixFeedback_SetEmf(bool known, uint8_t bucket)
{
  input_known = known;
  input_bucket = (bucket < MATRIX_FEEDBACK_EMF_BUCKETS)
    ? bucket : (uint8_t)(MATRIX_FEEDBACK_EMF_BUCKETS - 1U);
  if (!known && latched_known)
  {
    /* Never keep showing a colour the device is no longer measuring. */
    latched_known = false;
    latched_bucket = 0U;
    dirty = true;
  }
}

void MatrixFeedback_OnOnset(uint8_t size, uint32_t now_ms)
{
  uint8_t px;

  if ((size == 0U) || (size > MATRIX_FEEDBACK_ONSET_SIZES))
  {
    return;
  }
  px = feedback_config.kick_px[size - 1U];
  if (px < kick_px)
  {
    return;
  }
  kick_direction = (int8_t)-kick_direction;
  kick_px = px;
  next_decay_ms = now_ms + feedback_config.kick_decay_ms;
  dirty = true;
}

void MatrixFeedback_SetRecording(bool active)
{
  if (active == recording)
  {
    return;
  }
  recording = active;
  if (active)
  {
    fault_active = false;
  }
  dirty = true;
}

void MatrixFeedback_OnRecordingFault(uint32_t now_ms)
{
  recording = false;
  fault_active = true;
  fault_lit = true;
  fault_start_ms = now_ms;
  dirty = true;
}

bool MatrixFeedback_Advance(uint32_t now_ms)
{
  bool changed = dirty;
  uint8_t advanced = 0U;

  while (Reached(now_ms, next_step_ms) && (advanced < LoopSteps()))
  {
    step = (uint8_t)((step + 1U) % LoopSteps());
    if (step == 0U)
    {
      LatchEmf();
    }
    next_step_ms += StepPeriod();
    ++advanced;
  }
  if (Reached(now_ms, next_step_ms))
  {
    /* More than a whole loop behind: resynchronise instead of spinning. */
    next_step_ms = now_ms + StepPeriod();
  }
  if (advanced > 1U)
  {
    dropped_steps += advanced - 1U;
  }
  changed = changed || (advanced > 0U);

  while ((kick_px > 0U) && Reached(now_ms, next_decay_ms))
  {
    --kick_px;
    next_decay_ms += feedback_config.kick_decay_ms;
    changed = true;
  }

  if (fault_active)
  {
    const uint32_t elapsed = now_ms - fault_start_ms;
    const uint32_t blink_ms = feedback_config.fault_blink_ms;
    bool lit;

    if (elapsed >= (2U * blink_ms * feedback_config.fault_blinks) - blink_ms)
    {
      /* The last blink's off phase is the border turning off for good. */
      fault_active = false;
      changed = true;
    }
    else
    {
      lit = ((elapsed / blink_ms) % 2U) == 0U;
      if (lit != fault_lit)
      {
        fault_lit = lit;
        changed = true;
      }
    }
  }
  dirty = false;
  return changed;
}

bool MatrixFeedback_Compose(MatrixFeedbackFrame *frame)
{
  const MatrixFeedbackRgb colour = latched_known
    ? feedback_config.emf_colour[latched_bucket]
    : feedback_config.unknown_colour;
  const MatrixFeedbackRgb trail =
    Scale(colour, feedback_config.trail_percent);
  const MatrixBorder border = Border();
  uint8_t x;
  uint8_t y;

  if (frame == NULL)
  {
    return false;
  }
  (void)memset(frame, 0, sizeof(*frame));
  for (y = 0U; y < MATRIX_FEEDBACK_SIZE; ++y)
  {
    const int32_t dy = Distance(y, MATRIX_FEEDBACK_CENTRE);
    /* Even rows move with the kick direction, odd rows against it. */
    const int32_t shift = (((y & 1U) == 0U) ? kick_direction : -kick_direction) *
                          (int32_t)kick_px;

    for (x = 0U; x < MATRIX_FEEDBACK_SIZE; ++x)
    {
      const int32_t dx = Distance(x, MATRIX_FEEDBACK_CENTRE);
      const int32_t radius = (dx > dy) ? dx : dy; /* Chebyshev ring */
      const int32_t target = (int32_t)x + shift;

      if ((target < 0) || (target >= (int32_t)MATRIX_FEEDBACK_SIZE))
      {
        continue; /* clipped at the edge */
      }
      if ((radius == (int32_t)step) || (radius == (int32_t)step - 1))
      {
        frame->pixels[y][target] = colour;
      }
      else if (trail_enabled && (radius == (int32_t)step - 2))
      {
        frame->pixels[y][target] = trail;
      }
    }
  }
  if (border != MATRIX_BORDER_NONE)
  {
    MatrixFeedbackRgb ring = {0U, 0U, 0U};

    if (border == MATRIX_BORDER_RECORDING)
    {
      ring = feedback_config.recording_colour;
    }
    else if (border == MATRIX_BORDER_FAULT_ON)
    {
      ring = feedback_config.fault_colour;
    }
    for (x = 0U; x < MATRIX_FEEDBACK_SIZE; ++x)
    {
      frame->pixels[0][x] = ring;
      frame->pixels[MATRIX_FEEDBACK_SIZE - 1U][x] = ring;
      frame->pixels[x][0] = ring;
      frame->pixels[x][MATRIX_FEEDBACK_SIZE - 1U] = ring;
    }
  }
  return true;
}

bool MatrixFeedback_GetStatus(MatrixFeedbackStatus *status)
{
  if (status == NULL)
  {
    return false;
  }
  status->step = step;
  status->loop_steps = LoopSteps();
  status->trail = trail_enabled;
  status->emf_known = latched_known;
  status->emf_bucket = latched_bucket;
  status->kick_px = kick_px;
  status->kick_direction = kick_direction;
  status->border = Border();
  status->dropped_steps = dropped_steps;
  return true;
}
