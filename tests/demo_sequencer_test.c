/* Demo-only Instrument views and step view (CM7/App/demo_sequencer.c,
 * full_spooky_proto-p04.14), and the engine menu (p04.15). */
#include "demo_sequencer.h"

#include <stdio.h>
#include <string.h>

static unsigned failures;

#define CHECK(condition) do {                                               \
    if (!(condition)) {                                                     \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);       \
        ++failures;                                                         \
    }                                                                       \
} while (0)

static DemoSequencer seq;
static StepPattern pattern;
static DemoSeqTarget target;

static Gesture make(GestureKind kind, uint8_t encoder, int8_t detents)
{
    Gesture gesture;

    gesture.kind = (uint8_t)kind;
    gesture.encoder = encoder;
    gesture.detents = detents;
    gesture.reserved = 0u;
    return gesture;
}

static bool send(GestureKind kind, uint8_t encoder, int8_t detents)
{
    target.toggle_run = false;
    target.tempo_changed = false;
    target.engine_chosen = false;
    return DemoSequencer_OnGesture(&seq, make(kind, encoder, detents), &target, 1000u);
}

static void setup(void)
{
    DemoSequencer_Init(&seq);
    StepPattern_InitSweep(&pattern, 1000u);
    target.pattern = &pattern;
    target.read_only = false;
    target.engine = DEMO_SEQ_ENGINE_GRANULAR;
    target.tempo_x100 = 12000u;
}

/* The main page takes only the Encoder 0 click (run/stop) and the long Encoder 1
 * and Encoder 2 presses (engine and view menus); turns and the other clicks stay
 * with the pages. */
static void test_engine_page(void)
{
    setup();
    CHECK(seq.view == DEMO_SEQ_VIEW_ENGINE);
    CHECK(!send(GESTURE_TURN, 0u, 3));
    CHECK(!send(GESTURE_CLICK, 3u, 0));
    CHECK(!send(GESTURE_CLICK, 2u, 0));
    CHECK(!send(GESTURE_HOLD, 0u, 0));
    CHECK(!send(GESTURE_CLICK, 1u, 0));
    CHECK(send(GESTURE_CLICK, 0u, 0) && target.toggle_run);
    CHECK(send(GESTURE_HOLD, 2u, 0));
    CHECK(seq.view == DEMO_SEQ_VIEW_MENU && seq.item == DEMO_SEQ_ITEM_ENGINE);
}

/* The menu (decision 0009 items 16 to 18): Encoder 0 scrolls, its click selects,
 * an Encoder 1 click closes, everything else is swallowed, and it times out. */
static void test_menu(void)
{
    setup();
    send(GESTURE_HOLD, 2u, 0);
    CHECK(send(GESTURE_TURN, 1u, 1) && seq.item == DEMO_SEQ_ITEM_ENGINE);
    CHECK(send(GESTURE_CLICK, 2u, 0) && !target.toggle_run);
    CHECK(send(GESTURE_TURN, 0u, 5) && seq.item == DEMO_SEQ_ITEM_SEQUENCER);
    CHECK(send(GESTURE_CLICK, 1u, 0) && seq.view == DEMO_SEQ_VIEW_ENGINE); /* closed */
    send(GESTURE_HOLD, 2u, 0);
    send(GESTURE_TURN, 0u, 1);
    CHECK(send(GESTURE_CLICK, 0u, 0) && seq.view == DEMO_SEQ_VIEW_STEPS);
    CHECK(seq.focus == DEMO_SEQ_FOCUS_STEPS);
    /* From the step view, Encoder 3 opens the menu on SEQUENCER; ENGINE returns. */
    CHECK(send(GESTURE_CLICK, 3u, 0) && seq.view == DEMO_SEQ_VIEW_MENU);
    CHECK(seq.item == DEMO_SEQ_ITEM_SEQUENCER);
    CHECK(send(GESTURE_TURN, 0u, -1) && seq.item == DEMO_SEQ_ITEM_ENGINE);
    CHECK(send(GESTURE_CLICK, 0u, 0) && seq.view == DEMO_SEQ_VIEW_ENGINE);
    /* The timeout closes the menu to where it was opened from. */
    send(GESTURE_HOLD, 2u, 0); /* at 1,000 ms */
    CHECK(!DemoSequencer_Tick(&seq, 1000u + DEMO_SEQ_MENU_TIMEOUT_MS - 1u));
    CHECK(seq.view == DEMO_SEQ_VIEW_MENU);
    CHECK(DemoSequencer_Tick(&seq, 1000u + DEMO_SEQ_MENU_TIMEOUT_MS));
    CHECK(seq.view == DEMO_SEQ_VIEW_ENGINE);
    CHECK(!DemoSequencer_Tick(&seq, 99999u));
}

static void open_steps(void)
{
    setup();
    send(GESTURE_HOLD, 2u, 0);
    send(GESTURE_TURN, 0u, 1);
    send(GESTURE_CLICK, 0u, 0);
}

/* C-072 to C-076: browse, edit the value at once, toggle, and return. */
static void test_steps(void)
{
    open_steps();
    CHECK(send(GESTURE_TURN, 0u, 3) && seq.step == 3u);
    CHECK(send(GESTURE_TURN, 0u, 40) && seq.step == 15u);
    CHECK(send(GESTURE_TURN, 0u, -40) && seq.step == 0u);
    send(GESTURE_TURN, 0u, 2);
    CHECK(send(GESTURE_TURN, 1u, 4) && pattern.steps[2].value == 125u); /* swallowed */
    CHECK(send(GESTURE_CLICK, 0u, 0) && seq.focus == DEMO_SEQ_FOCUS_STEP_EDIT);
    CHECK(send(GESTURE_TURN, 0u, 5) && pattern.steps[2].value == 175u);
    CHECK(send(GESTURE_TURN, 0u, -100) && pattern.steps[2].value == 0u);
    CHECK(send(GESTURE_TURN, 0u, 127) && pattern.steps[2].value == DEMO_SEQ_VALUE_MAX);
    CHECK(send(GESTURE_CLICK, 1u, 0) && !pattern.steps[2].on);
    CHECK(send(GESTURE_CLICK, 1u, 0) && pattern.steps[2].on);
    CHECK(send(GESTURE_CLICK, 2u, 0) && target.toggle_run); /* run/stop from the edit */
    CHECK(seq.focus == DEMO_SEQ_FOCUS_STEP_EDIT);
    CHECK(send(GESTURE_CLICK, 0u, 0) && seq.focus == DEMO_SEQ_FOCUS_STEPS);
    pattern.length = 6u;
    send(GESTURE_TURN, 0u, 20);
    CHECK(seq.step == 5u);
}

/* C-077 to C-082 and 0026 item 8: settings row, live edits, keep or restore. */
static void test_settings(void)
{
    open_steps();
    CHECK(send(GESTURE_HOLD, 0u, 0) && seq.focus == DEMO_SEQ_FOCUS_SETTINGS);
    CHECK(seq.setting == DEMO_SEQ_SETTING_TEMPO);
    CHECK(send(GESTURE_CLICK, 0u, 0) && seq.focus == DEMO_SEQ_FOCUS_SETTING_EDIT);
    CHECK(send(GESTURE_TURN, 0u, -24) && target.tempo_changed && target.tempo_x100 == 9600u);
    CHECK(send(GESTURE_TURN, 0u, -100) && target.tempo_x100 == TRANSPORT_BPM_MIN_X100);
    CHECK(send(GESTURE_CLICK, 1u, 0) && target.tempo_changed); /* restore */
    CHECK(target.tempo_x100 == 12000u && seq.focus == DEMO_SEQ_FOCUS_SETTINGS);
    send(GESTURE_CLICK, 0u, 0);
    send(GESTURE_TURN, 0u, 100);
    CHECK(target.tempo_x100 == TRANSPORT_BPM_MAX_X100);
    CHECK(send(GESTURE_CLICK, 0u, 0) && !target.tempo_changed); /* keep */
    CHECK(target.tempo_x100 == TRANSPORT_BPM_MAX_X100);
    CHECK(send(GESTURE_TURN, 0u, 1) && seq.setting == DEMO_SEQ_SETTING_DIVISION);
    send(GESTURE_CLICK, 0u, 0);
    CHECK(send(GESTURE_TURN, 0u, 1) && pattern.steps_per_beat == 2u);
    CHECK(send(GESTURE_TURN, 0u, 5) && pattern.steps_per_beat == 1u);
    CHECK(send(GESTURE_CLICK, 1u, 0) && pattern.steps_per_beat == 4u); /* restore */
    send(GESTURE_CLICK, 0u, 0);
    send(GESTURE_TURN, 0u, 1);
    CHECK(send(GESTURE_CLICK, 3u, 0) && seq.view == DEMO_SEQ_VIEW_MENU); /* kept */
    CHECK(pattern.steps_per_beat == 2u && seq.focus == DEMO_SEQ_FOCUS_SETTINGS);
    send(GESTURE_TURN, 0u, 1);
    send(GESTURE_CLICK, 0u, 0); /* back to the step view, focus unchanged */
    CHECK(seq.view == DEMO_SEQ_VIEW_STEPS && seq.focus == DEMO_SEQ_FOCUS_SETTINGS);
    CHECK(send(GESTURE_HOLD, 0u, 0) && seq.focus == DEMO_SEQ_FOCUS_STEPS);
}

/* Entering Instrument lands on the main page (0027 item 4), keeping the
 * selected step. */
/* The engine menu (C-018 to C-020 in the menu form, user call 2026-10-07):
 * Encoder 1 hold opens it on the current engine, Encoder 0 scrolls, its click
 * commits and lands on the main page, an Encoder 1 click closes, and it times
 * out. It does not open from the step view. */
static void test_engine_menu(void)
{
    setup();
    CHECK(send(GESTURE_HOLD, 1u, 0));
    CHECK(seq.view == DEMO_SEQ_VIEW_ENGINE_MENU && seq.engine_item == DEMO_SEQ_ENGINE_GRANULAR);
    CHECK(send(GESTURE_TURN, 0u, 4) && seq.engine_item == DEMO_SEQ_ENGINE_SLICER);
    CHECK(send(GESTURE_CLICK, 2u, 0) && !target.toggle_run); /* swallowed */
    CHECK(send(GESTURE_CLICK, 0u, 0));
    CHECK(target.engine_chosen && target.engine == DEMO_SEQ_ENGINE_SLICER);
    CHECK(seq.view == DEMO_SEQ_VIEW_ENGINE);

    /* Committing the engine already running changes nothing. */
    CHECK(send(GESTURE_HOLD, 1u, 0) && seq.engine_item == DEMO_SEQ_ENGINE_SLICER);
    CHECK(send(GESTURE_CLICK, 0u, 0) && !target.engine_chosen);

    /* Encoder 1 click closes without a change. */
    CHECK(send(GESTURE_HOLD, 1u, 0));
    CHECK(send(GESTURE_TURN, 0u, -1));
    CHECK(send(GESTURE_CLICK, 1u, 0) && !target.engine_chosen && seq.view == DEMO_SEQ_VIEW_ENGINE);

    /* Times out to the main page. */
    CHECK(send(GESTURE_HOLD, 1u, 0));
    CHECK(!DemoSequencer_Tick(&seq, 1000u + DEMO_SEQ_MENU_TIMEOUT_MS - 1u));
    CHECK(DemoSequencer_Tick(&seq, 1000u + DEMO_SEQ_MENU_TIMEOUT_MS));
    CHECK(seq.view == DEMO_SEQ_VIEW_ENGINE);

    /* Not from the step view: there the hold is swallowed. */
    send(GESTURE_HOLD, 2u, 0);
    send(GESTURE_TURN, 0u, 1);
    send(GESTURE_CLICK, 0u, 0);
    CHECK(seq.view == DEMO_SEQ_VIEW_STEPS);
    CHECK(send(GESTURE_HOLD, 1u, 0) && seq.view == DEMO_SEQ_VIEW_STEPS);
    CHECK(strcmp(DemoSequencer_EngineName(DEMO_SEQ_ENGINE_SLICER), "SLICER") == 0);
    CHECK(strcmp(DemoSequencer_EngineName(9u), "?") == 0);
}

/* A read-only pattern (the Slicer's until p04.16) browses but does not edit;
 * the settings row still edits. */
static void test_read_only(void)
{
    setup();
    target.read_only = true;
    send(GESTURE_HOLD, 2u, 0);
    send(GESTURE_TURN, 0u, 1);
    send(GESTURE_CLICK, 0u, 0);
    CHECK(seq.view == DEMO_SEQ_VIEW_STEPS);
    CHECK(send(GESTURE_TURN, 0u, 2) && seq.step == 2u);
    CHECK(send(GESTURE_CLICK, 0u, 0) && seq.focus == DEMO_SEQ_FOCUS_STEPS);
    CHECK(send(GESTURE_CLICK, 1u, 0) && pattern.steps[2].on);
    CHECK(send(GESTURE_HOLD, 0u, 0) && seq.focus == DEMO_SEQ_FOCUS_SETTINGS);
    CHECK(send(GESTURE_CLICK, 0u, 0) && seq.focus == DEMO_SEQ_FOCUS_SETTING_EDIT);
    CHECK(send(GESTURE_TURN, 0u, 3) && target.tempo_changed && target.tempo_x100 == 12300u);
}

static void test_reset(void)
{
    open_steps();
    send(GESTURE_TURN, 0u, 4);
    DemoSequencer_Reset(&seq);
    CHECK(seq.view == DEMO_SEQ_VIEW_ENGINE && seq.step == 4u);
    CHECK(!DemoSequencer_OnGesture(NULL, make(GESTURE_CLICK, 0u, 0), &target, 0u));
    CHECK(strcmp(DemoSequencer_DivisionName(2u), "1/8") == 0);
    CHECK(strcmp(DemoSequencer_ItemName(DEMO_SEQ_ITEM_SEQUENCER), "SEQUENCER") == 0);
    CHECK(strcmp(DemoSequencer_SettingName(DEMO_SEQ_SETTING_DIVISION), "DIV") == 0);
}

int main(void)
{
    test_engine_page();
    test_menu();
    test_steps();
    test_settings();
    test_engine_menu();
    test_read_only();
    test_reset();
    if (failures != 0u) {
        printf("%u failure(s)\n", failures);
        return 1;
    }
    printf("demo_sequencer_test passed\n");
    return 0;
}
