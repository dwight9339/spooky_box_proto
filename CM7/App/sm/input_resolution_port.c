#include "input_resolution_port.h"

#include <stddef.h>

#include "InputResolutionSm.h"

/* One machine per region, so one private instance. */
static InputResolutionSm machine;

/* How the press of each held control was resolved. A release is interpreted from
 * this record, never from the layer it arrives in (decision 0009 item 1). */
typedef enum PressRecord {
    REC_IDLE = 0,       /* not held */
    REC_PENDING,        /* encoder press: click or hold not yet decided */
    REC_SESSION_HOLD,   /* Button 0 session hold, threshold not yet reached */
    REC_DELIVERED,      /* delivered as a gesture; its release is delivered too */
    REC_SHIFT_PENDING,  /* Shift button action that fires on release */
    REC_CONSUMED        /* resolved or dropped; its release is swallowed */
} PressRecord;

typedef struct Press {
    uint8_t record;     /* PressRecord */
    bool shift_capable; /* Encoder 3 pressed on an operating page */
    uint32_t deadline_ms;
} Press;

static Press presses[INP_CONTROL_COUNT];
static InpConfig config;

/* The event being dispatched, and its classification taken before dispatch. */
static const InpInput *current_input; /* NULL while a threshold is dispatched */
static uint8_t current_control;       /* InpControl, or the turned encoder's button */
static InpReleaseKind current_release;
static bool current_shift_armed;
static bool current_hold_is_shift;

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

static bool is_encoder_button(uint8_t control)
{
    return control >= INP_ENC0_BUTTON && control <= INP_ENC3_BUTTON;
}

static uint8_t encoder_of(uint8_t control)
{
    return (uint8_t)(control - INP_ENC0_BUTTON);
}

static bool is_due(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

static bool has_threshold(const Press *press)
{
    return press->record == REC_PENDING || press->record == REC_SESSION_HOLD;
}

static InputResolutionSm_EventId event_for(const InpInput *input)
{
    if (input->kind == INP_INPUT_DETENTS) {
        return InputResolutionSm_EventId_TURN;
    }
    const bool press = input->kind == INP_INPUT_PRESS;
    switch (input->index) {
    case INP_BUTTON0:
        return press ? InputResolutionSm_EventId_B0_PRESS : InputResolutionSm_EventId_B0_RELEASE;
    case INP_BUTTON1:
        return press ? InputResolutionSm_EventId_B1_PRESS : InputResolutionSm_EventId_B1_RELEASE;
    case INP_ENC0_BUTTON:
        return press ? InputResolutionSm_EventId_E0_PRESS : InputResolutionSm_EventId_E_RELEASE;
    case INP_ENC3_BUTTON:
        return press ? InputResolutionSm_EventId_E3_PRESS : InputResolutionSm_EventId_E3_RELEASE;
    default:
        return press ? InputResolutionSm_EventId_E_PRESS : InputResolutionSm_EventId_E_RELEASE;
    }
}

static InpReleaseKind release_kind(uint8_t control)
{
    switch (presses[control].record) {
    case REC_PENDING:
        return INP_RELEASE_CLICK;
    case REC_DELIVERED:
        return INP_RELEASE_DELIVERED;
    case REC_SHIFT_PENDING:
        return INP_RELEASE_SHIFT_PENDING;
    default:
        return INP_RELEASE_SWALLOWED;
    }
}

static void emit(GestureKind kind, uint8_t encoder, int8_t detents)
{
    const Gesture gesture = {(uint8_t)kind, encoder, detents, 0u};
    inp_integration_emit(gesture);
}

static void settle(uint32_t *counter)
{
    if (current_input != NULL) {
        ++*counter;
    }
}

/* Resolve the earliest due threshold, if any. Returns false when none is due. */
static bool fire_next_threshold(uint32_t now)
{
    int earliest = -1;
    for (int i = 0; i < (int)INP_CONTROL_COUNT; ++i) {
        const Press *press = &presses[i];
        if (!has_threshold(press) || !is_due(now, press->deadline_ms)) {
            continue;
        }
        if (earliest < 0 || (int32_t)(press->deadline_ms - presses[earliest].deadline_ms) < 0) {
            earliest = i;
        }
    }
    if (earliest < 0) {
        return false;
    }
    Press *press = &presses[earliest];
    const bool session_hold = press->record == REC_SESSION_HOLD;
    current_input = NULL;
    current_control = (uint8_t)earliest;
    current_release = INP_RELEASE_SWALLOWED;
    current_shift_armed = false;
    current_hold_is_shift = !session_hold && earliest == INP_ENC3_BUTTON && press->shift_capable;
    /* The press is resolved by its threshold: its release is swallowed. */
    press->record = REC_CONSUMED;
    InputResolutionSm_dispatch_event(&machine, session_hold ? InputResolutionSm_EventId_B0_HOLD
                                                            : InputResolutionSm_EventId_HOLD);
    current_hold_is_shift = false;
    return true;
}

void InputResolution_Init(void)
{
    for (size_t i = 0; i < INP_CONTROL_COUNT; ++i) {
        presses[i] = (Press){0};
    }
    config.hold_ms = INP_DEFAULT_HOLD_MS;
    config.session_hold_ms = INP_DEFAULT_SESSION_HOLD_MS;
    current_input = NULL;
    current_control = 0;
    current_release = INP_RELEASE_SWALLOWED;
    current_shift_armed = false;
    current_hold_is_shift = false;
    status = (InpStatus){0};
    InputResolutionSm_ctor(&machine);
    InputResolutionSm_start(&machine);
}

void InputResolution_Configure(const InpConfig *new_config)
{
    if (new_config != NULL) {
        config = *new_config;
    }
}

void InputResolution_OnInput(const InpInput *input)
{
    if (!is_well_formed(input)) {
        ++status.rejected;
        return;
    }
    InputResolution_OnTick();

    current_input = input;
    current_control = input->kind == INP_INPUT_DETENTS ? (uint8_t)(INP_ENC0_BUTTON + input->index)
                                                       : input->index;
    current_release = input->kind == INP_INPUT_RELEASE ? release_kind(input->index)
                                                       : INP_RELEASE_SWALLOWED;
    current_shift_armed = presses[INP_ENC3_BUTTON].record == REC_PENDING &&
                          presses[INP_ENC3_BUTTON].shift_capable;
    if (input->kind == INP_INPUT_PRESS) {
        /* A press of a control recorded as held means its release was lost; the new
         * press replaces the old record. */
        presses[input->index] = (Press){0};
    }
    InputResolutionSm_dispatch_event(&machine, event_for(input));
    if (input->kind == INP_INPUT_RELEASE) {
        presses[input->index] = (Press){0};
    }
    current_input = NULL;
    current_release = INP_RELEASE_SWALLOWED;
    current_shift_armed = false;
}

void InputResolution_OnTick(void)
{
    const uint32_t now = inp_integration_now_ms();
    /* Each threshold consumes its press, so this ends after at most one pass per
     * control. */
    for (size_t i = 0; i < INP_CONTROL_COUNT; ++i) {
        if (!fire_next_threshold(now)) {
            break;
        }
    }
}

bool InputResolution_TickDue(uint32_t now_ms)
{
    for (size_t i = 0; i < INP_CONTROL_COUNT; ++i) {
        if (has_threshold(&presses[i]) && is_due(now_ms, presses[i].deadline_ms)) {
            return true;
        }
    }
    return false;
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
    case InputResolutionSm_StateId_SHIFTREADY:
        out->state = INP_STATE_SHIFT_READY;
        break;
    case InputResolutionSm_StateId_SHIFTBUTTON0:
        out->state = INP_STATE_SHIFT_BUTTON0;
        break;
    case InputResolutionSm_StateId_SHIFTBUTTON1:
        out->state = INP_STATE_SHIFT_BUTTON1;
        break;
    case InputResolutionSm_StateId_SHIFTSPENT:
        out->state = INP_STATE_SHIFT_SPENT;
        break;
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

bool InputResolution_ShiftActive(void)
{
    switch (machine.state_id) {
    case InputResolutionSm_StateId_SHIFTREADY:
    case InputResolutionSm_StateId_SHIFTBUTTON0:
    case InputResolutionSm_StateId_SHIFTBUTTON1:
    case InputResolutionSm_StateId_SHIFTSPENT:
        return true;
    default:
        return false;
    }
}

/* --- Guards called by the diagram ---------------------------------------------- */

bool inp_release_is(InpReleaseKind kind)
{
    return current_input != NULL && current_input->kind == INP_INPUT_RELEASE &&
           current_release == kind;
}

bool inp_shift_armed(void)
{
    return current_shift_armed;
}

bool inp_hold_is_shift(void)
{
    return current_hold_is_shift;
}

bool inp_on_page(void)
{
    return inp_integration_on_page();
}

bool inp_session_active(void)
{
    return inp_integration_session_active();
}

/* --- Actions that settle the current input ------------------------------------- */

void inp_emit_press(void)
{
    presses[current_control].record = REC_DELIVERED;
    settle(&status.emitted);
    emit(GESTURE_BUTTON1_DOWN, 0u, 0);
}

void inp_emit_release(void)
{
    settle(&status.emitted);
    emit(GESTURE_BUTTON1_UP, 0u, 0);
}

void inp_emit_click(void)
{
    settle(&status.emitted);
    emit(GESTURE_CLICK, encoder_of(current_control), 0);
}

void inp_emit_turn(void)
{
    settle(&status.emitted);
    emit(GESTURE_TURN, encoder_of(current_control), current_input != NULL ? current_input->detents : 0);
}

void inp_emit_shift(void)
{
    presses[current_control].record = REC_CONSUMED;
    settle(&status.emitted);
    emit(GESTURE_SHIFT_ENCODER, encoder_of(current_control), 0);
}

void inp_fire_shift_release(void)
{
    settle(&status.emitted);
    emit(current_control == INP_BUTTON0 ? GESTURE_SHIFT_BUTTON0 : GESTURE_SHIFT_BUTTON1, 0u, 0);
}

void inp_fire_chord(void)
{
    presses[INP_BUTTON0].record = REC_CONSUMED;
    presses[INP_BUTTON1].record = REC_CONSUMED;
    settle(&status.emitted);
    emit(GESTURE_SHIFT_CHORD, 0u, 0);
}

void inp_begin_press(void)
{
    Press *press = &presses[current_control];
    press->record = REC_PENDING;
    press->shift_capable = current_control == INP_ENC3_BUTTON && inp_integration_on_page();
    press->deadline_ms = inp_integration_now_ms() + config.hold_ms;
    settle(&status.absorbed);
}

void inp_begin_session_hold(void)
{
    Press *press = &presses[current_control];
    press->record = REC_SESSION_HOLD;
    press->shift_capable = false;
    press->deadline_ms = inp_integration_now_ms() + config.session_hold_ms;
    settle(&status.absorbed);
}

void inp_mark_shift_pending(void)
{
    presses[current_control].record = REC_SHIFT_PENDING;
    settle(&status.absorbed);
}

void inp_swallow(void)
{
    if (current_input == NULL) {
        return;
    }
    if (current_input->kind == INP_INPUT_PRESS) {
        presses[current_control].record = REC_CONSUMED;
    }
    ++status.swallowed;
}

/* --- Actions that settle no input ---------------------------------------------- */

void inp_emit_hold(void)
{
    if (is_encoder_button(current_control)) {
        emit(GESTURE_HOLD, encoder_of(current_control), 0);
    }
}

void inp_cancel_pending(void)
{
    for (size_t i = 0; i < INP_CONTROL_COUNT; ++i) {
        if (presses[i].record == REC_PENDING) {
            presses[i].record = REC_CONSUMED;
        }
    }
}

void inp_cancel_turned_press(void)
{
    if (presses[current_control].record == REC_PENDING) {
        presses[current_control].record = REC_CONSUMED;
    }
}

void inp_release_all(void)
{
    for (size_t i = 0; i < INP_CONTROL_COUNT; ++i) {
        presses[i] = (Press){0};
    }
    inp_integration_release_all();
}

void inp_issue(InpCommand command)
{
    inp_integration_issue(command);
}

void inp_publish(InpPublished event)
{
    inp_integration_publish(event);
}
