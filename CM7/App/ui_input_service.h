#ifndef SPOOKY_UI_INPUT_SERVICE_H
#define SPOOKY_UI_INPUT_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#define UI_INPUT_SWITCH_COUNT 6U
#define UI_INPUT_ENCODER_COUNT 4U
#define UI_INPUT_DEBOUNCE_MS 15U

typedef struct
{
  uint8_t high_mask;
  uint8_t low_mask;
} UiInputSwitchEvents;

typedef struct
{
  uint32_t clockwise;
  uint32_t counterclockwise;
  int32_t count;
} UiInputEncoderEvents;

typedef struct
{
  bool stable_switches[UI_INPUT_SWITCH_COUNT];
  uint8_t encoder_ab[UI_INPUT_ENCODER_COUNT];
  int32_t encoder_count[UI_INPUT_ENCODER_COUNT];
  uint32_t invalid_transitions[UI_INPUT_ENCODER_COUNT];
} UiInputStatus;

/*
 * Electrical input processing only. The caller owns GPIO sampling and invokes
 * Tick1ms from exactly one 1 kHz context. Gesture meaning deliberately stays
 * outside this service. Calls have fixed bounds of six switches or four
 * encoders; TakeEncoderEvents must be serialized against Tick1ms by the owner.
 */
bool UiInputService_Init(const bool raw_switches[UI_INPUT_SWITCH_COUNT],
                         const uint8_t encoder_ab[UI_INPUT_ENCODER_COUNT],
                         uint32_t now_ms);
void UiInputService_ResetEncoders(
  const uint8_t encoder_ab[UI_INPUT_ENCODER_COUNT], bool reset_counts);
void UiInputService_UpdateSwitches(
  const bool raw_switches[UI_INPUT_SWITCH_COUNT], uint32_t now_ms,
  UiInputSwitchEvents *events);
void UiInputService_Tick1ms(
  const uint8_t encoder_ab[UI_INPUT_ENCODER_COUNT]);
void UiInputService_TakeEncoderEvents(
  UiInputEncoderEvents events[UI_INPUT_ENCODER_COUNT]);
bool UiInputService_GetStatus(
  const uint8_t encoder_ab[UI_INPUT_ENCODER_COUNT], UiInputStatus *status);

#endif /* SPOOKY_UI_INPUT_SERVICE_H */
