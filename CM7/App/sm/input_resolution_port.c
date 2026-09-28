#include "input_resolution_port.h"

#include <stddef.h>

#include "InputResolutionSm.h"

/* One machine per region, so one private instance. */
static InputResolutionSm machine;

/* Per-control record of the press currently held, so that a release is interpreted
 * in the context where its press began (decision 0005 item 9). */
static bool held[INP_CONTROL_COUNT];
static bool press_delivered[INP_CONTROL_COUNT];

/* The input event being dispatched, and its classification taken before dispatch. */
static const InpInput *current;
static bool current_delivered_release;
static bool current_undelivered_release;

static bool hold_timer_armed;
static InpStatus status;

static bool is_press_or_release(const InpInput *input)
{
    return input->kind == INP_INPUT_PRESS || input->kind == INP_INPUT_RELEASE;
}

static bool is_well_formed(const InpInput *input)
{
    if (input == NULL) {
        return false;
    }
    if (is_press_or_release(input)) {
        return input->index < INP_CONTROL_COUNT;
    }
    return input->kind == INP_INPUT_DETENTS && input->index < INP_ENCODER_COUNT;
}

static InputResolutionSm_EventId event_for(const InpInput *input)
{
    if (input->kind == INP_INPUT_PRESS && input->index == INP_BUTTON0) {
        return InputResolutionSm_EventId_BTN0_PRESS;
    }
    if (input->kind == INP_INPUT_RELEASE && input->index == INP_BUTTON0) {
        return InputResolutionSm_EventId_BTN0_RELEASE;
    }
    if (input->kind == INP_INPUT_PRESS && input->index == INP_ENC0_BUTTON) {
        return InputResolutionSm_EventId_ENC0_PRESS;
    }
    return InputResolutionSm_EventId_INPUT;
}

/* Record the outcome of the current event for its control. */
static void record_outcome(bool delivered)
{
    if (current == NULL) {
        return;
    }
    if (current->kind == INP_INPUT_PRESS) {
        held[current->index] = true;
        press_delivered[current->index] = delivered;
    } else if (current->kind == INP_INPUT_RELEASE) {
        held[current->index] = false;
        press_delivered[current->index] = false;
    }
}

void InputResolution_Init(void)
{
    for (size_t i = 0; i < INP_CONTROL_COUNT; ++i) {
        held[i] = false;
        press_delivered[i] = false;
    }
    current = NULL;
    current_delivered_release = false;
    current_undelivered_release = false;
    hold_timer_armed = false;
    status = (InpStatus){0};
    InputResolutionSm_ctor(&machine);
    InputResolutionSm_start(&machine);
}

void InputResolution_OnInput(const InpInput *input)
{
    if (!is_well_formed(input)) {
        ++status.rejected;
        return;
    }
    const bool release = input->kind == INP_INPUT_RELEASE;
    current = input;
    current_delivered_release = release && held[input->index] && press_delivered[input->index];
    current_undelivered_release = release && !current_delivered_release;
    InputResolutionSm_dispatch_event(&machine, event_for(input));
    current = NULL;
    current_delivered_release = false;
    current_undelivered_release = false;
}

void InputResolution_OnHoldThreshold(void)
{
    if (!hold_timer_armed) {
        return;
    }
    hold_timer_armed = false;
    InputResolutionSm_dispatch_event(&machine, InputResolutionSm_EventId_HOLD_THRESHOLD);
}

void InputResolution_OnSessionChanged(void)
{
    InputResolutionSm_dispatch_event(&machine, InputResolutionSm_EventId_SESSION_CHANGED);
}

void InputResolution_OnReconcile(void)
{
    InputResolutionSm_dispatch_event(&machine, InputResolutionSm_EventId_RECONCILE);
}

void InputResolution_GetStatus(InpStatus *out)
{
    if (out == NULL) {
        return;
    }
    *out = status;
    switch (machine.state_id) {
    case InputResolutionSm_StateId_PENDING:
        out->state = INP_STATE_PENDING;
        break;
    case InputResolutionSm_StateId_STARTPROMPT:
        out->state = INP_STATE_START_PROMPT;
        break;
    case InputResolutionSm_StateId_STOPPROMPT:
        out->state = INP_STATE_STOP_PROMPT;
        break;
    case InputResolutionSm_StateId_CONSUMED:
        out->state = INP_STATE_CONSUMED;
        break;
    default:
        out->state = INP_STATE_NEUTRAL;
        break;
    }
}

/* --- Guards and actions called by the diagram ---------------------------------- */

bool inp_input_is_delivered_release(void)
{
    return current_delivered_release;
}

bool inp_input_is_undelivered_release(void)
{
    return current_undelivered_release;
}

bool inp_session_hold_allowed(void)
{
    return inp_integration_session_hold_allowed();
}

bool inp_session_active(void)
{
    return inp_integration_session_active();
}

void inp_deliver(void)
{
    if (current == NULL) {
        return;
    }
    record_outcome(true);
    ++status.delivered;
    inp_integration_deliver(current);
}

void inp_swallow(void)
{
    if (current == NULL) {
        return;
    }
    record_outcome(false);
    ++status.swallowed;
}

void inp_release_all(void)
{
    for (size_t i = 0; i < INP_CONTROL_COUNT; ++i) {
        held[i] = false;
        press_delivered[i] = false;
    }
    inp_integration_release_all();
}

void inp_start_hold_timer(void)
{
    hold_timer_armed = true;
    inp_integration_start_hold_timer();
}

void inp_cancel_hold_timer(void)
{
    hold_timer_armed = false;
    inp_integration_cancel_hold_timer();
}

void inp_issue(InpCommand command)
{
    inp_integration_issue(command);
}

void inp_publish(InpPublished event)
{
    inp_integration_publish(event);
}
