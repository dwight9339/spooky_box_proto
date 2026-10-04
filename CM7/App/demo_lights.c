#include "demo_lights.h"

#include <stddef.h>

#include "demo_view.h"

#define WHITE (DEMO_LIGHT_RED | DEMO_LIGHT_GREEN | DEMO_LIGHT_BLUE)
#define YELLOW (DEMO_LIGHT_RED | DEMO_LIGHT_GREEN)
#define GREEN DEMO_LIGHT_GREEN

#define FULL_LIGHTNESS 1000U

static uint16_t idle_lightness;
static bool fault_active;
static uint32_t fault_start_ms;

uint16_t DemoLights_LightnessToDuty(uint16_t lightness_permille)
{
  const uint64_t lightness = (lightness_permille > FULL_LIGHTNESS)
    ? FULL_LIGHTNESS : lightness_permille;

  /* L* = lightness / 10. Y = ((L* + 16) / 116)^3 above L* = 8, else L* / 903.3. */
  if (lightness > 80U)
  {
    const uint64_t base = lightness + 160U;
    const uint64_t scale = 1160ULL * 1160ULL * 1160ULL;

    return (uint16_t)(((base * base * base * 1000U) + (scale / 2U)) / scale);
  }
  return (uint16_t)(((lightness * 1000U) + 4516U) / 9033U);
}

void DemoLights_Init(void)
{
  idle_lightness = DEMO_LIGHTS_IDLE_LIGHTNESS;
  fault_active = false;
  fault_start_ms = 0U;
}

void DemoLights_SetIdleLightness(uint16_t lightness_permille)
{
  idle_lightness = (lightness_permille > FULL_LIGHTNESS) ? FULL_LIGHTNESS
                                                         : lightness_permille;
}

uint16_t DemoLights_IdleLightness(void)
{
  return idle_lightness;
}

void DemoLights_OnSessionFault(uint32_t now_ms)
{
  fault_active = true;
  fault_start_ms = now_ms;
}

/* Triangle from off up to the breathing peak, in perceived lightness. */
static uint16_t Breath(uint32_t now_ms)
{
  const uint32_t half = DEMO_LIGHTS_BREATH_PERIOD_MS / 2U;
  const uint32_t phase = now_ms % DEMO_LIGHTS_BREATH_PERIOD_MS;
  const uint32_t rise = (phase < half) ? phase : (DEMO_LIGHTS_BREATH_PERIOD_MS - phase);

  return (uint16_t)((DEMO_LIGHTS_BREATH_HIGH_LIGHTNESS * rise) / half);
}

/* Lightness of the fault blinks, or false once they are over. */
static bool FaultBlink(uint32_t now_ms, uint16_t *lightness)
{
  const uint32_t elapsed = now_ms - fault_start_ms;

  if (!fault_active)
  {
    return false;
  }
  if (elapsed >= (2U * DEMO_LIGHTS_BLINK_MS * DEMO_LIGHTS_BLINKS))
  {
    fault_active = false;
    return false;
  }
  *lightness = (((elapsed / DEMO_LIGHTS_BLINK_MS) % 2U) == 0U) ? FULL_LIGHTNESS : 0U;
  return true;
}

static void Buttons(const DemoLightsInput *input, uint32_t now_ms, uint16_t lightness[2])
{
  uint32_t button;

  for (button = 0U; button < 2U; ++button)
  {
    lightness[button] = idle_lightness;
  }
  if (input->shift)
  {
    lightness[0] = input->shift_mode_switch ? idle_lightness : 0U;
    lightness[1] = input->shift_save ? idle_lightness : 0U;
  }
  else if (input->session_active)
  {
    lightness[0] = Breath(now_ms);
  }
  for (button = 0U; button < 2U; ++button)
  {
    if (input->button_pressed[button])
    {
      lightness[button] = FULL_LIGHTNESS;
    }
  }
  /* The fault pattern overrides Button 0, then its rule resumes. */
  (void)FaultBlink(now_ms, &lightness[0]);
}

static void Encoders(const DemoLightsInput *input, uint8_t rgb[4])
{
  uint32_t encoder;

  for (encoder = 0U; encoder < 4U; ++encoder)
  {
    rgb[encoder] = 0U;
  }
  if (input->shift)
  {
    rgb[0] = WHITE; /* utility root */
    rgb[1] = input->shift_quick_jump ? WHITE : 0U;
    return;
  }
  switch (input->screen)
  {
    case DEMO_SCREEN_CLASSIC:
      for (encoder = 0U; encoder < 4U; ++encoder)
      {
        rgb[encoder] = WHITE;
      }
      break;
    case DEMO_SCREEN_MANUAL:
      rgb[3] = WHITE; /* page and Shift; Manual tuning is not in the demo */
      break;
    case DEMO_SCREEN_BAND_MENU:
    case DEMO_SCREEN_ENGINE_MENU:
      rgb[0] = WHITE;
      rgb[1] = WHITE;
      break;
    case DEMO_SCREEN_PROMPT_START:
      rgb[0] = GREEN;
      break;
    case DEMO_SCREEN_PROMPT_STOP:
      rgb[0] = YELLOW;
      break;
    case DEMO_SCREEN_UTILITY:
      rgb[1] = WHITE;
      rgb[3] = WHITE;
      break;
    case DEMO_SCREEN_INSTRUMENT:
    default:
      break;
  }
}

void DemoLights_Compute(const DemoLightsInput *input, uint32_t now_ms,
                        DemoLightsOutput *output)
{
  uint16_t lightness[2];
  uint32_t button;

  if ((input == NULL) || (output == NULL))
  {
    return;
  }
  Buttons(input, now_ms, lightness);
  for (button = 0U; button < 2U; ++button)
  {
    output->button_duty[button] = DemoLights_LightnessToDuty(lightness[button]);
  }
  Encoders(input, output->encoder_rgb);
}
