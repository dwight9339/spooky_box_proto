#ifndef SPOOKY_DEMO_LIGHTS_H
#define SPOOKY_DEMO_LIGHTS_H

/*
 * Demo-only button and encoder lights (decision 0011 item 12,
 * full_spooky_proto-p04.3). Computes what the two button LEDs and the four
 * encoder RGB LEDs show from published state and the physical press state:
 *
 * - Buttons (PRES-LED-01 to -05): an adjustable idle level when not pressed,
 *   100% while pressed, Button 0 breathing from off up to a fixed peak while a
 *   session is active (user direction 2026-10-04), and three fast blinks on Button 0 for a rejected or
 *   aborted session. Brightness is perceived lightness converted to PWM duty
 *   through the inverse CIE L* curve. PRES-LED-01 asks for 10%; on the bench
 *   that was too dim to see in normal light, so the demo starts brighter and
 *   the level can be set at run time (user direction 2026-10-04).
 * - Encoders: white on the controls with an action on the current screen;
 *   while the session prompt is open only Encoder 0, the confirm, green to
 *   start and yellow to stop (PRES-LED-06). In Shift only the controls with an
 *   available Shift action are lit (decision 0008 item 8, PRES-INP-01), the
 *   buttons at the idle level. Encoder LEDs are on/off per channel.
 *
 * Portable C with no HAL calls.
 */

#include <stdbool.h>
#include <stdint.h>

/* Encoder LED channel bits. */
#define DEMO_LIGHT_RED 4U
#define DEMO_LIGHT_GREEN 2U
#define DEMO_LIGHT_BLUE 1U

/* Starting values for bench trials (Principle IV). */
#define DEMO_LIGHTS_IDLE_LIGHTNESS 400U       /* permille L*, adjustable */
#define DEMO_LIGHTS_BREATH_HIGH_LIGHTNESS 850U /* breathing runs from off to here */
#define DEMO_LIGHTS_BREATH_PERIOD_MS 4000U
#define DEMO_LIGHTS_BLINK_MS 120U             /* on and off time of a fault blink */
#define DEMO_LIGHTS_BLINKS 3U

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
} DemoLightsInput;

typedef struct
{
  uint16_t button_duty[2]; /* PWM duty in permille */
  uint8_t encoder_rgb[4];  /* DEMO_LIGHT_* bits */
} DemoLightsOutput;

/* Resets the idle level to DEMO_LIGHTS_IDLE_LIGHTNESS. */
void DemoLights_Init(void);
/* Perceived lightness of a button that is not pressed, permille, clamped to
 * 0..1000. */
void DemoLights_SetIdleLightness(uint16_t lightness_permille);
uint16_t DemoLights_IdleLightness(void);
/* SES_PUB_RECORDING_REJECTED or _ABORTED (PRES-LED-05). */
void DemoLights_OnSessionFault(uint32_t now_ms);
void DemoLights_Compute(const DemoLightsInput *input, uint32_t now_ms,
                        DemoLightsOutput *output);
/* Perceived lightness (permille L*) to PWM duty (permille), inverse CIE L*. */
uint16_t DemoLights_LightnessToDuty(uint16_t lightness_permille);

#endif /* SPOOKY_DEMO_LIGHTS_H */
