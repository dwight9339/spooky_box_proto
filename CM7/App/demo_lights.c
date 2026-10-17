#include "demo_lights.h"

#include <stddef.h>

#include "demo_view.h"

#define WHITE (DEMO_LIGHT_RED | DEMO_LIGHT_GREEN | DEMO_LIGHT_BLUE)
#define YELLOW (DEMO_LIGHT_RED | DEMO_LIGHT_GREEN)
#define GREEN DEMO_LIGHT_GREEN

#define FULL_LIGHTNESS 1000U

static uint16_t idle_lightness;
static uint16_t encoder_lightness;
static uint16_t balance[DEMO_LIGHT_CHANNELS];
static bool fault_active;
static uint32_t fault_start_ms;
static LightFlash flash;
static DemoLightsFlashCounters flash_counters;

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
  encoder_lightness = DEMO_LIGHTS_ENCODER_LIGHTNESS;
  balance[0] = DEMO_LIGHTS_BALANCE_RED;
  balance[1] = DEMO_LIGHTS_BALANCE_GREEN;
  balance[2] = DEMO_LIGHTS_BALANCE_BLUE;
  fault_active = false;
  fault_start_ms = 0U;
  {
    LightFlashConfig config;

    LightFlash_DefaultConfig(&config);
    (void)LightFlash_Init(&flash, &config);
  }
  flash_counters.onsets = 0U;
  flash_counters.suppressed = 0U;
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

void DemoLights_SetEncoderLightness(uint16_t lightness_permille)
{
  encoder_lightness = (lightness_permille > FULL_LIGHTNESS) ? FULL_LIGHTNESS
                                                            : lightness_permille;
}

uint16_t DemoLights_EncoderLightness(void)
{
  return encoder_lightness;
}

void DemoLights_SetBalance(const uint16_t balance_permille[DEMO_LIGHT_CHANNELS])
{
  uint32_t channel;

  if (balance_permille == NULL)
  {
    return;
  }
  for (channel = 0U; channel < DEMO_LIGHT_CHANNELS; ++channel)
  {
    balance[channel] = (balance_permille[channel] > DEMO_LIGHTS_BALANCE_FULL)
      ? DEMO_LIGHTS_BALANCE_FULL : balance_permille[channel];
  }
}

void DemoLights_GetBalance(uint16_t balance_permille[DEMO_LIGHT_CHANNELS])
{
  uint32_t channel;

  if (balance_permille == NULL)
  {
    return;
  }
  for (channel = 0U; channel < DEMO_LIGHT_CHANNELS; ++channel)
  {
    balance_permille[channel] = balance[channel];
  }
}

bool DemoLights_SetFlash(const LightFlashConfig *config)
{
  return LightFlash_Init(&flash, config);
}

void DemoLights_GetFlash(LightFlashConfig *config)
{
  if (config != NULL)
  {
    *config = flash.config;
  }
}

void DemoLights_GetFlashCounters(DemoLightsFlashCounters *counters)
{
  if (counters != NULL)
  {
    *counters = flash_counters;
  }
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

/* boost: the radio onset flash now, permille of the headroom (decision 0031). */
static void Buttons(const DemoLightsInput *input, uint32_t now_ms, uint16_t boost,
                    uint16_t lightness[2])
{
  bool exempt[2] = {false, false}; /* decision 0031 item 2 */
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
    exempt[0] = true;
  }
  for (button = 0U; button < 2U; ++button)
  {
    if (input->button_pressed[button])
    {
      lightness[button] = FULL_LIGHTNESS;
      exempt[button] = true;
    }
  }
  /* The fault pattern overrides Button 0, then its rule resumes. */
  if (FaultBlink(now_ms, &lightness[0]))
  {
    exempt[0] = true;
  }
  for (button = 0U; button < 2U; ++button)
  {
    if ((boost > 0U) && !exempt[button] && (lightness[button] > 0U))
    {
      lightness[button] = LightFlash_Apply(lightness[button], boost);
    }
  }
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

/* Each channel an encoder's colour uses gets the duty of the encoder level,
 * raised by the onset flash, then scaled by that channel's balance, so the hue
 * holds through a flash. The prompt's Encoder 0 is the confirm cue and uses
 * the prompt level instead. */
static void EncoderDuty(const DemoLightsInput *input, const uint8_t rgb[4], uint16_t boost,
                        uint16_t duty[4][DEMO_LIGHT_CHANNELS])
{
  const bool prompt = !input->shift &&
                      ((input->screen == DEMO_SCREEN_PROMPT_START) ||
                       (input->screen == DEMO_SCREEN_PROMPT_STOP));
  uint32_t encoder;
  uint32_t channel;

  for (encoder = 0U; encoder < 4U; ++encoder)
  {
    const uint32_t lit_duty = DemoLights_LightnessToDuty(
      (prompt && (encoder == 0U)) ? DEMO_LIGHTS_PROMPT_LIGHTNESS
                                  : LightFlash_Apply(encoder_lightness, boost));

    for (channel = 0U; channel < DEMO_LIGHT_CHANNELS; ++channel)
    {
      const bool on = (rgb[encoder] & (DEMO_LIGHT_RED >> channel)) != 0U;

      duty[encoder][channel] = on
        ? (uint16_t)(((lit_duty * balance[channel]) + (DEMO_LIGHTS_BALANCE_FULL / 2U)) /
                     DEMO_LIGHTS_BALANCE_FULL)
        : 0U;
    }
  }
}

/* The onset flash now: onsets are taken only in Field with no prompt and no
 * Shift (decision 0031 items 1 and 3); otherwise they are dropped and a flash
 * in progress ends, so it does not reappear afterwards. */
static uint16_t FlashBoost(const DemoLightsInput *input, uint32_t now_ms)
{
  const bool allowed = input->field && !input->shift &&
                       (input->screen != (uint8_t)DEMO_SCREEN_PROMPT_START) &&
                       (input->screen != (uint8_t)DEMO_SCREEN_PROMPT_STOP);

  if (!allowed)
  {
    if (input->onset != 0U)
    {
      ++flash_counters.suppressed;
    }
    flash.active = false;
    return 0U;
  }
  if (input->onset != 0U)
  {
    ++flash_counters.onsets;
    LightFlash_OnOnset(&flash, input->onset, now_ms);
  }
  return LightFlash_Boost(&flash, now_ms);
}

void DemoLights_Compute(const DemoLightsInput *input, uint32_t now_ms,
                        DemoLightsOutput *output)
{
  uint16_t lightness[2];
  uint16_t boost;
  uint32_t button;

  if ((input == NULL) || (output == NULL))
  {
    return;
  }
  boost = FlashBoost(input, now_ms);
  Buttons(input, now_ms, boost, lightness);
  for (button = 0U; button < 2U; ++button)
  {
    output->button_duty[button] = DemoLights_LightnessToDuty(lightness[button]);
  }
  Encoders(input, output->encoder_rgb);
  EncoderDuty(input, output->encoder_rgb, boost, output->encoder_duty);
}
