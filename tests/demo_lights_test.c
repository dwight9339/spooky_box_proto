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
    input.field = true;
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

static const uint16_t unity_balance[DEMO_LIGHT_CHANNELS] = {1000u, 1000u, 1000u};

/* A lit encoder shows its colour at the encoder level (p04.12): every channel
 * of its colour gets that level's duty, the others none, and an unlit encoder
 * is dark. The encoder level and the button idle level are independent. */
static void test_encoder_levels(void)
{
    const uint16_t encoder_duty = DemoLights_LightnessToDuty(DEMO_LIGHTS_ENCODER_LIGHTNESS);
    DemoLightsInput input = idle_classic();
    DemoLightsOutput output;

    DemoLights_Init();
    CHECK(DemoLights_EncoderLightness() == DEMO_LIGHTS_ENCODER_LIGHTNESS);
    DemoLights_SetBalance(unity_balance);
    output = compute(&input, 0u);
    for (unsigned encoder = 0u; encoder < 4u; ++encoder) {
        for (unsigned channel = 0u; channel < DEMO_LIGHT_CHANNELS; ++channel) {
            CHECK(output.encoder_duty[encoder][channel] == encoder_duty);
        }
    }
    CHECK(output.button_duty[0] == idle_duty);

    /* Encoder 0 yellow at the prompt level (the confirm cue), the rest dark. */
    input.screen = DEMO_SCREEN_PROMPT_STOP;
    output = compute(&input, 0u);
    CHECK(output.encoder_duty[0][0] ==
          DemoLights_LightnessToDuty(DEMO_LIGHTS_PROMPT_LIGHTNESS));
    CHECK(output.encoder_duty[0][1] ==
          DemoLights_LightnessToDuty(DEMO_LIGHTS_PROMPT_LIGHTNESS));
    CHECK(output.encoder_duty[0][2] == 0u);
    for (unsigned encoder = 1u; encoder < 4u; ++encoder) {
        for (unsigned channel = 0u; channel < DEMO_LIGHT_CHANNELS; ++channel) {
            CHECK(output.encoder_duty[encoder][channel] == 0u);
        }
    }
    input.screen = DEMO_SCREEN_PROMPT_START;  /* green, also at the prompt level */
    output = compute(&input, 0u);
    CHECK(output.encoder_duty[0][1] ==
          DemoLights_LightnessToDuty(DEMO_LIGHTS_PROMPT_LIGHTNESS));
    CHECK(output.encoder_duty[0][0] == 0u);
    input.shift = true;                       /* Shift: utility root at the encoder level */
    output = compute(&input, 0u);
    CHECK(output.encoder_duty[0][0] == encoder_duty);

    input = idle_classic();
    DemoLights_SetEncoderLightness(1000u);
    output = compute(&input, 0u);
    CHECK(output.encoder_duty[3][2] == 1000u);
    CHECK(output.button_duty[1] == idle_duty);  /* the buttons keep their level */
    DemoLights_SetIdleLightness(0u);            /* and the encoders keep theirs */
    output = compute(&input, 0u);
    CHECK(output.encoder_duty[3][2] == 1000u);
    CHECK(output.button_duty[1] == 0u);
    DemoLights_SetEncoderLightness(5000u);      /* clamped */
    CHECK(DemoLights_EncoderLightness() == 1000u);
    DemoLights_SetEncoderLightness(0u);
    output = compute(&input, 0u);
    CHECK(output.encoder_duty[3][2] == 0u);

    DemoLights_Init();                          /* boot resets it */
    CHECK(DemoLights_EncoderLightness() == DEMO_LIGHTS_ENCODER_LIGHTNESS);
}

/* The balance scales each channel's duty so white looks neutral, is clamped to
 * full, and leaves the buttons alone. */
static void test_encoder_balance(void)
{
    const uint16_t balance[DEMO_LIGHT_CHANNELS] = {1000u, 500u, 250u};
    const uint16_t too_high[DEMO_LIGHT_CHANNELS] = {1001u, 2000u, 0u};
    uint16_t read[DEMO_LIGHT_CHANNELS];
    DemoLightsInput input = idle_classic();
    DemoLightsOutput output;

    DemoLights_Init();
    DemoLights_GetBalance(read);
    CHECK(read[0] == DEMO_LIGHTS_BALANCE_RED);
    CHECK(read[1] == DEMO_LIGHTS_BALANCE_GREEN);
    CHECK(read[2] == DEMO_LIGHTS_BALANCE_BLUE);
    DemoLights_SetEncoderLightness(1000u);
    DemoLights_SetBalance(balance);
    output = compute(&input, 0u);
    CHECK(output.encoder_duty[2][0] == 1000u);
    CHECK(output.encoder_duty[2][1] == 500u);
    CHECK(output.encoder_duty[2][2] == 250u);
    CHECK(output.button_duty[0] == idle_duty);

    DemoLights_SetEncoderLightness(DEMO_LIGHTS_IDLE_LIGHTNESS);   /* duty 113 */
    output = compute(&input, 0u);
    CHECK(output.encoder_duty[2][1] == 57u);                     /* rounded half */
    CHECK(output.encoder_duty[2][2] == 28u);

    DemoLights_SetBalance(too_high);
    DemoLights_GetBalance(read);
    CHECK(read[0] == 1000u && read[1] == 1000u && read[2] == 0u);
    output = compute(&input, 0u);
    CHECK(output.encoder_duty[1][2] == 0u);

    DemoLights_Init();                                             /* boot resets it */
    DemoLights_GetBalance(read);
    CHECK(read[2] == DEMO_LIGHTS_BALANCE_BLUE);
}

/* --- Radio onset flashes (decision 0031, p04.29) ------------------------- */

/* Duty of one encoder channel at a lightness, with the default balance. */
static uint16_t encoder_channel(uint16_t lightness, unsigned channel)
{
    static const uint16_t balance[DEMO_LIGHT_CHANNELS] = {
        DEMO_LIGHTS_BALANCE_RED, DEMO_LIGHTS_BALANCE_GREEN, DEMO_LIGHTS_BALANCE_BLUE};

    return (uint16_t)(((uint32_t)DemoLights_LightnessToDuty(lightness) * balance[channel] +
                       500u) / 1000u);
}

/* One pass with an onset of this size at now_ms. */
static DemoLightsOutput onset_at(DemoLightsInput input, uint8_t size, uint32_t now_ms)
{
    input.onset = size;
    return compute(&input, now_ms);
}

/* Items 5 to 7: a lit control rises by thirds of its headroom, encoders keep
 * their hue (channels scaled together), and the flash decays one third per
 * 80 ms back to the idle levels. */
static void test_flash_raises_lit_lights(void)
{
    const uint16_t medium_button = DemoLights_LightnessToDuty(
        LightFlash_Apply(DEMO_LIGHTS_IDLE_LIGHTNESS, 667u));
    DemoLightsInput input = idle_classic();
    DemoLightsOutput output;

    DemoLights_Init();
    output = onset_at(input, 3u, 1000u);                 /* large: to full */
    CHECK(output.button_duty[0] == 1000u);
    CHECK(output.button_duty[1] == 1000u);
    for (unsigned encoder = 0u; encoder < 4u; ++encoder) {
        for (unsigned channel = 0u; channel < DEMO_LIGHT_CHANNELS; ++channel) {
            CHECK(output.encoder_duty[encoder][channel] == encoder_channel(1000u, channel));
        }
    }
    output = compute(&input, 1080u);                     /* one step down: medium */
    CHECK(output.button_duty[1] == medium_button);
    CHECK(output.encoder_duty[2][1] ==
          encoder_channel(LightFlash_Apply(DEMO_LIGHTS_ENCODER_LIGHTNESS, 667u), 1u));
    output = compute(&input, 1240u);                     /* over */
    CHECK(output.button_duty[0] == idle_duty);
    CHECK(output.encoder_duty[3][0] == encoder_channel(DEMO_LIGHTS_ENCODER_LIGHTNESS, 0u));

    DemoLights_Init();
    output = onset_at(input, 1u, 2000u);                 /* small: one third */
    CHECK(output.button_duty[0] == DemoLights_LightnessToDuty(
        LightFlash_Apply(DEMO_LIGHTS_IDLE_LIGHTNESS, 333u)));
    output = onset_at(input, 3u, 2040u);                 /* a larger one restarts */
    CHECK(output.button_duty[1] == 1000u);
    output = onset_at(input, 1u, 2060u);                 /* a smaller one is ignored */
    CHECK(output.button_duty[1] > medium_button);
}

/* Item 1: a dark control stays dark. */
static void test_flash_leaves_dark_controls_dark(void)
{
    DemoLightsInput input = idle_classic();
    DemoLightsOutput output;

    DemoLights_Init();
    input.screen = DEMO_SCREEN_MANUAL;                   /* only Encoder 3 lit */
    output = onset_at(input, 3u, 1000u);
    for (unsigned encoder = 0u; encoder < 3u; ++encoder) {
        for (unsigned channel = 0u; channel < DEMO_LIGHT_CHANNELS; ++channel) {
            CHECK(output.encoder_duty[encoder][channel] == 0u);
        }
    }
    CHECK(output.encoder_duty[3][2] == encoder_channel(1000u, 2u));

    DemoLights_Init();
    DemoLights_SetIdleLightness(0u);                     /* buttons dark at idle */
    output = onset_at(idle_classic(), 3u, 1000u);
    CHECK(output.button_duty[0] == 0u);
    CHECK(output.button_duty[1] == 0u);
}

/* Item 2: a held button stays at full, Button 0 keeps breathing and keeps its
 * fault pattern; the other button still flashes. */
static void test_flash_exempt_buttons(void)
{
    const uint32_t breath_at = 500u;
    DemoLightsInput input = idle_classic();
    DemoLightsOutput output;
    uint16_t breathing;

    DemoLights_Init();
    input.button_pressed[1] = true;
    output = onset_at(input, 1u, 1000u);
    CHECK(output.button_duty[1] == 1000u);               /* held: stays at full */
    CHECK(output.button_duty[0] > idle_duty);            /* Button 0 flashes */

    input = idle_classic();
    input.session_active = true;
    DemoLights_Init();
    output = compute(&input, breath_at);
    breathing = output.button_duty[0];
    DemoLights_Init();
    output = onset_at(input, 3u, breath_at);
    CHECK(output.button_duty[0] == breathing);           /* breathing: no flash */
    CHECK(output.button_duty[1] == 1000u);

    input = idle_classic();
    DemoLights_Init();
    DemoLights_OnSessionFault(1000u);
    output = onset_at(input, 3u, 1000u + DEMO_LIGHTS_BLINK_MS);   /* blink off phase */
    CHECK(output.button_duty[0] == 0u);
    CHECK(output.button_duty[1] == 1000u);
}

/* Item 3: no flash while Shift is held, while the prompt is open or in
 * Instrument. The onset is dropped and a flash in progress ends for good. */
static void test_flash_suppressed(void)
{
    DemoLightsInput input = idle_classic();
    DemoLightsOutput output;
    DemoLightsFlashCounters counters;

    DemoLights_Init();
    input.shift = true;
    output = onset_at(input, 3u, 1000u);
    CHECK(output.button_duty[0] == idle_duty);           /* mode switch lit, no flash */
    CHECK(output.encoder_duty[0][0] == encoder_channel(DEMO_LIGHTS_ENCODER_LIGHTNESS, 0u));
    input.shift = false;
    output = compute(&input, 1010u);                     /* dropped, not deferred */
    CHECK(output.button_duty[0] == idle_duty);

    input.screen = DEMO_SCREEN_PROMPT_START;
    output = onset_at(input, 3u, 2000u);
    CHECK(output.button_duty[1] == idle_duty);
    CHECK(output.encoder_duty[0][1] == encoder_channel(DEMO_LIGHTS_PROMPT_LIGHTNESS, 1u));

    input = idle_classic();
    input.field = false;                                 /* Instrument */
    input.screen = DEMO_SCREEN_INSTRUMENT;
    output = onset_at(input, 3u, 3000u);
    CHECK(output.button_duty[0] == idle_duty);

    input = idle_classic();                              /* a flash in progress ends */
    output = onset_at(input, 3u, 4000u);
    CHECK(output.button_duty[1] == 1000u);
    input.shift = true;
    output = compute(&input, 4020u);
    CHECK(output.button_duty[0] == idle_duty);           /* mode switch lit, no flash */
    input.shift = false;
    output = compute(&input, 4040u);
    CHECK(output.button_duty[1] == idle_duty);           /* does not come back */

    DemoLights_GetFlashCounters(&counters);
    CHECK(counters.onsets == 1u);
    CHECK(counters.suppressed == 3u);
    DemoLights_Init();
    DemoLights_GetFlashCounters(&counters);
    CHECK(counters.onsets == 0u && counters.suppressed == 0u);
}

/* Item 8: the steps and the decay are settable; bad values are refused. */
static void test_flash_settings(void)
{
    LightFlashConfig config;
    DemoLightsInput input = idle_classic();
    DemoLightsOutput output;

    DemoLights_Init();
    DemoLights_GetFlash(&config);
    CHECK(config.boost_permille[0] == 333u && config.boost_permille[2] == 1000u);
    CHECK(config.full_decay_ms == 240u);
    config.full_decay_ms = 480u;
    CHECK(DemoLights_SetFlash(&config));
    (void)onset_at(input, 3u, 1000u);
    output = compute(&input, 1240u);
    CHECK(output.button_duty[0] > idle_duty);            /* halfway */
    config.boost_permille[1] = 200u;                    /* below small */
    CHECK(!DemoLights_SetFlash(&config));
    DemoLights_GetFlash(&config);
    CHECK(config.boost_permille[1] == 667u && config.full_decay_ms == 480u);
    DemoLights_Init();
    DemoLights_GetFlash(&config);
    CHECK(config.full_decay_ms == 240u);
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
    test_encoder_levels();
    test_encoder_balance();
    test_flash_raises_lit_lights();
    test_flash_leaves_dark_controls_dark();
    test_flash_exempt_buttons();
    test_flash_suppressed();
    test_flash_settings();
    if (failures != 0u) {
        printf("%u failure(s)\n", failures);
        return 1;
    }
    printf("demo_lights_test passed\n");
    return 0;
}
