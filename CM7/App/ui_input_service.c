#include "ui_input_service.h"

#include <stddef.h>
#include <string.h>

typedef struct
{
  bool stable;
  bool candidate;
  uint32_t candidate_since_ms;
} UiSwitchState;

typedef struct
{
  volatile uint8_t previous_ab;
  volatile int8_t transition_accumulator;
  volatile int32_t count;
  volatile uint32_t invalid_transitions;
  volatile uint32_t pending_cw;
  volatile uint32_t pending_ccw;
} UiEncoderState;

static const int8_t quadrature_table[16] =
{
   0, -1,  1,  0,
   1,  0,  0, -1,
  -1,  0,  0,  1,
   0,  1, -1,  0
};

static UiSwitchState switches[UI_INPUT_SWITCH_COUNT];
static UiEncoderState encoders[UI_INPUT_ENCODER_COUNT];
static bool initialized;

bool UiInputService_Init(const bool raw_switches[UI_INPUT_SWITCH_COUNT],
                         const uint8_t encoder_ab[UI_INPUT_ENCODER_COUNT],
                         uint32_t now_ms)
{
  uint32_t index;

  initialized = false;
  if ((raw_switches == NULL) || (encoder_ab == NULL))
  {
    return false;
  }
  (void)memset(switches, 0, sizeof(switches));
  (void)memset(encoders, 0, sizeof(encoders));
  for (index = 0U; index < UI_INPUT_SWITCH_COUNT; ++index)
  {
    switches[index].stable = raw_switches[index];
    switches[index].candidate = raw_switches[index];
    switches[index].candidate_since_ms = now_ms;
  }
  for (index = 0U; index < UI_INPUT_ENCODER_COUNT; ++index)
  {
    encoders[index].previous_ab = encoder_ab[index] & 0x03U;
  }
  initialized = true;
  return true;
}

void UiInputService_ResetEncoders(
  const uint8_t encoder_ab[UI_INPUT_ENCODER_COUNT], bool reset_counts)
{
  uint32_t index;

  if (!initialized || (encoder_ab == NULL))
  {
    return;
  }
  for (index = 0U; index < UI_INPUT_ENCODER_COUNT; ++index)
  {
    encoders[index].previous_ab = encoder_ab[index] & 0x03U;
    encoders[index].transition_accumulator = 0;
    encoders[index].pending_cw = 0U;
    encoders[index].pending_ccw = 0U;
    if (reset_counts)
    {
      encoders[index].count = 0;
      encoders[index].invalid_transitions = 0U;
    }
  }
}

void UiInputService_UpdateSwitches(
  const bool raw_switches[UI_INPUT_SWITCH_COUNT], uint32_t now_ms,
  UiInputSwitchEvents *events)
{
  uint32_t index;

  if (events != NULL)
  {
    events->high_mask = 0U;
    events->low_mask = 0U;
  }
  if (!initialized || (raw_switches == NULL) || (events == NULL))
  {
    return;
  }
  for (index = 0U; index < UI_INPUT_SWITCH_COUNT; ++index)
  {
    UiSwitchState *input = &switches[index];
    const bool raw = raw_switches[index];

    if (raw != input->candidate)
    {
      input->candidate = raw;
      input->candidate_since_ms = now_ms;
    }
    else if ((raw != input->stable) &&
             ((now_ms - input->candidate_since_ms) >= UI_INPUT_DEBOUNCE_MS))
    {
      input->stable = raw;
      if (raw)
      {
        events->high_mask |= (uint8_t)(1U << index);
      }
      else
      {
        events->low_mask |= (uint8_t)(1U << index);
      }
    }
  }
}

void UiInputService_Tick1ms(
  const uint8_t encoder_ab[UI_INPUT_ENCODER_COUNT])
{
  uint32_t index;

  if (!initialized || (encoder_ab == NULL))
  {
    return;
  }
  for (index = 0U; index < UI_INPUT_ENCODER_COUNT; ++index)
  {
    UiEncoderState *encoder = &encoders[index];
    const uint8_t current_ab = encoder_ab[index] & 0x03U;
    uint8_t transition;
    int8_t delta;

    if (current_ab == encoder->previous_ab)
    {
      continue;
    }
    transition = (uint8_t)((encoder->previous_ab << 2U) | current_ab);
    delta = quadrature_table[transition & 0x0FU];
    encoder->previous_ab = current_ab;
    if (delta == 0)
    {
      ++encoder->invalid_transitions;
      encoder->transition_accumulator = 0;
      continue;
    }
    encoder->transition_accumulator += delta;
    if (encoder->transition_accumulator >= 4)
    {
      ++encoder->count;
      ++encoder->pending_cw;
      encoder->transition_accumulator = 0;
    }
    else if (encoder->transition_accumulator <= -4)
    {
      --encoder->count;
      ++encoder->pending_ccw;
      encoder->transition_accumulator = 0;
    }
  }
}

void UiInputService_TakeEncoderEvents(
  UiInputEncoderEvents events[UI_INPUT_ENCODER_COUNT])
{
  uint32_t index;

  if (events == NULL)
  {
    return;
  }
  (void)memset(events, 0, sizeof(*events) * UI_INPUT_ENCODER_COUNT);
  if (!initialized)
  {
    return;
  }
  for (index = 0U; index < UI_INPUT_ENCODER_COUNT; ++index)
  {
    events[index].clockwise = encoders[index].pending_cw;
    events[index].counterclockwise = encoders[index].pending_ccw;
    events[index].count = encoders[index].count;
    encoders[index].pending_cw = 0U;
    encoders[index].pending_ccw = 0U;
  }
}

bool UiInputService_GetStatus(
  const uint8_t encoder_ab[UI_INPUT_ENCODER_COUNT], UiInputStatus *status)
{
  uint32_t index;

  if (!initialized || (encoder_ab == NULL) || (status == NULL))
  {
    return false;
  }
  for (index = 0U; index < UI_INPUT_SWITCH_COUNT; ++index)
  {
    status->stable_switches[index] = switches[index].stable;
  }
  for (index = 0U; index < UI_INPUT_ENCODER_COUNT; ++index)
  {
    status->encoder_ab[index] = encoder_ab[index] & 0x03U;
    status->encoder_count[index] = encoders[index].count;
    status->invalid_transitions[index] = encoders[index].invalid_transitions;
  }
  return true;
}
