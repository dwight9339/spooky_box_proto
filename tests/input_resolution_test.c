/*
 * Host scenario tests for the InputResolution machine
 * (docs/design/behavior/InputResolutionSm.puml; decision 0005 items 1-9 and
 * decision 0009 items 1-7, 21-37, P1-P15). The generated machine and the real port
 * run against a fake integration that records every call. Host results only; not
 * hardware evidence.
 */

#include <stdio.h>
#include <string.h>

#include "input_resolution_port.h"

/* --- Fake integration ---------------------------------------------------------- */

#define LOG_CAPACITY 128u

typedef struct Fake {
    bool on_page;
    bool session_active;
    uint32_t now;
    unsigned release_alls;
    unsigned gesture_count;
    Gesture gestures[LOG_CAPACITY];
    unsigned command_count;
    InpCommand commands[LOG_CAPACITY];
    unsigned published_count;
    InpPublished published[LOG_CAPACITY];
} Fake;

static Fake fake;

void inp_integration_emit(Gesture gesture)
{
    if (fake.gesture_count < LOG_CAPACITY) {
        fake.gestures[fake.gesture_count] = gesture;
    }
    ++fake.gesture_count;
}

void inp_integration_release_all(void) { ++fake.release_alls; }
bool inp_integration_on_page(void) { return fake.on_page; }
bool inp_integration_session_active(void) { return fake.session_active; }
uint32_t inp_integration_now_ms(void) { return fake.now; }

void inp_integration_issue(InpCommand command)
{
    if (fake.command_count < LOG_CAPACITY) {
        fake.commands[fake.command_count] = command;
    }
    ++fake.command_count;
}

void inp_integration_publish(InpPublished event)
{
    if (fake.published_count < LOG_CAPACITY) {
        fake.published[fake.published_count] = event;
    }
    ++fake.published_count;
}

/* --- Helpers ------------------------------------------------------------------- */

static int failures;
static const char *current_test;

#define CHECK(cond)                                                                   \
    do {                                                                              \
        if (!(cond)) {                                                                \
            printf("FAIL %s (line %d): %s\n", current_test, __LINE__, #cond);        \
            ++failures;                                                               \
        }                                                                             \
    } while (0)

static uint32_t sequence;

static void reset(void)
{
    memset(&fake, 0, sizeof(fake));
    fake.on_page = true;
    fake.now = 1000u;
    sequence = 0;
    InputResolution_Init();
}

static void input(InpInputKind kind, uint8_t index, int8_t detents)
{
    const InpInput event = {++sequence, fake.now, (uint8_t)kind, index, detents, 0};
    InputResolution_OnInput(&event);
}

static void press(InpControl control) { input(INP_INPUT_PRESS, (uint8_t)control, 0); }
static void release(InpControl control) { input(INP_INPUT_RELEASE, (uint8_t)control, 0); }
static void turn(uint8_t encoder, int8_t detents) { input(INP_INPUT_DETENTS, encoder, detents); }

static void advance(uint32_t ms)
{
    fake.now += ms;
    InputResolution_OnTick();
}

static void hold_threshold(void) { advance(INP_DEFAULT_HOLD_MS); }
static void session_threshold(void) { advance(INP_DEFAULT_SESSION_HOLD_MS); }

static InpStatus status(void)
{
    InpStatus s;
    InputResolution_GetStatus(&s);
    return s;
}

static uint8_t state(void) { return status().state; }

static bool last_published_is(InpPublished event)
{
    return fake.published_count > 0 && fake.published[fake.published_count - 1] == event;
}

static unsigned count_published(InpPublished event)
{
    unsigned n = 0;
    for (unsigned i = 0; i < fake.published_count && i < LOG_CAPACITY; ++i) {
        n += fake.published[i] == event ? 1u : 0u;
    }
    return n;
}

static bool gesture_is(unsigned index, GestureKind kind, uint8_t encoder)
{
    return index < fake.gesture_count && fake.gestures[index].kind == kind &&
           fake.gestures[index].encoder == encoder;
}

static bool only_gesture_is(GestureKind kind, uint8_t encoder)
{
    return fake.gesture_count == 1u && gesture_is(0, kind, encoder);
}

/* Reach each session-hold state from Neutral. */
static void to_pending(void) { press(INP_BUTTON0); }
static void to_prompt(bool session_active)
{
    fake.session_active = session_active;
    press(INP_BUTTON0);
    session_threshold();
}
static void to_consumed(void)
{
    to_prompt(false);
    press(INP_ENC0_BUTTON);
}

/* --- Script runner for the chord permutations ---------------------------------- */

/* Steps: "E3d", "E3u", "E3h" (press and hold past the threshold), "B0d", "B0u",
 * "B1d", "B1u", "E1d", "E1u", "R" (reconcile). */
static void run_script(const char *script)
{
    char token[4];
    const char *p = script;
    while (*p != '\0') {
        while (*p == ' ') {
            ++p;
        }
        size_t n = 0;
        while (p[n] != '\0' && p[n] != ' ' && n < 3) {
            token[n] = p[n];
            ++n;
        }
        token[n] = '\0';
        p += n;
        if (n == 0) {
            break;
        }
        if (strcmp(token, "R") == 0) {
            InputResolution_OnReconcile();
            continue;
        }
        InpControl control = INP_BUTTON0;
        if (token[0] == 'B') {
            control = token[1] == '0' ? INP_BUTTON0 : INP_BUTTON1;
        } else {
            control = (InpControl)(INP_ENC0_BUTTON + (token[1] - '0'));
        }
        if (token[2] == 'd') {
            press(control);
        } else if (token[2] == 'u') {
            release(control);
        } else {
            press(control);
            hold_threshold();
        }
        advance(10u); /* humans are slower than one loop pass */
    }
}

typedef struct ChordCase {
    const char *name;
    const char *script;       /* after Encoder 3 is down */
    GestureKind expected[3];
    unsigned expected_count;
} ChordCase;

static void check_gestures(const char *name, const char *variant, const GestureKind *expected,
                           unsigned expected_count)
{
    bool ok = fake.gesture_count == expected_count;
    for (unsigned i = 0; ok && i < expected_count; ++i) {
        ok = fake.gestures[i].kind == (uint8_t)expected[i];
    }
    if (!ok) {
        printf("FAIL %s: %s (%s): %u gestures:", current_test, name, variant, fake.gesture_count);
        for (unsigned i = 0; i < fake.gesture_count && i < LOG_CAPACITY; ++i) {
            printf(" %u", (unsigned)fake.gestures[i].kind);
        }
        printf("\n");
        ++failures;
    }
}

/* --- Clicks, holds and push-turn (decision 0009 items 1-6) --------------------- */

static void click_fires_on_release_before_the_threshold(void)
{
    reset();
    press(INP_ENC0_BUTTON);
    CHECK(fake.gesture_count == 0);
    advance(INP_DEFAULT_HOLD_MS - 1u);
    release(INP_ENC0_BUTTON);
    CHECK(only_gesture_is(GESTURE_CLICK, 0));
}

static void hold_fires_at_the_threshold_and_its_release_is_swallowed(void)
{
    reset();
    press(INP_ENC1_BUTTON);
    hold_threshold();
    CHECK(only_gesture_is(GESTURE_HOLD, 1));
    advance(1000u);
    release(INP_ENC1_BUTTON);
    CHECK(fake.gesture_count == 1);
    CHECK(status().swallowed == 1);
}

static void a_due_threshold_resolves_before_a_late_release(void)
{
    /* No tick ran between the threshold and the release: the hold still wins. */
    reset();
    press(INP_ENC2_BUTTON);
    fake.now += INP_DEFAULT_HOLD_MS + 50u;
    CHECK(InputResolution_TickDue(fake.now));
    release(INP_ENC2_BUTTON);
    CHECK(only_gesture_is(GESTURE_HOLD, 2));
    CHECK(!InputResolution_TickDue(fake.now));
}

static void push_turn_cancels_the_click_and_the_hold(void)
{
    reset();
    press(INP_ENC0_BUTTON);
    turn(0, 1);
    CHECK(only_gesture_is(GESTURE_TURN, 0) && fake.gestures[0].detents == 1);
    hold_threshold();
    release(INP_ENC0_BUTTON);
    CHECK(fake.gesture_count == 1);
}

static void turning_another_encoder_keeps_a_pending_click(void)
{
    reset();
    press(INP_ENC0_BUTTON);
    turn(1, -2);
    release(INP_ENC0_BUTTON);
    CHECK(fake.gesture_count == 2);
    CHECK(gesture_is(0, GESTURE_TURN, 1) && fake.gestures[0].detents == -2);
    CHECK(gesture_is(1, GESTURE_CLICK, 0));
}

static void button1_is_delivered_from_its_press(void)
{
    reset();
    press(INP_BUTTON1);
    CHECK(only_gesture_is(GESTURE_BUTTON1_DOWN, 0));
    hold_threshold();
    release(INP_BUTTON1);
    CHECK(fake.gesture_count == 2 && gesture_is(1, GESTURE_BUTTON1_UP, 0));
}

static void a_hold_cancels_other_pending_presses(void)
{
    /* Encoder 1 reaches its threshold while Encoder 3 is pending: the Encoder 3
     * press is consumed, with neither a page advance nor Shift (item 25). */
    reset();
    press(INP_ENC1_BUTTON);
    advance(100u);
    press(INP_ENC3_BUTTON);
    advance(INP_DEFAULT_HOLD_MS - 100u);
    CHECK(only_gesture_is(GESTURE_HOLD, 1));
    advance(INP_DEFAULT_HOLD_MS);
    release(INP_ENC3_BUTTON);
    release(INP_ENC1_BUTTON);
    CHECK(fake.gesture_count == 1);
    CHECK(state() == INP_STATE_NEUTRAL && count_published(INP_PUB_SHIFT_ENTERED) == 0);
}

/* --- Encoder 3: page and Shift (items 21-26) ----------------------------------- */

static void encoder3_click_advances_the_page(void)
{
    reset();
    press(INP_ENC3_BUTTON);
    advance(100u);
    release(INP_ENC3_BUTTON);
    CHECK(only_gesture_is(GESTURE_CLICK, 3));
    CHECK(count_published(INP_PUB_SHIFT_ENTERED) == 0);
}

static void encoder3_hold_enters_shift_and_its_release_never_advances(void)
{
    reset();
    press(INP_ENC3_BUTTON);
    hold_threshold();
    CHECK(state() == INP_STATE_SHIFT_READY && InputResolution_ShiftActive());
    CHECK(last_published_is(INP_PUB_SHIFT_ENTERED));
    release(INP_ENC3_BUTTON);
    CHECK(state() == INP_STATE_NEUTRAL && !InputResolution_ShiftActive());
    CHECK(last_published_is(INP_PUB_SHIFT_LEFT));
    CHECK(fake.gesture_count == 0);
}

static void another_press_enters_shift_before_the_threshold(void)
{
    reset();
    press(INP_ENC3_BUTTON);
    advance(50u);
    press(INP_ENC1_BUTTON);
    CHECK(state() == INP_STATE_SHIFT_READY);
    CHECK(only_gesture_is(GESTURE_SHIFT_ENCODER, 1));
    release(INP_ENC1_BUTTON);
    release(INP_ENC3_BUTTON);
    CHECK(fake.gesture_count == 1 && state() == INP_STATE_NEUTRAL);
}

static void turns_in_shift_are_swallowed(void)
{
    reset();
    press(INP_ENC3_BUTTON);
    hold_threshold();
    turn(0, 3);
    turn(3, -1);
    CHECK(fake.gesture_count == 0 && state() == INP_STATE_SHIFT_READY);
}

static void utility_root_spends_shift(void)
{
    reset();
    press(INP_ENC3_BUTTON);
    hold_threshold();
    press(INP_ENC0_BUTTON);
    CHECK(only_gesture_is(GESTURE_SHIFT_ENCODER, 0));
    CHECK(state() == INP_STATE_SHIFT_SPENT);
    press(INP_BUTTON1);
    release(INP_BUTTON1);
    press(INP_ENC1_BUTTON);
    CHECK(fake.gesture_count == 1);
    release(INP_ENC3_BUTTON);
    CHECK(state() == INP_STATE_NEUTRAL);
    release(INP_ENC1_BUTTON);
    release(INP_ENC0_BUTTON);
    CHECK(fake.gesture_count == 1);
}

static void shift_is_unavailable_off_an_operating_page(void)
{
    /* In a menu or utility: Encoder 3 resolves as click or hold, and Button 0 has
     * no action (items 24 and 38). */
    reset();
    fake.on_page = false;
    press(INP_ENC3_BUTTON);
    press(INP_BUTTON0);
    CHECK(state() == INP_STATE_NEUTRAL && fake.gesture_count == 0);
    hold_threshold();
    CHECK(only_gesture_is(GESTURE_HOLD, 3));
    CHECK(count_published(INP_PUB_SHIFT_ENTERED) == 0);
    release(INP_BUTTON0);
    release(INP_ENC3_BUTTON);
    CHECK(fake.gesture_count == 1);
}

static void a_utility_exit_hold_never_becomes_shift_after_the_return(void)
{
    reset();
    fake.on_page = false;
    press(INP_ENC3_BUTTON);
    hold_threshold(); /* leaves the utility */
    CHECK(only_gesture_is(GESTURE_HOLD, 3));
    fake.on_page = true; /* back on the page, Encoder 3 still held */
    advance(1000u);
    press(INP_BUTTON0);
    CHECK(state() == INP_STATE_PENDING); /* a session hold, not a Shift action */
    release(INP_BUTTON0);
    release(INP_ENC3_BUTTON);
    CHECK(fake.gesture_count == 1 && count_published(INP_PUB_SHIFT_ENTERED) == 0);
}

/* --- Chord permutations (decision 0009 P1-P15) --------------------------------- */

static void chord_permutations_resolve_as_specified(void)
{
    static const ChordCase cases[] = {
        {"P1", "B0d B0u", {GESTURE_SHIFT_BUTTON0}, 1},
        {"P2", "B1d B1u", {GESTURE_SHIFT_BUTTON1}, 1},
        {"P3", "B0d B1d B1u B0u", {GESTURE_SHIFT_CHORD}, 1},
        {"P4", "B0d B1d B0u B1u", {GESTURE_SHIFT_CHORD}, 1},
        {"P5", "B1d B0d B0u B1u", {GESTURE_SHIFT_CHORD}, 1},
        {"P6", "B1d B0d B1u B0u", {GESTURE_SHIFT_CHORD}, 1},
        {"P7", "B0d E3u B0u", {GESTURE_SHIFT_BUTTON0}, 1},
        {"P8", "B1d E3u B1u", {GESTURE_SHIFT_BUTTON1}, 1},
        {"P9a", "B0d B1d E3u B0u B1u", {GESTURE_SHIFT_CHORD}, 1},
        {"P9b", "B0d B1d E3u B1u B0u", {GESTURE_SHIFT_CHORD}, 1},
        {"P10", "B1d B1u B0d B0u", {GESTURE_SHIFT_BUTTON1, GESTURE_SHIFT_BUTTON0}, 2},
        {"P11", "B0d B0u B1d B1u", {GESTURE_SHIFT_BUTTON0}, 1},
        {"P12", "B0d R B0u B1d B1u", {GESTURE_BUTTON1_DOWN, GESTURE_BUTTON1_UP}, 2},
        {"P15", "E1d E1u E1d E1u", {GESTURE_SHIFT_ENCODER, GESTURE_SHIFT_ENCODER}, 2},
    };
    static const char *const variants[] = {"E3d ", "E3h "};
    for (size_t v = 0; v < 2; ++v) {
        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
            reset();
            char script[64];
            snprintf(script, sizeof(script), "%s%s", variants[v], cases[i].script);
            run_script(script);
            check_gestures(cases[i].name, variants[v], cases[i].expected, cases[i].expected_count);
            /* Whatever happened, releasing everything leaves no Shift behind. */
            release(INP_ENC3_BUTTON);
            if (state() != INP_STATE_NEUTRAL || InputResolution_ShiftActive()) {
                printf("FAIL %s: %s (%s) left Shift active\n", current_test, cases[i].name,
                       variants[v]);
                ++failures;
            }
            /* The Encoder 3 press never advanced the page. */
            for (unsigned g = 0; g < fake.gesture_count && g < LOG_CAPACITY; ++g) {
                if (fake.gestures[g].kind == GESTURE_CLICK) {
                    printf("FAIL %s: %s (%s) emitted a click\n", current_test, cases[i].name,
                           variants[v]);
                    ++failures;
                }
            }
        }
    }
}

static void p11_swallows_presses_after_the_mode_switch(void)
{
    reset();
    run_script("E3h B0d B0u");
    CHECK(state() == INP_STATE_SHIFT_SPENT);
    run_script("B1d B1u");
    CHECK(only_gesture_is(GESTURE_SHIFT_BUTTON0, 0));
}

static void p13_ptt_held_before_shift_stays_ptt(void)
{
    reset();
    run_script("B1d E3d B0d B0u B1u E3u");
    CHECK(fake.gesture_count == 3);
    CHECK(gesture_is(0, GESTURE_BUTTON1_DOWN, 0));
    CHECK(gesture_is(1, GESTURE_SHIFT_BUTTON0, 0));
    CHECK(gesture_is(2, GESTURE_BUTTON1_UP, 0));
    CHECK(state() == INP_STATE_NEUTRAL);
}

static void p14_session_hold_swallows_encoder3(void)
{
    reset();
    run_script("B0d E3d");
    CHECK(state() == INP_STATE_PENDING);
    CHECK(count_published(INP_PUB_SHIFT_ENTERED) == 0);
    hold_threshold();
    CHECK(count_published(INP_PUB_SHIFT_ENTERED) == 0 && fake.gesture_count == 0);
}

/* --- Button 0 session hold (decision 0005) ------------------------------------- */

static void hold_opens_start_prompt_when_no_session_is_active(void)
{
    reset();
    press(INP_BUTTON0);
    CHECK(state() == INP_STATE_PENDING);
    CHECK(fake.published_count == 0);
    advance(INP_DEFAULT_SESSION_HOLD_MS - 1u);
    CHECK(state() == INP_STATE_PENDING);
    advance(1u);
    CHECK(state() == INP_STATE_START_PROMPT);
    CHECK(fake.published_count == 1 && last_published_is(INP_PUB_PROMPT_OPENED_START));
    CHECK(fake.gesture_count == 0 && fake.command_count == 0);
}

static void hold_opens_stop_prompt_when_a_session_is_active(void)
{
    reset();
    to_prompt(true);
    CHECK(state() == INP_STATE_STOP_PROMPT);
    CHECK(fake.published_count == 1 && last_published_is(INP_PUB_PROMPT_OPENED_STOP));
}

static void short_press_of_button0_has_no_action(void)
{
    reset();
    press(INP_BUTTON0);
    release(INP_BUTTON0);
    CHECK(state() == INP_STATE_NEUTRAL);
    session_threshold();
    CHECK(state() == INP_STATE_NEUTRAL);
    CHECK(fake.gesture_count == 0 && fake.command_count == 0 && fake.published_count == 0);
}

static void other_gestures_are_swallowed_before_the_prompt_appears(void)
{
    reset();
    to_pending();
    press(INP_BUTTON1);
    press(INP_ENC0_BUTTON);
    press(INP_ENC3_BUTTON);
    turn(2, 3);
    release(INP_ENC0_BUTTON);
    CHECK(state() == INP_STATE_PENDING);
    CHECK(fake.gesture_count == 0 && fake.command_count == 0);
    CHECK(status().swallowed == 5);
}

static void releases_of_swallowed_presses_are_swallowed_after_the_hold(void)
{
    reset();
    to_pending();
    press(INP_BUTTON1);
    press(INP_ENC3_BUTTON);
    release(INP_BUTTON0);
    CHECK(state() == INP_STATE_NEUTRAL);
    release(INP_BUTTON1);
    release(INP_ENC3_BUTTON);
    CHECK(fake.gesture_count == 0);
    /* The next press resolves normally again. */
    press(INP_BUTTON1);
    release(INP_BUTTON1);
    CHECK(fake.gesture_count == 2);
}

static void a_pending_press_is_consumed_by_the_session_hold(void)
{
    /* Modal layers cancel pending presses (decision 0009 item 5). */
    reset();
    press(INP_ENC0_BUTTON);
    to_prompt(false);
    release(INP_ENC0_BUTTON);
    CHECK(fake.gesture_count == 0 && state() == INP_STATE_START_PROMPT);
    CHECK(fake.command_count == 0);
}

static void confirming_a_start_prompt_issues_start_session(void)
{
    reset();
    to_prompt(false);
    press(INP_ENC0_BUTTON);
    CHECK(state() == INP_STATE_CONSUMED);
    CHECK(fake.command_count == 1 && fake.commands[0] == INP_CMD_START_SESSION);
    CHECK(last_published_is(INP_PUB_PROMPT_CONFIRMED_START));
    CHECK(fake.gesture_count == 0);
}

static void confirming_a_stop_prompt_issues_stop_session(void)
{
    reset();
    to_prompt(true);
    press(INP_ENC0_BUTTON);
    CHECK(state() == INP_STATE_CONSUMED);
    CHECK(fake.command_count == 1 && fake.commands[0] == INP_CMD_STOP_SESSION);
    CHECK(last_published_is(INP_PUB_PROMPT_CONFIRMED_STOP));
}

static void releasing_button0_cancels_the_prompt(void)
{
    reset();
    to_prompt(true);
    release(INP_BUTTON0);
    CHECK(state() == INP_STATE_NEUTRAL);
    CHECK(fake.command_count == 0);
    CHECK(fake.published_count == 2 && last_published_is(INP_PUB_PROMPT_CANCELLED));
    CHECK(fake.gesture_count == 0);
}

static void the_prompt_dismisses_everything_else(void)
{
    reset();
    to_prompt(false);
    press(INP_BUTTON1);
    press(INP_ENC1_BUTTON);
    turn(0, -2);
    release(INP_BUTTON1);
    release(INP_ENC1_BUTTON);
    CHECK(state() == INP_STATE_START_PROMPT);
    CHECK(fake.gesture_count == 0 && fake.command_count == 0 && fake.published_count == 1);
}

static void the_rest_of_the_hold_is_swallowed_after_confirming(void)
{
    reset();
    to_consumed();
    const unsigned published = fake.published_count;
    release(INP_ENC0_BUTTON);
    press(INP_ENC0_BUTTON);
    turn(1, 1);
    release(INP_ENC0_BUTTON);
    CHECK(state() == INP_STATE_CONSUMED);
    release(INP_BUTTON0);
    CHECK(state() == INP_STATE_NEUTRAL);
    CHECK(fake.gesture_count == 0 && fake.command_count == 1);
    CHECK(fake.published_count == published);
}

static void a_session_change_withdraws_the_open_prompt(void)
{
    reset();
    to_prompt(false);
    fake.session_active = true;
    InputResolution_OnSessionChanged();
    CHECK(state() == INP_STATE_CONSUMED);
    CHECK(last_published_is(INP_PUB_PROMPT_WITHDRAWN));
    CHECK(fake.command_count == 0);
    press(INP_ENC0_BUTTON);
    CHECK(fake.command_count == 0 && fake.gesture_count == 0);
    release(INP_BUTTON0);
    CHECK(state() == INP_STATE_NEUTRAL);
}

static void a_session_change_outside_the_prompt_changes_nothing(void)
{
    reset();
    InputResolution_OnSessionChanged();
    CHECK(state() == INP_STATE_NEUTRAL);
    to_pending();
    InputResolution_OnSessionChanged();
    CHECK(state() == INP_STATE_PENDING);
    release(INP_BUTTON0);
    to_consumed();
    InputResolution_OnSessionChanged();
    CHECK(state() == INP_STATE_CONSUMED);
    CHECK(fake.published_count == 2); /* opened and confirmed only */
}

static void pre_held_ptt_release_is_delivered_exactly_once(void)
{
    static const char *const names[] = {"pending", "prompt", "consumed"};
    for (int where = 0; where < 3; ++where) {
        reset();
        press(INP_BUTTON1); /* held before Button 0 went down: delivered */
        CHECK(fake.gesture_count == 1);
        if (where == 0) {
            to_pending();
        } else if (where == 1) {
            to_prompt(false);
        } else {
            to_consumed();
        }
        const InpStatus before = status();
        release(INP_BUTTON1);
        const InpStatus after = status();
        if (fake.gesture_count != 2 || after.emitted != before.emitted + 1u ||
            after.swallowed != before.swallowed) {
            printf("FAIL %s: pre-held release in %s not delivered exactly once\n",
                   current_test, names[where]);
            ++failures;
        }
        CHECK(gesture_is(1, GESTURE_BUTTON1_UP, 0));
        CHECK(fake.command_count == (where == 2 ? 1u : 0u));
    }
}

/* --- Reconciliation (decision 0009 item 6, Principle III) ---------------------- */

static void reconcile_releases_everything_from_every_state(void)
{
    for (int start = 0; start < 8; ++start) {
        reset();
        press(INP_BUTTON1); /* delivered, then left held */
        switch (start) {
        case 1: to_pending(); break;
        case 2: to_prompt(false); break;
        case 3: to_prompt(true); break;
        case 4: to_consumed(); break;
        case 5: run_script("E3h"); break;      /* ShiftReady */
        case 6: run_script("E3d B0d"); break;  /* ShiftButton0 */
        case 7: press(INP_ENC0_BUTTON); break; /* pending click */
        default: break;
        }
        const unsigned published = fake.published_count;
        const unsigned commands = fake.command_count;
        const unsigned gestures = fake.gesture_count;
        InputResolution_OnReconcile();
        CHECK(state() == INP_STATE_NEUTRAL && !InputResolution_ShiftActive());
        CHECK(fake.release_alls == 1);
        CHECK(fake.command_count == commands);
        CHECK(fake.gesture_count == gestures); /* reconciliation never acts */
        if (start == 2 || start == 3) {
            CHECK(fake.published_count == published + 1 && last_published_is(INP_PUB_PROMPT_CANCELLED));
        } else if (start == 5 || start == 6) {
            CHECK(fake.published_count == published + 1 && last_published_is(INP_PUB_SHIFT_LEFT));
        } else {
            CHECK(fake.published_count == published);
        }
        /* Every control counts as released: later releases act on nothing. */
        release(INP_BUTTON1);
        release(INP_BUTTON0);
        release(INP_ENC0_BUTTON);
        release(INP_ENC3_BUTTON);
        advance(2000u);
        CHECK(fake.gesture_count == gestures);
        /* A new hold works. */
        fake.session_active = false;
        to_prompt(false);
        CHECK(state() == INP_STATE_START_PROMPT);
    }
}

static void malformed_input_is_rejected(void)
{
    reset();
    input(INP_INPUT_PRESS, INP_CONTROL_COUNT, 0);
    input(INP_INPUT_DETENTS, INP_ENCODER_COUNT, 1);
    input((InpInputKind)7, 0, 0);
    InputResolution_OnInput(NULL);
    const InpStatus s = status();
    CHECK(s.rejected == 4 && s.emitted == 0 && s.swallowed == 0 && s.absorbed == 0);
    CHECK(state() == INP_STATE_NEUTRAL);
}

static void configured_thresholds_apply(void)
{
    reset();
    const InpConfig config = {150u, 600u};
    InputResolution_Configure(&config);
    press(INP_ENC2_BUTTON);
    advance(149u);
    CHECK(fake.gesture_count == 0);
    advance(1u);
    CHECK(only_gesture_is(GESTURE_HOLD, 2));
    release(INP_ENC2_BUTTON);
    press(INP_BUTTON0);
    advance(600u);
    CHECK(state() == INP_STATE_START_PROMPT);
}

/* Random sequences: every well-formed input has exactly one outcome, commands come
 * only from a confirming Encoder 0 press, PTT is never delivered twice without a
 * release, and releasing every control ends Shift and every pending press. */
static void random_sequences_keep_the_invariants(void)
{
    uint32_t rng = 12345u;
    for (int run = 0; run < 300; ++run) {
        reset();
        bool down[INP_CONTROL_COUNT] = {false};
        unsigned inputs = 0;
        int ptt_open = 0;
        unsigned seen = 0;
        for (int step = 0; step < 300; ++step) {
            rng = rng * 1103515245u + 12345u;
            const uint32_t r = rng >> 8;
            const uint8_t state_before = state();
            const unsigned commands_before = fake.command_count;
            bool enc0_press = false;
            switch (r % 10u) {
            case 0: advance((r >> 4) % 700u); break;
            case 1: fake.session_active = !fake.session_active; InputResolution_OnSessionChanged(); break;
            case 2:
                if (r % 97u == 0u) {
                    InputResolution_OnReconcile();
                    memset(down, 0, sizeof(down));
                    ptt_open = 0;
                }
                break;
            case 3: fake.on_page = (r & 0x100u) != 0u; break;
            case 4: turn((uint8_t)((r >> 4) % INP_ENCODER_COUNT), (int8_t)((r >> 6) % 5u) - 2); ++inputs; break;
            default: {
                const InpControl c = (InpControl)((r >> 4) % INP_CONTROL_COUNT);
                if (down[c]) { release(c); } else { press(c); enc0_press = c == INP_ENC0_BUTTON; }
                down[c] = !down[c];
                ++inputs;
                break;
            }
            }
            for (; seen < fake.gesture_count && seen < LOG_CAPACITY; ++seen) {
                if (fake.gestures[seen].kind == GESTURE_BUTTON1_DOWN) {
                    ++ptt_open;
                } else if (fake.gestures[seen].kind == GESTURE_BUTTON1_UP) {
                    --ptt_open;
                }
            }
            if (fake.gesture_count >= LOG_CAPACITY) {
                fake.gesture_count = 0; /* keep the log bounded */
                seen = 0;
            }
            const InpStatus s = status();
            if (s.emitted + s.absorbed + s.swallowed != inputs) {
                printf("FAIL %s: run %d step %d: %u inputs, %u outcomes\n", current_test, run,
                       step, inputs, (unsigned)(s.emitted + s.absorbed + s.swallowed));
                ++failures;
                return;
            }
            if (ptt_open < 0 || ptt_open > 1) {
                printf("FAIL %s: run %d step %d: PTT balance %d\n", current_test, run, step, ptt_open);
                ++failures;
                return;
            }
            if (fake.command_count != commands_before) {
                const bool in_prompt = state_before == INP_STATE_START_PROMPT ||
                                       state_before == INP_STATE_STOP_PROMPT;
                if (!(enc0_press && in_prompt && fake.command_count == commands_before + 1u)) {
                    printf("FAIL %s: run %d step %d: unexpected command\n", current_test, run, step);
                    ++failures;
                    return;
                }
            }
        }
        /* Release everything: nothing stays latched. */
        for (int c = 0; c < (int)INP_CONTROL_COUNT; ++c) {
            if (down[c]) {
                release((InpControl)c);
            }
        }
        advance(2000u);
        for (; seen < fake.gesture_count && seen < LOG_CAPACITY; ++seen) {
            if (fake.gestures[seen].kind == GESTURE_BUTTON1_DOWN) {
                ++ptt_open;
            } else if (fake.gestures[seen].kind == GESTURE_BUTTON1_UP) {
                --ptt_open;
            }
        }
        if (state() != INP_STATE_NEUTRAL || InputResolution_ShiftActive() || ptt_open != 0 ||
            InputResolution_TickDue(fake.now + 100000u)) {
            printf("FAIL %s: run %d left state %u, PTT balance %d\n", current_test, run,
                   (unsigned)state(), ptt_open);
            ++failures;
            return;
        }
    }
}

#define RUN(test)              \
    do {                       \
        current_test = #test;  \
        test();                \
    } while (0)

int main(void)
{
    RUN(click_fires_on_release_before_the_threshold);
    RUN(hold_fires_at_the_threshold_and_its_release_is_swallowed);
    RUN(a_due_threshold_resolves_before_a_late_release);
    RUN(push_turn_cancels_the_click_and_the_hold);
    RUN(turning_another_encoder_keeps_a_pending_click);
    RUN(button1_is_delivered_from_its_press);
    RUN(a_hold_cancels_other_pending_presses);
    RUN(encoder3_click_advances_the_page);
    RUN(encoder3_hold_enters_shift_and_its_release_never_advances);
    RUN(another_press_enters_shift_before_the_threshold);
    RUN(turns_in_shift_are_swallowed);
    RUN(utility_root_spends_shift);
    RUN(shift_is_unavailable_off_an_operating_page);
    RUN(a_utility_exit_hold_never_becomes_shift_after_the_return);
    RUN(chord_permutations_resolve_as_specified);
    RUN(p11_swallows_presses_after_the_mode_switch);
    RUN(p13_ptt_held_before_shift_stays_ptt);
    RUN(p14_session_hold_swallows_encoder3);
    RUN(hold_opens_start_prompt_when_no_session_is_active);
    RUN(hold_opens_stop_prompt_when_a_session_is_active);
    RUN(short_press_of_button0_has_no_action);
    RUN(other_gestures_are_swallowed_before_the_prompt_appears);
    RUN(releases_of_swallowed_presses_are_swallowed_after_the_hold);
    RUN(a_pending_press_is_consumed_by_the_session_hold);
    RUN(confirming_a_start_prompt_issues_start_session);
    RUN(confirming_a_stop_prompt_issues_stop_session);
    RUN(releasing_button0_cancels_the_prompt);
    RUN(the_prompt_dismisses_everything_else);
    RUN(the_rest_of_the_hold_is_swallowed_after_confirming);
    RUN(a_session_change_withdraws_the_open_prompt);
    RUN(a_session_change_outside_the_prompt_changes_nothing);
    RUN(pre_held_ptt_release_is_delivered_exactly_once);
    RUN(reconcile_releases_everything_from_every_state);
    RUN(malformed_input_is_rejected);
    RUN(configured_thresholds_apply);
    RUN(random_sequences_keep_the_invariants);
    if (failures != 0) {
        printf("input_resolution_test: %d failure(s)\n", failures);
        return 1;
    }
    puts("input_resolution_test: all scenarios passed");
    return 0;
}
