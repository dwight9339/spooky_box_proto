#ifndef SPOOKY_INPUT_RESOLUTION_PORT_H
#define SPOOKY_INPUT_RESOLUTION_PORT_H

/*
 * Port of the InputResolution machine (docs/design/behavior/InputResolutionSm.puml,
 * decisions 0005 and 0006). Three parts:
 *
 * 1. The service interface the M7 application calls. Each call dispatches one event
 *    to the generated machine and returns when the machine has finished with it.
 * 2. The guards and actions the diagram calls. Nothing else may be called from the
 *    diagram.
 * 3. The integration functions the port calls. The firmware wiring provides them
 *    once the input service (full_spooky_proto-8lw.9) and the product IPC
 *    (full_spooky_proto-54w.4) exist; host tests provide fakes.
 *
 * Portable C with no HAL calls. Not reentrant: call it from one context only, and do
 * not call the service interface from an integration function.
 */

#include <stdbool.h>
#include <stdint.h>

/* Controls that report press and release. Encoder turns report detents. */
typedef enum InpControl {
    INP_BUTTON0 = 0,
    INP_BUTTON1,
    INP_ENC0_BUTTON,
    INP_ENC1_BUTTON,
    INP_ENC2_BUTTON,
    INP_ENC3_BUTTON,
    INP_CONTROL_COUNT
} InpControl;

#define INP_ENCODER_COUNT 4u

typedef enum InpInputKind {
    INP_INPUT_PRESS = 0,
    INP_INPUT_RELEASE,
    INP_INPUT_DETENTS
} InpInputKind;

/* A behavior-neutral input event (constitution Principle III). */
typedef struct InpInput {
    uint32_t sequence;
    uint32_t time_ms;
    uint8_t kind;    /* InpInputKind */
    uint8_t index;   /* InpControl for press and release, encoder 0-3 for detents */
    int8_t detents;  /* signed detent count for INP_INPUT_DETENTS, otherwise 0 */
    uint8_t reserved;
} InpInput;

/* Commands issued through the shared command policy. */
typedef enum InpCommand {
    INP_CMD_START_SESSION = 0,
    INP_CMD_STOP_SESSION
} InpCommand;

/* Domain events the region publishes (presentation.md). */
typedef enum InpPublished {
    INP_PUB_PROMPT_OPENED_START = 0,
    INP_PUB_PROMPT_OPENED_STOP,
    INP_PUB_PROMPT_CONFIRMED_START,
    INP_PUB_PROMPT_CONFIRMED_STOP,
    INP_PUB_PROMPT_CANCELLED,
    INP_PUB_PROMPT_WITHDRAWN
} InpPublished;

/* Machine states, for status and diagnostics. */
typedef enum InpState {
    INP_STATE_NEUTRAL = 0,
    INP_STATE_PENDING,
    INP_STATE_START_PROMPT,
    INP_STATE_STOP_PROMPT,
    INP_STATE_CONSUMED
} InpState;

typedef struct InpStatus {
    uint8_t state;        /* InpState */
    uint8_t reserved[3];
    uint32_t delivered;   /* input events passed on to gesture resolution */
    uint32_t swallowed;   /* input events dropped, including releases of dropped presses */
    uint32_t rejected;    /* malformed input events, dropped before dispatch */
} InpStatus;

/* --- 1. Service interface ------------------------------------------------------ */

/* Resets the machine to Neutral with no control held. */
void InputResolution_Init(void);

/* An input event from the input subsystem. Every well-formed event is either
 * delivered or swallowed exactly once. */
void InputResolution_OnInput(const InpInput *input);

/* The hold timer started by inp_start_hold_timer() expired. Ignored if it was
 * cancelled in the meantime. */
void InputResolution_OnHoldThreshold(void);

/* The Session region changed state. */
void InputResolution_OnSessionChanged(void);

/* Input reporting restarted, overflowed or went stale: every control is released. */
void InputResolution_OnReconcile(void);

void InputResolution_GetStatus(InpStatus *status);

/* --- 2. Guards and actions called by the diagram ------------------------------- */

/* Guards read a classification of the current input event taken before dispatch, so
 * they give the same answer however many actions run. */
bool inp_input_is_delivered_release(void);   /* release of a control whose press was delivered */
bool inp_input_is_undelivered_release(void); /* release of a control whose press was not delivered */
bool inp_session_hold_allowed(void);
bool inp_session_active(void);

void inp_deliver(void);
void inp_swallow(void);
void inp_release_all(void);
void inp_start_hold_timer(void);
void inp_cancel_hold_timer(void);
void inp_issue(InpCommand command);
void inp_publish(InpPublished event);

/* --- 3. Integration functions, provided by the firmware wiring or a test ------- */

/* Pass an input event on to gesture resolution in the current context. */
void inp_integration_deliver(const InpInput *input);
/* Tell gesture resolution that every control is released. */
void inp_integration_release_all(void);
/* Button 0 may start a session hold: Shift is not held and the device is in Field or
 * Instrument. */
bool inp_integration_session_hold_allowed(void);
/* The Session region is active (recording or finalizing). */
bool inp_integration_session_active(void);
/* Start or cancel the session hold timer. The threshold stays configurable until
 * bench trials set it (decision 0005 item 10). On expiry, call
 * InputResolution_OnHoldThreshold(). */
void inp_integration_start_hold_timer(void);
void inp_integration_cancel_hold_timer(void);
/* Issue a command through the shared command policy. */
void inp_integration_issue(InpCommand command);
/* Publish a domain event. */
void inp_integration_publish(InpPublished event);

#endif /* SPOOKY_INPUT_RESOLUTION_PORT_H */
