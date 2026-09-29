#ifndef SPOOKY_UI_RENDER_SERVICE_H
#define SPOOKY_UI_RENDER_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#define UI_RENDER_LED_COUNT 14U
#define UI_RENDER_LED_STEP_MS 350U
#define UI_RENDER_MATRIX_WIDTH 9U
#define UI_RENDER_MATRIX_HEIGHT 9U
#define UI_RENDER_MATRIX_PIXEL_COUNT 81U
#define UI_RENDER_MATRIX_MAX_FRAME_PIXELS 5U
#define UI_RENDER_MATRIX_FRAME_MS 70U
#define UI_RENDER_DISPLAY_WIDTH 128U
#define UI_RENDER_DISPLAY_HEIGHT 64U
#define UI_RENDER_DISPLAY_BUFFER_SIZE 1024U

typedef struct
{
  uint8_t x;
  uint8_t y;
  uint8_t red;
  uint8_t green;
  uint8_t blue;
} UiRenderPixel;

typedef struct
{
  uint16_t phase;
  uint8_t pixel_count;
  bool final_frame;
  UiRenderPixel pixels[UI_RENDER_MATRIX_MAX_FRAME_PIXELS];
} UiRenderMatrixFrame;

typedef struct
{
  bool led_chase_active;
  uint8_t led_index;
  bool matrix_animation_active;
  uint16_t matrix_frame;
} UiRenderStatus;

/*
 * Bounded, hardware-neutral rendering policy. A service call produces at most
 * one LED action or five matrix pixel writes; the owner performs the physical
 * GPIO/I2C/SPI transfers. The 1 KiB OLED framebuffer is service-local.
 */
void UiRenderService_Init(void);
void UiRenderService_StartLedChase(uint32_t now_ms);
bool UiRenderService_NextLed(uint32_t now_ms, uint8_t *led_index,
                             bool *finished);
void UiRenderService_StartMatrixAnimation(uint32_t now_ms);
bool UiRenderService_PrepareMatrixFrame(uint32_t now_ms,
                                        UiRenderMatrixFrame *frame);
void UiRenderService_CommitMatrixFrame(uint32_t now_ms, bool success);
void UiRenderService_StopMatrixAnimation(void);
const uint8_t *UiRenderService_BuildDisplayTestPattern(void);
bool UiRenderService_GetStatus(UiRenderStatus *status);
void UiRenderService_SafeOff(void);

#endif /* SPOOKY_UI_RENDER_SERVICE_H */
