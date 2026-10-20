#ifndef SPOOKY_DEMO_LIGHTS_H
#define SPOOKY_DEMO_LIGHTS_H

/*
 * Demo-only button and encoder lights (decision 0011 item 12,
 * full_spooky_proto-p04.3). Computes what the two button LEDs and the four
 * encoder RGB LEDs show from published state and the physical press state:
 *
 * - Buttons (PRES-LED-01 to -05): 100% while pressed, and three fast blinks on
 *   Button 0 for a rejected or aborted session. Button 1 shows an adjustable
 *   idle level when not pressed. Brightness is perceived lightness converted to
 *   PWM duty through the inverse CIE L* curve. PRES-LED-01 asks for 10%; on the
 *   bench that was too dim to see in normal light, so the demo starts brighter
 *   and the level can be set at run time (user direction 2026-10-04).
 * - Button 0 is only ever off or full (full_spooky_proto-p04.32, user
 *   direction 2026-10-20). On the Nucleo, its PF6 trace runs beside the PDM
 *   clock (PC2), and its PWM edges put click bursts in the microphone (evidence
 *   2026-10-19-button0-pwm-mic-bursts). So it is dark when idle instead of
 *   showing the idle level (PRES-LED-01), blinks slowly during a session instead
 *   of breathing (PRES-LED-03), is full in Shift when its action is available,
 *   and never flashes.
 * - Encoders: white on the controls with an action on the current screen;
 *   while the session prompt is open only Encoder 0, the confirm, green to
 *   start and yellow to stop (PRES-LED-06). In Shift only the controls with an
 *   available Shift action are lit (decision 0008 item 8, PRES-INP-01),
 *   Button 1 at the idle level. A lit encoder shows its colour at its own
 *   adjustable level (full_spooky_proto-p04.12): the encoders need a higher
 *   level than the buttons to look as bright (bench 2026-10-16). Each channel
 *   the colour uses gets that level's PWM duty, scaled down by a per-channel
 *   balance so white looks neutral at every level. The prompt's green or
 *   yellow Encoder 0 is at full lightness: at the encoder level a single
 *   channel was too dim to read as the confirm cue (bench 2026-10-16).
 * - Radio onset flashes (decision 0031, full_spooky_proto-p04.29): in Field,
 *   each onset from the feed's lights reader raises every lit encoder and
 *   button toward full through the light_flash envelope, encoders keeping
 *   their hue. Dark controls stay dark. No flash on a held button or ever on
 *   Button 0 (item 2, and p04.32), and none while
 *   Shift is held, while the prompt is open or in Instrument (item 3): onsets
 *   then are dropped and a flash in progress stops showing.
 *
 * Portable C with no HAL calls.
 */

#include <stdbool.h>
#include <stdint.h>

#include "light_flash.h"

/* Encoder LED channel bits. */
#define DEMO_LIGHT_RED 4U
#define DEMO_LIGHT_GREEN 2U
#define DEMO_LIGHT_BLUE 1U

/* Encoder LED channels, in the order of encoder_duty. */
#define DEMO_LIGHT_CHANNELS 3U /* red, green, blue */

/* Starting values for bench trials (Principle IV). */
#define DEMO_LIGHTS_IDLE_LIGHTNESS 400U       /* permille L*, Button 1, adjustable */
#define DEMO_LIGHTS_SESSION_BLINK_MS 1000U    /* Button 0's on and off time in a session */
#define DEMO_LIGHTS_BLINK_MS 120U             /* on and off time of a fault blink */
#define DEMO_LIGHTS_BLINKS 3U
#define DEMO_LIGHTS_ENCODER_LIGHTNESS 800U    /* permille L*, set by eye on the bench */
#define DEMO_LIGHTS_PROMPT_LIGHTNESS 1000U    /* the prompt's Encoder 0 (PRES-LED-06) */
#define DEMO_LIGHTS_BALANCE_FULL 1000U         /* permille: channel duty unscaled */
/* Encoder white balance, permille of each channel's duty, set by eye on the
 * bench (2026-10-16). */
#define DEMO_LIGHTS_BALANCE_RED 1000U
#define DEMO_LIGHTS_BALANCE_GREEN 450U
#define DEMO_LIGHTS_BALANCE_BLUE 350U

typedef struct
{
  uint8_t screen;         /* DemoScreen */
  bool shift;
  bool session_active;    /* Session Recording or Finalizing */
  bool button_pressed[2]; /* debounced physical state of Buttons 0 and 1 */
  /* Shift actions available now. */
  bool shift_mode_switch; /* Shift plus Button 0: not during a session */
  bool shift_save;        /* Shift plus Button 1: capture save */
  bool shift_quick_jump;  /* Shift plus Encoder 1: Classic or a quick-jumped Manual */
  bool field;             /* Context mode is Field (not Instrument) */
  uint8_t onset;          /* RadioOnset size taken since the last pass, 0 for none */
} DemoLightsInput;

typedef struct
{
  uint32_t onsets;     /* onsets taken while flashes are allowed */
  uint32_t suppressed; /* onsets dropped: Shift, prompt or Instrument */
} DemoLightsFlashCounters;

typedef struct
{
  uint16_t button_duty[2]; /* PWM duty in permille */
  uint8_t encoder_rgb[4];  /* DEMO_LIGHT_* bits: the colour of each encoder */
  /* PWM duty in permille of each encoder's red, green and blue channel. */
  uint16_t encoder_duty[4][DEMO_LIGHT_CHANNELS];
} DemoLightsOutput;

/* Resets the button idle level, the encoder level and the balance to their
 * DEMO_LIGHTS_* defaults, the flash to LightFlash_DefaultConfig, and the flash
 * counters. */
void DemoLights_Init(void);
/* Perceived lightness of Button 1 when not pressed, permille, clamped to
 * 0..1000. Button 0 has no idle level (p04.32). */
void DemoLights_SetIdleLightness(uint16_t lightness_permille);
uint16_t DemoLights_IdleLightness(void);
/* Perceived lightness of a lit encoder, permille, clamped to 0..1000. */
void DemoLights_SetEncoderLightness(uint16_t lightness_permille);
uint16_t DemoLights_EncoderLightness(void);
/* Scale of each encoder channel's duty, permille, clamped to 0..1000, so the
 * three channels of a white encoder look neutral (set by eye on the bench). */
void DemoLights_SetBalance(const uint16_t balance_permille[DEMO_LIGHT_CHANNELS]);
void DemoLights_GetBalance(uint16_t balance_permille[DEMO_LIGHT_CHANNELS]);
/* Flash steps and decay (decision 0031 item 8); false and unchanged if
 * LightFlash_Init refuses them. The flash in progress ends. */
bool DemoLights_SetFlash(const LightFlashConfig *config);
void DemoLights_GetFlash(LightFlashConfig *config);
void DemoLights_GetFlashCounters(DemoLightsFlashCounters *counters);
/* SES_PUB_RECORDING_REJECTED or _ABORTED (PRES-LED-05). */
void DemoLights_OnSessionFault(uint32_t now_ms);
void DemoLights_Compute(const DemoLightsInput *input, uint32_t now_ms,
                        DemoLightsOutput *output);
/* Perceived lightness (permille L*) to PWM duty (permille), inverse CIE L*. */
uint16_t DemoLights_LightnessToDuty(uint16_t lightness_permille);

#endif /* SPOOKY_DEMO_LIGHTS_H */
