/*
 * Host scenario tests for the InputResolution machine
 * (docs/design/behavior/InputResolutionSm.puml, decision 0005 items 1-9).
 * The generated machine and the real port run against a fake integration that
 * records every call. Host results only; not hardware evidence.
 */

#include <stdio.h>
#include <string.h>

#include "input_resolution_port.h"

/* --- Fake integration ---------------------------------------------------------- */

#define LOG_CAPACITY 64u

typedef struct Fake {
    bool hold_allowed;
    bool session_active;
    bool timer_running;
    unsigned timer_starts;
    unsigned timer_cancels;
    unsigned release_alls;
    unsigned delivered_count;
    InpInput delivered[LOG_CAPACITY];
    unsigned command_count;
    InpCommand commands[LOG_CAPACITY];
    unsigned published_count;
    InpPublished published[LOG_CAPACITY];
} Fake;

static Fake fake;

void inp_integration_deliver(const InpInput *input)
{
    if (fake.delivered_count < LOG_CAPACITY) {
        fake.delivered[fake.delivered_count] = *input;
    }
    ++fake.delivered_count;
}

void inp_integration_release_all(void) { ++fake.release_alls; }
bool inp_integration_session_hold_allowed(void) { return fake.hold_allowed; }
bool inp_integration_session_active(void) { return fake.session_active; }

void inp_integration_start_hold_timer(void)
{
    fake.timer_running = true;
    ++fake.timer_starts;
}

void inp_integration_cancel_hold_timer(void)
{
    fake.timer_running = false;
    ++fake.timer_cancels;
}

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
    fake.hold_allowed = true;
    sequence = 0;
    InputResolution_Init();
}

static void input(InpInputKind kind, uint8_t index, int8_t detents)
{
    const InpInput event = {++sequence, sequence * 10u, (uint8_t)kind, index, detents, 0};
    InputResolution_OnInput(&event);
}

static void press(InpControl control) { input(INP_INPUT_PRESS, (uint8_t)control, 0); }
static void release(InpControl control) { input(INP_INPUT_RELEASE, (uint8_t)control, 0); }
static void turn(uint8_t encoder, int8_t detents) { input(INP_INPUT_DETENTS, encoder, detents); }

static void threshold(void)
{
    /* Only a running timer can expire. */
    if (fake.timer_running) {
        fake.timer_running = false;
        InputResolution_OnHoldThreshold();
    }
}

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

/* Reach each state from Neutral. */
static void to_pending(void) { press(INP_BUTTON0); }
static void to_prompt(bool session_active)
{
    fake.session_active = session_active;
    press(INP_BUTTON0);
    threshold();
}
static void to_consumed(void)
{
    to_prompt(false);
    press(INP_ENC0_BUTTON);
}

/* --- Scenarios ----------------------------------------------------------------- */

static void hold_opens_start_prompt_when_no_session_is_active(void)
{
    reset();
    press(INP_BUTTON0);
    CHECK(state() == INP_STATE_PENDING);
    CHECK(fake.timer_starts == 1 && fake.timer_running);
    CHECK(fake.published_count == 0);
    threshold();
    CHECK(state() == INP_STATE_START_PROMPT);
    CHECK(fake.published_count == 1 && last_published_is(INP_PUB_PROMPT_OPENED_START));
    CHECK(fake.delivered_count == 0 && fake.command_count == 0);
}

static void hold_opens_stop_prompt_when_a_session_is_active(void)
{
    reset();
    fake.session_active = true;
    press(INP_BUTTON0);
    threshold();
    CHECK(state() == INP_STATE_STOP_PROMPT);
    CHECK(fake.published_count == 1 && last_published_is(INP_PUB_PROMPT_OPENED_STOP));
}

static void button0_is_delivered_when_the_session_hold_is_not_allowed(void)
{
    /* Shift held, or outside Field and Instrument: Button 0 resolves elsewhere. */
    reset();
    fake.hold_allowed = false;
    press(INP_BUTTON0);
    CHECK(state() == INP_STATE_NEUTRAL);
    CHECK(fake.timer_starts == 0);
    release(INP_BUTTON0);
    CHECK(fake.delivered_count == 2);
    CHECK(fake.delivered[0].kind == INP_INPUT_PRESS && fake.delivered[1].kind == INP_INPUT_RELEASE);
}

static void short_press_of_button0_has_no_action(void)
{
    reset();
    press(INP_BUTTON0);
    release(INP_BUTTON0);
    CHECK(state() == INP_STATE_NEUTRAL);
    CHECK(fake.timer_cancels >= 1 && !fake.timer_running);
    CHECK(fake.delivered_count == 0 && fake.command_count == 0 && fake.published_count == 0);
    /* A threshold from the cancelled timer is ignored. */
    InputResolution_OnHoldThreshold();
    CHECK(state() == INP_STATE_NEUTRAL && fake.published_count == 0);
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
    CHECK(fake.delivered_count == 0 && fake.command_count == 0);
    CHECK(status().swallowed == 6);
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
    CHECK(fake.delivered_count == 0);
    /* The next press resolves normally again. */
    press(INP_BUTTON1);
    release(INP_BUTTON1);
    CHECK(fake.delivered_count == 2);
}

static void confirming_a_start_prompt_issues_start_session(void)
{
    reset();
    to_prompt(false);
    press(INP_ENC0_BUTTON);
    CHECK(state() == INP_STATE_CONSUMED);
    CHECK(fake.command_count == 1 && fake.commands[0] == INP_CMD_START_SESSION);
    CHECK(last_published_is(INP_PUB_PROMPT_CONFIRMED_START));
    CHECK(fake.delivered_count == 0);
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
    CHECK(fake.delivered_count == 0);
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
    CHECK(fake.delivered_count == 0 && fake.command_count == 0 && fake.published_count == 1);
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
    CHECK(fake.delivered_count == 0 && fake.command_count == 1);
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
    /* The rest of the hold is swallowed, including a late confirm. */
    press(INP_ENC0_BUTTON);
    CHECK(fake.command_count == 0 && fake.delivered_count == 0);
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

static void pre_held_release_is_delivered_exactly_once(void)
{
    static const char *const names[] = {"pending", "prompt", "consumed"};
    for (int where = 0; where < 3; ++where) {
        reset();
        press(INP_BUTTON1); /* held before Button 0 went down: delivered */
        CHECK(fake.delivered_count == 1);
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
        if (fake.delivered_count != 2 || after.delivered != before.delivered + 1u ||
            after.swallowed != before.swallowed) {
            printf("FAIL %s: pre-held release in %s not delivered exactly once\n",
                   current_test, names[where]);
            ++failures;
        }
        CHECK(fake.delivered[1].kind == INP_INPUT_RELEASE && fake.delivered[1].index == INP_BUTTON1);
        CHECK(fake.command_count == (where == 2 ? 1u : 0u));
    }
}

static void pre_held_encoder0_release_is_delivered_in_the_prompt(void)
{
    reset();
    press(INP_ENC0_BUTTON);
    to_prompt(false);
    release(INP_ENC0_BUTTON);
    CHECK(fake.delivered_count == 2 && state() == INP_STATE_START_PROMPT);
    CHECK(fake.command_count == 0);
}

static void reconcile_releases_everything_from_every_state(void)
{
    for (int start = 0; start < 5; ++start) {
        reset();
        press(INP_BUTTON1); /* delivered, then left held */
        switch (start) {
        case 1: to_pending(); break;
        case 2: to_prompt(false); break;
        case 3: to_prompt(true); break;
        case 4: to_consumed(); break;
        default: break;
        }
        const unsigned published = fake.published_count;
        const unsigned commands = fake.command_count;
        InputResolution_OnReconcile();
        CHECK(state() == INP_STATE_NEUTRAL);
        CHECK(fake.release_alls == 1);
        CHECK(fake.command_count == commands);
        CHECK(!fake.timer_running);
        if (start == 2 || start == 3) {
            CHECK(fake.published_count == published + 1 && last_published_is(INP_PUB_PROMPT_CANCELLED));
        } else {
            CHECK(fake.published_count == published);
        }
        /* Every control counts as released: later releases are swallowed. */
        const unsigned delivered = fake.delivered_count;
        release(INP_BUTTON1);
        release(INP_BUTTON0);
        CHECK(fake.delivered_count == delivered);
        /* A stale threshold is ignored and a new hold works. */
        InputResolution_OnHoldThreshold();
        CHECK(state() == INP_STATE_NEUTRAL);
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
    CHECK(s.rejected == 4 && s.delivered == 0 && s.swallowed == 0);
    CHECK(state() == INP_STATE_NEUTRAL);
}

/* Random sequences: every well-formed input gets exactly one outcome, commands are
 * issued only by a confirming Encoder 0 press, and nothing stays latched. */
static void every_input_has_exactly_one_outcome(void)
{
    uint32_t rng = 12345u;
    for (int run = 0; run < 200; ++run) {
        reset();
        bool down[INP_CONTROL_COUNT] = {false};
        unsigned inputs = 0;
        for (int step = 0; step < 200; ++step) {
            rng = rng * 1103515245u + 12345u;
            const uint32_t r = rng >> 8;
            const uint8_t prompt_before = state();
            const unsigned commands_before = fake.command_count;
            bool enc0_press = false;
            switch (r % 8u) {
            case 0: threshold(); break;
            case 1: fake.session_active = !fake.session_active; InputResolution_OnSessionChanged(); break;
            case 2: if (r % 97u == 0u) { InputResolution_OnReconcile(); memset(down, 0, sizeof(down)); } break;
            case 3: fake.hold_allowed = (r & 0x100u) != 0u; break;
            case 4: turn((uint8_t)((r >> 4) % INP_ENCODER_COUNT), (int8_t)((r >> 6) % 5u) - 2); ++inputs; break;
            default: {
                const InpControl c = (InpControl)((r >> 4) % INP_CONTROL_COUNT);
                if (down[c]) { release(c); } else { press(c); enc0_press = c == INP_ENC0_BUTTON; }
                down[c] = !down[c];
                ++inputs;
                break;
            }
            }
            const InpStatus s = status();
            if (s.delivered + s.swallowed != inputs) {
                printf("FAIL %s: run %d step %d: %u inputs, %u outcomes\n", current_test, run,
                       step, inputs, (unsigned)(s.delivered + s.swallowed));
                ++failures;
                return;
            }
            if (fake.command_count != commands_before) {
                const bool in_prompt = prompt_before == INP_STATE_START_PROMPT ||
                                       prompt_before == INP_STATE_STOP_PROMPT;
                if (!(enc0_press && in_prompt && fake.command_count == commands_before + 1u)) {
                    printf("FAIL %s: run %d step %d: unexpected command\n", current_test, run, step);
                    ++failures;
                    return;
                }
            }
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
    RUN(hold_opens_start_prompt_when_no_session_is_active);
    RUN(hold_opens_stop_prompt_when_a_session_is_active);
    RUN(button0_is_delivered_when_the_session_hold_is_not_allowed);
    RUN(short_press_of_button0_has_no_action);
    RUN(other_gestures_are_swallowed_before_the_prompt_appears);
    RUN(releases_of_swallowed_presses_are_swallowed_after_the_hold);
    RUN(confirming_a_start_prompt_issues_start_session);
    RUN(confirming_a_stop_prompt_issues_stop_session);
    RUN(releasing_button0_cancels_the_prompt);
    RUN(the_prompt_dismisses_everything_else);
    RUN(the_rest_of_the_hold_is_swallowed_after_confirming);
    RUN(a_session_change_withdraws_the_open_prompt);
    RUN(a_session_change_outside_the_prompt_changes_nothing);
    RUN(pre_held_release_is_delivered_exactly_once);
    RUN(pre_held_encoder0_release_is_delivered_in_the_prompt);
    RUN(reconcile_releases_everything_from_every_state);
    RUN(malformed_input_is_rejected);
    RUN(every_input_has_exactly_one_outcome);
    if (failures != 0) {
        printf("input_resolution_test: %d failure(s)\n", failures);
        return 1;
    }
    puts("input_resolution_test: all scenarios passed");
    return 0;
}
