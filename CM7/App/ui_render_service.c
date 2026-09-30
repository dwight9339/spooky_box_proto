#include "ui_render_service.h"

#include <stddef.h>
#include <string.h>

#define UI_MATRIX_TRAIL_LENGTH 4U

static bool led_chase_active;
static uint8_t led_index;
static uint32_t led_next_ms;
static bool matrix_animation_active;
static uint16_t matrix_frame;
static uint32_t matrix_next_ms;
static uint8_t display_buffer[UI_RENDER_DISPLAY_BUFFER_SIZE];

static void MatrixPathPosition(uint16_t position, uint8_t *x, uint8_t *y)
{
  *y = (uint8_t)(position / UI_RENDER_MATRIX_WIDTH);
  *x = (uint8_t)(position % UI_RENDER_MATRIX_WIDTH);
  if ((*y & 1U) != 0U)
  {
    *x = (UI_RENDER_MATRIX_WIDTH - 1U) - *x;
  }
}

static void MatrixPathColor(uint16_t position, uint8_t level,
                            uint8_t *red, uint8_t *green, uint8_t *blue)
{
  const uint8_t phase = (uint8_t)(position % UI_RENDER_MATRIX_PIXEL_COUNT);
  uint8_t blend;

  *red = 0U;
  *green = 0U;
  *blue = 0U;
  if (phase < 27U)
  {
    blend = phase;
    *red = (uint8_t)(((uint16_t)level * (27U - blend)) / 27U);
    *green = (uint8_t)(((uint16_t)level * blend) / 27U);
  }
  else if (phase < 54U)
  {
    blend = phase - 27U;
    *green = (uint8_t)(((uint16_t)level * (27U - blend)) / 27U);
    *blue = (uint8_t)(((uint16_t)level * blend) / 27U);
  }
  else
  {
    blend = phase - 54U;
    *blue = (uint8_t)(((uint16_t)level * (27U - blend)) / 27U);
    *red = (uint8_t)(((uint16_t)level * blend) / 27U);
  }
}

static void SetDisplayPixel(uint8_t x, uint8_t y)
{
  if ((x < UI_RENDER_DISPLAY_WIDTH) && (y < UI_RENDER_DISPLAY_HEIGHT))
  {
    display_buffer[((uint16_t)(y >> 3U) * UI_RENDER_DISPLAY_WIDTH) + x] |=
      (uint8_t)(1U << (y & 7U));
  }
}

static void DrawDisplayLine(int32_t x0, int32_t y0,
                            int32_t x1, int32_t y1)
{
  const int32_t dx = (x1 >= x0) ? (x1 - x0) : (x0 - x1);
  const int32_t sx = (x0 < x1) ? 1 : -1;
  const int32_t dy = -((y1 >= y0) ? (y1 - y0) : (y0 - y1));
  const int32_t sy = (y0 < y1) ? 1 : -1;
  int32_t error = dx + dy;

  for (;;)
  {
    SetDisplayPixel((uint8_t)x0, (uint8_t)y0);
    if ((x0 == x1) && (y0 == y1))
    {
      break;
    }
    if ((2 * error) >= dy)
    {
      error += dy;
      x0 += sx;
    }
    if ((2 * error) <= dx)
    {
      error += dx;
      y0 += sy;
    }
  }
}

void UiRenderService_Init(void)
{
  led_chase_active = false;
  led_index = 0U;
  led_next_ms = 0U;
  matrix_animation_active = false;
  matrix_frame = 0U;
  matrix_next_ms = 0U;
  (void)memset(display_buffer, 0, sizeof(display_buffer));
}

void UiRenderService_StartLedChase(uint32_t now_ms)
{
  led_chase_active = true;
  led_index = 0U;
  led_next_ms = now_ms + UI_RENDER_LED_STEP_MS;
}

bool UiRenderService_NextLed(uint32_t now_ms, uint8_t *next_led,
                             bool *finished)
{
  if ((next_led == NULL) || (finished == NULL) || !led_chase_active ||
      ((int32_t)(now_ms - led_next_ms) < 0))
  {
    return false;
  }
  ++led_index;
  *next_led = led_index;
  *finished = led_index >= UI_RENDER_LED_COUNT;
  if (*finished)
  {
    led_chase_active = false;
  }
  else
  {
    led_next_ms = now_ms + UI_RENDER_LED_STEP_MS;
  }
  return true;
}

void UiRenderService_StartMatrixAnimation(uint32_t now_ms)
{
  matrix_animation_active = true;
  matrix_frame = 0U;
  matrix_next_ms = now_ms;
}

bool UiRenderService_PrepareMatrixFrame(uint32_t now_ms,
                                        UiRenderMatrixFrame *frame)
{
  static const uint8_t trail_levels[UI_MATRIX_TRAIL_LENGTH] =
    {192U, 72U, 24U, 8U};
  uint8_t age;

  if ((frame == NULL) || !matrix_animation_active ||
      ((int32_t)(now_ms - matrix_next_ms) < 0))
  {
    return false;
  }
  (void)memset(frame, 0, sizeof(*frame));
  frame->phase = matrix_frame;
  if ((matrix_frame >= UI_MATRIX_TRAIL_LENGTH) &&
      ((matrix_frame - UI_MATRIX_TRAIL_LENGTH) <
       UI_RENDER_MATRIX_PIXEL_COUNT))
  {
    UiRenderPixel *pixel = &frame->pixels[frame->pixel_count++];
    MatrixPathPosition(matrix_frame - UI_MATRIX_TRAIL_LENGTH,
                       &pixel->x, &pixel->y);
  }
  for (age = 0U; age < UI_MATRIX_TRAIL_LENGTH; ++age)
  {
    uint16_t position;
    UiRenderPixel *pixel;

    if (matrix_frame < age)
    {
      continue;
    }
    position = matrix_frame - age;
    if (position >= UI_RENDER_MATRIX_PIXEL_COUNT)
    {
      continue;
    }
    pixel = &frame->pixels[frame->pixel_count++];
    MatrixPathPosition(position, &pixel->x, &pixel->y);
    MatrixPathColor(position, trail_levels[age], &pixel->red,
                    &pixel->green, &pixel->blue);
  }
  frame->final_frame =
    (matrix_frame + 1U) >=
    (UI_RENDER_MATRIX_PIXEL_COUNT + UI_MATRIX_TRAIL_LENGTH);
  return true;
}

void UiRenderService_CommitMatrixFrame(uint32_t now_ms, bool success)
{
  if (!matrix_animation_active)
  {
    return;
  }
  if (!success)
  {
    matrix_animation_active = false;
    return;
  }
  ++matrix_frame;
  if (matrix_frame >=
      (UI_RENDER_MATRIX_PIXEL_COUNT + UI_MATRIX_TRAIL_LENGTH))
  {
    matrix_animation_active = false;
  }
  else
  {
    matrix_next_ms = now_ms + UI_RENDER_MATRIX_FRAME_MS;
  }
}

void UiRenderService_StopMatrixAnimation(void)
{
  matrix_animation_active = false;
}

const uint8_t *UiRenderService_BuildDisplayTestPattern(void)
{
  uint16_t x;
  uint8_t y;

  (void)memset(display_buffer, 0, sizeof(display_buffer));
  DrawDisplayLine(0, 0, 127, 0);
  DrawDisplayLine(0, 63, 127, 63);
  DrawDisplayLine(0, 0, 0, 63);
  DrawDisplayLine(127, 0, 127, 63);
  DrawDisplayLine(0, 0, 127, 63);
  DrawDisplayLine(0, 63, 127, 0);
  DrawDisplayLine(63, 0, 63, 63);
  DrawDisplayLine(0, 31, 127, 31);
  for (y = 5U; y <= 12U; ++y)
  {
    for (x = 5U; x <= 12U; ++x)
    {
      SetDisplayPixel((uint8_t)x, y);
    }
  }
  DrawDisplayLine(110, 5, 121, 5);
  DrawDisplayLine(110, 5, 110, 16);
  DrawDisplayLine(121, 5, 121, 16);
  DrawDisplayLine(110, 16, 121, 16);
  DrawDisplayLine(6, 49, 25, 49);
  DrawDisplayLine(6, 53, 20, 53);
  DrawDisplayLine(6, 57, 15, 57);
  for (x = 106U; x <= 121U; x += 3U)
  {
    DrawDisplayLine((int32_t)x, 48, (int32_t)x, 58);
  }
  return display_buffer;
}

bool UiRenderService_GetStatus(UiRenderStatus *status)
{
  if (status == NULL)
  {
    return false;
  }
  status->led_chase_active = led_chase_active;
  status->led_index = led_index;
  status->matrix_animation_active = matrix_animation_active;
  status->matrix_frame = matrix_frame;
  return true;
}

void UiRenderService_SafeOff(void)
{
  led_chase_active = false;
  matrix_animation_active = false;
}
