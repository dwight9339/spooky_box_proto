/* Demo-only Slicer page (CM7/App/demo_slicer.c, full_spooky_proto-p04.15;
 * decisions 0020 item 11 and 0022 items 4 to 9 and 14). */
#include "demo_slicer.h"

#include <stdio.h>
#include <string.h>

static unsigned failures;

#define CHECK(condition) do {                                               \
    if (!(condition)) {                                                     \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);       \
        ++failures;                                                         \
    }                                                                       \
} while (0)

#define CLIP_SAMPLES 72000u

static DemoSlicer slicer;
static DemoSlicerAction action;
static bool running;

static bool send(GestureKind kind, uint8_t encoder, int8_t detents)
{
    Gesture gesture;

    gesture.kind = (uint8_t)kind;
    gesture.encoder = encoder;
    gesture.detents = detents;
    gesture.reserved = 0u;
    return DemoSlicer_OnGesture(&slicer, gesture, running, &action);
}

static void setup(void)
{
    DemoSlicer_Init(&slicer);
    DemoSlicer_OnClip(&slicer, CLIP_SAMPLES);
    running = false;
}

/* First load: 16 equal slices at their defaults (0020 item 9). */
static void test_first_load(void)
{
    setup();
    CHECK(slicer.count == 16u && slicer.setup.map.count == 16u);
    CHECK(slicer.setup.map.clip_samples == CLIP_SAMPLES);
    CHECK(SliceMap_Valid(&slicer.setup.map, SLICER_MIN_SLICE_SAMPLES));
    CHECK(!DemoSlicer_Edited(&slicer));
    CHECK(slicer.setup.slices[7].gate_percent == 100u);
    CHECK(DemoSlicer_LengthMs(&slicer, 0u) == 187u); /* 4,500 samples */
}

/* The main page leaves the shell's gestures alone and takes its own. */
static void test_shell_gestures_pass(void)
{
    setup();
    CHECK(!send(GESTURE_CLICK, 0u, 0)); /* run/stop */
    CHECK(!send(GESTURE_HOLD, 1u, 0));  /* engine menu */
    CHECK(!send(GESTURE_HOLD, 2u, 0));  /* view menu */
    CHECK(send(GESTURE_CLICK, 3u, 0));  /* one page: swallowed */
    CHECK(send(GESTURE_TURN, 2u, 1));
    CHECK(!DemoSlicer_Modal(&slicer));
}

/* Encoder 0 selects; with the transport stopped a new selection auditions once
 * (0022 item 8), not while it runs or at a range end. */
static void test_select_and_audition(void)
{
    setup();
    CHECK(send(GESTURE_TURN, 0u, 3) && slicer.selected == 3u);
    CHECK(action.audition && action.audition_slice == 3u);
    CHECK(send(GESTURE_TURN, 0u, -9) && slicer.selected == 0u && action.audition);
    CHECK(send(GESTURE_TURN, 0u, -1) && slicer.selected == 0u && !action.audition);
    CHECK(send(GESTURE_TURN, 0u, 40) && slicer.selected == 15u);
    running = true;
    CHECK(send(GESTURE_TURN, 0u, -1) && slicer.selected == 14u && !action.audition);
}

/* Encoder 0 hold opens the slice: pitch, gate, level and length on Encoders 0
 * to 3; click closes; Encoder 2 click runs or stops (user call 2026-10-07). */
static void test_slice_parameters(void)
{
    setup();
    send(GESTURE_TURN, 0u, 2);
    CHECK(send(GESTURE_HOLD, 0u, 0) && DemoSlicer_Modal(&slicer));
    CHECK(send(GESTURE_TURN, 0u, 5) && action.setup_changed);
    CHECK(slicer.setup.slices[2].pitch_semitones == 5);
    CHECK(send(GESTURE_TURN, 0u, 20) && slicer.setup.slices[2].pitch_semitones == SLICER_PITCH_MAX);
    CHECK(send(GESTURE_TURN, 0u, 1) && !action.setup_changed); /* at the end */
    CHECK(send(GESTURE_TURN, 1u, -3) && slicer.setup.slices[2].gate_percent == 85u);
    CHECK(send(GESTURE_TURN, 1u, -40) && slicer.setup.slices[2].gate_percent == SLICER_GATE_MIN);
    CHECK(send(GESTURE_TURN, 2u, -4) && slicer.setup.slices[2].level_percent == 60u);
    CHECK(send(GESTURE_TURN, 2u, 10) && slicer.setup.slices[2].level_percent == 100u);
    /* Length: 10 ms (240 samples) a detent; the next slice gives way. */
    CHECK(send(GESTURE_TURN, 3u, 2) && action.setup_changed);
    CHECK(SliceMap_Length(&slicer.setup.map, 2u) == 4500u + 480u);
    CHECK(SliceMap_Length(&slicer.setup.map, 3u) == 4500u - 480u);
    CHECK(send(GESTURE_TURN, 3u, -100));
    CHECK(SliceMap_Length(&slicer.setup.map, 2u) == SLICER_MIN_SLICE_SAMPLES);
    CHECK(SliceMap_Valid(&slicer.setup.map, SLICER_MIN_SLICE_SAMPLES));
    /* Other slices are untouched. */
    CHECK(slicer.setup.slices[1].pitch_semitones == 0 && slicer.setup.slices[3].gate_percent == 100u);
    /* Swallowed, run/stop on Encoder 2, closed by Encoder 0 click. */
    CHECK(send(GESTURE_HOLD, 1u, 0) && DemoSlicer_Modal(&slicer));
    CHECK(send(GESTURE_CLICK, 2u, 0) && action.toggle_run);
    CHECK(send(GESTURE_CLICK, 0u, 0) && !action.toggle_run && !DemoSlicer_Modal(&slicer));
    CHECK(DemoSlicer_Edited(&slicer));
}

/* The last slice's end is the clip end. */
static void test_last_slice_length(void)
{
    setup();
    send(GESTURE_TURN, 0u, 15);
    send(GESTURE_HOLD, 0u, 0);
    CHECK(send(GESTURE_TURN, 3u, -3) && !action.setup_changed);
    CHECK(SliceMap_End(&slicer.setup.map, 15u) == CLIP_SAMPLES);
}

/* The slice count: turn to choose, click to apply (0022 items 4 and 9); unedited
 * it applies at once and asks for a remap from the old map. */
static void test_count_change(void)
{
    setup();
    send(GESTURE_TURN, 0u, 9);
    CHECK(send(GESTURE_TURN, 1u, -1) && slicer.count_choice == 8u && slicer.count == 16u);
    CHECK(send(GESTURE_TURN, 1u, -5) && slicer.count_choice == 4u);
    CHECK(send(GESTURE_CLICK, 1u, 0));
    CHECK(action.count_changed && action.setup_changed);
    CHECK(action.old_map.count == 16u && slicer.count == 4u && slicer.setup.map.count == 4u);
    CHECK(slicer.selected == 2u); /* slice 9 started at 40,500: in 36,000..54,000 */
    CHECK(SliceMap_Remap(&action.old_map, 9u, &slicer.setup.map) == 2u);
    /* Nothing chosen: the click does nothing. */
    CHECK(send(GESTURE_CLICK, 1u, 0) && !action.count_changed);
    CHECK(DemoSlicer_NextCount(16u, 1) == 16u && DemoSlicer_NextCount(4u, -1) == 4u);
    CHECK(DemoSlicer_NextCount(8u, 1) == 16u && DemoSlicer_NextCount(8u, -1) == 4u);
}

/* After edits the page asks first: a second click discards them, anything else
 * cancels (0022 item 9). */
static void test_count_change_confirm(void)
{
    setup();
    send(GESTURE_HOLD, 0u, 0);
    send(GESTURE_TURN, 2u, -2);
    send(GESTURE_CLICK, 0u, 0);
    CHECK(DemoSlicer_Edited(&slicer));

    send(GESTURE_TURN, 1u, -1);
    CHECK(send(GESTURE_CLICK, 1u, 0) && !action.count_changed);
    CHECK(slicer.focus == DEMO_SLICER_CONFIRM && DemoSlicer_Modal(&slicer));
    CHECK(send(GESTURE_CLICK, 0u, 0) && !action.toggle_run); /* cancels, swallowed */
    CHECK(slicer.focus == DEMO_SLICER_BROWSE && slicer.count == 16u && slicer.count_choice == 16u);
    CHECK(slicer.setup.slices[0].level_percent == 70u);

    send(GESTURE_TURN, 1u, -1);
    send(GESTURE_CLICK, 1u, 0);
    CHECK(send(GESTURE_CLICK, 1u, 0) && action.count_changed);
    CHECK(slicer.count == 8u && !DemoSlicer_Edited(&slicer));
    CHECK(slicer.setup.slices[0].level_percent == 80u);
}

/* After a count change each slice fires on the step where its time begins and
 * the steps between are off, so a round trip gives the identity back (user call
 * 2026-10-07, p04.15 bench: 0022 item 11's remap made 16, 4, 16 play as 4). */
static void test_clip_pattern(void)
{
    StepPattern pattern;
    unsigned step;

    StepPattern_InitSweep(&pattern, 16u);
    setup();
    send(GESTURE_TURN, 1u, -2);
    CHECK(send(GESTURE_CLICK, 1u, 0) && action.count_changed && slicer.count == 4u);
    DemoSlicer_ClipPattern(&slicer.setup.map, &pattern);
    for (step = 0u; step < 16u; ++step) {
        CHECK(pattern.steps[step].value == step / 4u);
        CHECK(pattern.steps[step].on == ((step % 4u) == 0u));
    }

    send(GESTURE_TURN, 1u, 1);
    CHECK(send(GESTURE_CLICK, 1u, 0) && action.count_changed && slicer.count == 8u);
    DemoSlicer_ClipPattern(&slicer.setup.map, &pattern);
    for (step = 0u; step < 16u; ++step) {
        CHECK(pattern.steps[step].value == step / 2u);
        CHECK(pattern.steps[step].on == ((step % 2u) == 0u));
    }

    send(GESTURE_TURN, 1u, 1);
    CHECK(send(GESTURE_CLICK, 1u, 0) && action.count_changed && slicer.count == 16u);
    DemoSlicer_ClipPattern(&slicer.setup.map, &pattern);
    for (step = 0u; step < 16u; ++step) {
        CHECK(pattern.steps[step].value == step && pattern.steps[step].on);
    }

    /* A shorter pattern spreads the clip over its own steps; the rest is kept. */
    pattern.length = 8u;
    pattern.steps[12].value = 3u;
    DemoSlicer_ClipPattern(&slicer.setup.map, &pattern);
    for (step = 0u; step < 8u; ++step) {
        CHECK(pattern.steps[step].value == 2u * step && pattern.steps[step].on);
    }
    CHECK(pattern.steps[12].value == 3u);
    /* No clip: nothing changes. */
    DemoSlicer_Init(&slicer);
    DemoSlicer_ClipPattern(&slicer.setup.map, &pattern);
    CHECK(pattern.steps[1].value == 2u);
}

/* A new clip keeps count and parameters and makes the boundaries equal again
 * (0022 item 14); Reset drops a half-chosen count and closes the slice. */
static void test_new_clip_and_reset(void)
{
    setup();
    send(GESTURE_TURN, 1u, -1);
    send(GESTURE_CLICK, 1u, 0); /* 8 slices */
    send(GESTURE_TURN, 0u, 7);
    send(GESTURE_HOLD, 0u, 0);
    send(GESTURE_TURN, 0u, -3);
    send(GESTURE_TURN, 3u, -5);
    CHECK(SliceMap_Length(&slicer.setup.map, 7u) == CLIP_SAMPLES / 8u); /* last: fixed */
    DemoSlicer_OnClip(&slicer, 120000u);
    CHECK(!DemoSlicer_Modal(&slicer));
    CHECK(slicer.count == 8u && slicer.setup.map.count == 8u);
    CHECK(slicer.setup.map.clip_samples == 120000u && SliceMap_Length(&slicer.setup.map, 0u) == 15000u);
    CHECK(slicer.setup.slices[7].pitch_semitones == -3);
    CHECK(slicer.selected == 7u);

    send(GESTURE_TURN, 1u, 1);
    CHECK(slicer.count_choice == 16u);
    send(GESTURE_HOLD, 0u, 0);
    DemoSlicer_Reset(&slicer);
    CHECK(slicer.count_choice == 8u && slicer.focus == DEMO_SLICER_BROWSE);
}

int main(void)
{
    test_first_load();
    test_shell_gestures_pass();
    test_select_and_audition();
    test_slice_parameters();
    test_last_slice_length();
    test_count_change();
    test_count_change_confirm();
    test_clip_pattern();
    test_new_clip_and_reset();
    if (failures != 0u) {
        printf("%u failure(s)\n", failures);
        return 1;
    }
    printf("demo_slicer_test: all passed\n");
    return 0;
}
