#include "ui_input_service.h"
#include "ui_render_service.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static unsigned failures;

#define CHECK(condition) do {                                               \
    if (!(condition)) {                                                     \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);       \
        ++failures;                                                         \
    }                                                                       \
} while (0)

static void input_debounces_and_decodes_without_gestures(void)
{
    bool switches[UI_INPUT_SWITCH_COUNT] = {false};
    uint8_t ab[UI_INPUT_ENCODER_COUNT] = {0u};
    UiInputSwitchEvents switch_events;
    UiInputEncoderEvents encoder_events[UI_INPUT_ENCODER_COUNT];
    UiInputStatus status;
    static const uint8_t clockwise[] = {2u, 3u, 1u, 0u};
    unsigned index;

    CHECK(UiInputService_Init(switches, ab, 100u));
    switches[2] = true;
    UiInputService_UpdateSwitches(switches, 101u, &switch_events);
    CHECK(switch_events.high_mask == 0u);
    UiInputService_UpdateSwitches(switches, 115u, &switch_events);
    CHECK(switch_events.high_mask == 0u);
    UiInputService_UpdateSwitches(switches, 116u, &switch_events);
    CHECK(switch_events.high_mask == (1u << 2));

    for (index = 0u; index < sizeof(clockwise); ++index) {
        ab[0] = clockwise[index];
        UiInputService_Tick1ms(ab);
    }
    UiInputService_TakeEncoderEvents(encoder_events);
    CHECK(encoder_events[0].clockwise == 1u);
    CHECK(encoder_events[0].counterclockwise == 0u);
    CHECK(encoder_events[0].count == 1);
    UiInputService_TakeEncoderEvents(encoder_events);
    CHECK(encoder_events[0].clockwise == 0u);

    ab[1] = 3u; /* 00 -> 11 is invalid. */
    UiInputService_Tick1ms(ab);
    CHECK(UiInputService_GetStatus(ab, &status));
    CHECK(status.invalid_transitions[1] == 1u);
    CHECK(status.stable_switches[2]);
}

static void render_work_is_bounded_and_repeatable(void)
{
    UiRenderMatrixFrame frame;
    UiRenderStatus status;
    const uint8_t *display;
    uint8_t led;
    bool finished;
    uint32_t now = 0u;
    unsigned index;

    UiRenderService_Init();
    UiRenderService_StartLedChase(now);
    CHECK(!UiRenderService_NextLed(349u, &led, &finished));
    for (index = 1u; index < UI_RENDER_LED_COUNT; ++index) {
        now += 350u;
        CHECK(UiRenderService_NextLed(now, &led, &finished));
        CHECK(led == index);
        CHECK(!finished);
    }
    now += 350u;
    CHECK(UiRenderService_NextLed(now, &led, &finished));
    CHECK(finished);

    UiRenderService_StartMatrixAnimation(1000u);
    CHECK(UiRenderService_PrepareMatrixFrame(1000u, &frame));
    CHECK(frame.phase == 0u);
    CHECK(frame.pixel_count == 1u);
    CHECK(frame.pixels[0].x == 0u && frame.pixels[0].y == 0u);
    CHECK(frame.pixels[0].red == 192u);
    UiRenderService_CommitMatrixFrame(1000u, true);
    CHECK(!UiRenderService_PrepareMatrixFrame(1069u, &frame));
    CHECK(UiRenderService_PrepareMatrixFrame(1070u, &frame));
    CHECK(frame.phase == 1u);
    CHECK(frame.pixel_count <= UI_RENDER_MATRIX_MAX_FRAME_PIXELS);
    UiRenderService_CommitMatrixFrame(1070u, false);
    CHECK(UiRenderService_GetStatus(&status));
    CHECK(!status.matrix_animation_active);

    now = 2000u;
    UiRenderService_StartMatrixAnimation(now);
    for (index = 0u;
         index < UI_RENDER_MATRIX_PIXEL_COUNT + 4u;
         ++index) {
        CHECK(UiRenderService_PrepareMatrixFrame(now, &frame));
        CHECK(frame.phase == index);
        CHECK(frame.pixel_count <= UI_RENDER_MATRIX_MAX_FRAME_PIXELS);
        CHECK(frame.final_frame ==
              (index == UI_RENDER_MATRIX_PIXEL_COUNT + 3u));
        UiRenderService_CommitMatrixFrame(now, true);
        now += UI_RENDER_MATRIX_FRAME_MS;
    }
    CHECK(UiRenderService_GetStatus(&status));
    CHECK(!status.matrix_animation_active);

    display = UiRenderService_BuildDisplayTestPattern();
    CHECK(display != NULL);
    CHECK((display[0] & 0x01u) != 0u);
    CHECK((display[127] & 0x01u) != 0u);
    display = UiRenderService_BuildDisplayTestPattern();
    CHECK((display[0] & 0x01u) != 0u);
}

int main(void)
{
    input_debounces_and_decodes_without_gestures();
    render_work_is_bounded_and_repeatable();
    if (failures != 0u) {
        printf("%u failure(s)\n", failures);
        return 1;
    }
    puts("ui services tests passed");
    return 0;
}
