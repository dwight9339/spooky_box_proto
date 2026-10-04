/* Demo-only button and encoder lights (demo_lights.c, full_spooky_proto-p04.3). */
#include "demo_lights.h"
#include "demo_view.h"

#include <stdio.h>
#include <string.h>

static unsigned failures;

#define CHECK(condition) do {                                               \
    if (!(condition)) {                                                     \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);       \
        ++failures;                                                         \
    }                                                                       \
} while (0)

#define WHITE (DEMO_LIGHT_RED | DEMO_LIGHT_GREEN | DEMO_LIGHT_BLUE)
#define YELLOW (DEMO_LIGHT_RED | DEMO_LIGHT_GREEN)

static DemoLightsInput idle_classic(void)
{
    DemoLightsInput input;

    memset(&input, 0, sizeof(input));
    input.screen = DEMO_SCREEN_CLASSIC;
    input.shift_mode_switch = true;
    input.shift_quick_jump = true;
    return input;
}

static DemoLightsOutput compute(const DemoLightsInput *input, uint32_t now_ms)
{
    DemoLightsOutput output;

    memset(&output, 0xA5, sizeof(output));
    DemoLights_Compute(input, now_ms, &output);
    return output;
}

/* Duty of one button at now_ms. */
static uint16_t duty(const DemoLightsInput *input, uint32_t now_ms, unsigned button)
{
    const DemoLightsOutput output = compute(input, now_ms);

    return output.button_duty[button];
}

#define idle_duty DemoLights_LightnessToDuty(DEMO_LIGHTS_IDLE_LIGHTNESS)

static void test_lightness_curve(void)
{
    uint16_t previous = 0u;

    CHECK(DemoLights_LightnessToDuty(0u) == 0u);
    CHECK(DemoLights_LightnessToDuty(100u) == 11u); /* 10% perceived: 1.1% duty */
    CHECK(DemoLights_LightnessToDuty(400u) == 113u);
    CHECK(DemoLights_LightnessToDuty(500u) == 184u); /* L* 50 is Y 18.4% */
    CHECK(DemoLights_LightnessToDuty(1000u) == 1000u);
    CHECK(DemoLights_LightnessToDuty(5000u) == 1000u);
    for (uint16_t lightness = 0u; lightness <= 1000u; ++lightness) {
        const uint16_t duty = DemoLights_LightnessToDuty(lightness);

        CHECK(duty >= previous);
        previous = duty;
    }
}

static void test_idle_and_pressed(void)
{
    DemoLightsInput input = idle_classic();
    DemoLightsOutput output;

    DemoLights_Init();
    output = compute(&input, 1000u);
    CHECK(output.button_duty[0] == idle_duty); /* PRES-LED-01 */
    CHECK(output.button_duty[1] == idle_duty);
    for (unsigned encoder = 0u; encoder < 4u; ++encoder) {
        CHECK(output.encoder_rgb[encoder] == WHITE);
    }
    input.button_pressed[1] = true;
    output = compute(&input, 1000u);
    CHECK(output.button_duty[0] == idle_duty);
    CHECK(output.button_duty[1] == 1000u); /* PRES-LED-02 */
}

static void test_session_breathing(void)
{
    DemoLightsInput input = idle_classic();
    DemoLightsOutput output;
    const uint32_t half = DEMO_LIGHTS_BREATH_PERIOD_MS / 2u;

    DemoLights_Init();
    input.session_active = true;
    output = compute(&input, 0u);
    CHECK(output.button_duty[0] == 0u); /* breathing goes all the way off */
    CHECK(output.button_duty[1] == idle_duty); /* PRES-LED-04 */
    output = compute(&input, half);
    CHECK(output.button_duty[0] == DemoLights_LightnessToDuty(DEMO_LIGHTS_BREATH_HIGH_LIGHTNESS));
    output = compute(&input, half / 2u);
    CHECK(output.button_duty[0] > 0u);
    CHECK(output.button_duty[0] < DemoLights_LightnessToDuty(DEMO_LIGHTS_BREATH_HIGH_LIGHTNESS));
    CHECK(duty(&input, half / 2u, 0u) ==
          duty(&input, half + half / 2u, 0u));
    input.button_pressed[0] = true;
    CHECK(duty(&input, half / 2u, 0u) == 1000u);
}

static void test_fault_blinks_then_resume(void)
{
    DemoLightsInput input = idle_classic();
    const uint32_t start = 0xFFFFFF00u; /* across the clock wrap */
    const uint32_t blink = DEMO_LIGHTS_BLINK_MS;

    DemoLights_Init();
    DemoLights_OnSessionFault(start);
    for (uint32_t n = 0u; n < DEMO_LIGHTS_BLINKS; ++n) {
        CHECK(duty(&input, start + (2u * n * blink), 0u) == 1000u);
        CHECK(duty(&input, start + (2u * n * blink) + blink, 0u) == 0u);
    }
    CHECK(duty(&input, start + (2u * DEMO_LIGHTS_BLINKS * blink), 0u) ==
          idle_duty);
    /* Over for good: an earlier time does not restart it. */
    CHECK(duty(&input, start, 0u) == idle_duty);
    /* Button 1 never blinks. */
    DemoLights_OnSessionFault(start);
    CHECK(duty(&input, start + blink, 1u) == idle_duty);
}

static void test_shift_lights_available_actions(void)
{
    DemoLightsInput input = idle_classic();
    DemoLightsOutput output;

    DemoLights_Init();
    input.shift = true;
    output = compute(&input, 0u);
    CHECK(output.encoder_rgb[0] == WHITE); /* utility root */
    CHECK(output.encoder_rgb[1] == WHITE); /* Manual quick-jump */
    CHECK(output.encoder_rgb[2] == 0u);
    CHECK(output.encoder_rgb[3] == 0u);
    CHECK(output.button_duty[0] == idle_duty); /* mode switch available */
    CHECK(output.button_duty[1] == 0u);       /* save not in this build */

    input.session_active = true;
    input.shift_mode_switch = false;
    input.shift_quick_jump = false;
    output = compute(&input, 0u);
    CHECK(output.encoder_rgb[1] == 0u);
    CHECK(output.button_duty[0] == 0u); /* no breathing while Shift is held */
    input.button_pressed[0] = true;
    CHECK(duty(&input, 0u, 0u) == 1000u);
}

static void test_screens(void)
{
    DemoLightsInput input = idle_classic();
    DemoLightsOutput output;

    DemoLights_Init();
    input.screen = DEMO_SCREEN_PROMPT_START;
    output = compute(&input, 0u);
    CHECK(output.encoder_rgb[0] == DEMO_LIGHT_GREEN); /* PRES-LED-06 */
    CHECK((output.encoder_rgb[1] | output.encoder_rgb[2] | output.encoder_rgb[3]) == 0u);
    input.screen = DEMO_SCREEN_PROMPT_STOP;
    output = compute(&input, 0u);
    CHECK(output.encoder_rgb[0] == YELLOW);
    CHECK((output.encoder_rgb[1] | output.encoder_rgb[2] | output.encoder_rgb[3]) == 0u);
    input.screen = DEMO_SCREEN_BAND_MENU;
    output = compute(&input, 0u);
    CHECK(output.encoder_rgb[0] == WHITE);
    CHECK(output.encoder_rgb[1] == WHITE);
    CHECK((output.encoder_rgb[2] | output.encoder_rgb[3]) == 0u);
    input.screen = DEMO_SCREEN_INSTRUMENT;
    output = compute(&input, 0u);
    CHECK((output.encoder_rgb[0] | output.encoder_rgb[1] | output.encoder_rgb[2] |
           output.encoder_rgb[3]) == 0u);
    /* Null arguments are ignored. */
    DemoLights_Compute(NULL, 0u, &output);
    DemoLights_Compute(&input, 0u, NULL);
}

static void test_adjustable_idle(void)
{
    DemoLightsInput input = idle_classic();
    const uint32_t half = DEMO_LIGHTS_BREATH_PERIOD_MS / 2u;

    DemoLights_Init();
    CHECK(DemoLights_IdleLightness() == DEMO_LIGHTS_IDLE_LIGHTNESS);
    DemoLights_SetIdleLightness(600u);
    CHECK(DemoLights_IdleLightness() == 600u);
    CHECK(duty(&input, 0u, 0u) == DemoLights_LightnessToDuty(600u));
    CHECK(duty(&input, 0u, 1u) == DemoLights_LightnessToDuty(600u));
    input.button_pressed[1] = true;
    CHECK(duty(&input, 0u, 1u) == 1000u); /* still room to brighten */
    input.button_pressed[1] = false;
    /* Breathing does not depend on the idle level. */
    input.session_active = true;
    CHECK(duty(&input, 0u, 0u) == 0u);
    CHECK(duty(&input, half, 0u) ==
          DemoLights_LightnessToDuty(DEMO_LIGHTS_BREATH_HIGH_LIGHTNESS));
    DemoLights_SetIdleLightness(900u);
    CHECK(duty(&input, 0u, 0u) == 0u);
    CHECK(duty(&input, half, 0u) ==
          DemoLights_LightnessToDuty(DEMO_LIGHTS_BREATH_HIGH_LIGHTNESS));
    DemoLights_SetIdleLightness(5000u);
    CHECK(DemoLights_IdleLightness() == 1000u);
    DemoLights_SetIdleLightness(0u);
    input.session_active = false;
    CHECK(duty(&input, 0u, 0u) == 0u);
    /* Init restores the default. */
    DemoLights_Init();
    CHECK(DemoLights_IdleLightness() == DEMO_LIGHTS_IDLE_LIGHTNESS);
}

int main(void)
{
    test_lightness_curve();
    test_idle_and_pressed();
    test_session_breathing();
    test_fault_blinks_then_resume();
    test_shift_lights_available_actions();
    test_screens();
    test_adjustable_idle();
    if (failures != 0u) {
        printf("%u failure(s)\n", failures);
        return 1;
    }
    printf("demo_lights_test passed\n");
    return 0;
}
