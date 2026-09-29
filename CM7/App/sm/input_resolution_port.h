#ifndef SPOOKY_INPUT_RESOLUTION_PORT_H
#define SPOOKY_INPUT_RESOLUTION_PORT_H

/*
 * Port of the InputResolution machine (docs/design/behavior/InputResolutionSm.puml,
 * decisions 0005, 0006 and 0009). Three parts:
 *
 * 1. The service interface the M7 application calls. Each call dispatches at most a
 *    few events to the generated machine and returns when the machine is done.
 * 2. The guards and actions the diagram calls. Nothing else may be called from the
 *    diagram.
 * 3. The integration functions the port calls. The firmware wiring provides them
 *    once the input service (full_spooky_proto-8lw.9) and the product IPC
 *    (full_spooky_proto-54w.4) exist; host tests provide fakes.
 *
 * The machine resolves behavior-neutral inputs into gestures (gesture.h) for the
 * Context machine, and resolves the Button 0 session hold into session commands.
 * Hold thresholds are measured on the integration clock, not on input timestamps.
 *
 * Portable C with no HAL calls. Not reentrant: call it from one context only, and do
 * not call the service interface from an integration function.
 */

#include <stdbool.h>
#include <stdint.h>

#include "gesture.h"

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

/* Thresholds (decision 0009 item 7). They stay configurable until bench trials set
 * them (Principle IV). */
typedef struct InpConfig {
    uint32_t hold_ms;         /* encoder button hold threshold and click limit */
    uint32_t session_hold_ms; /* Button 0 session hold threshold (decision 0005) */
} InpConfig;

#define INP_DEFAULT_HOLD_MS 400u
#define INP_DEFAULT_SESSION_HOLD_MS 1000u

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
    INP_PUB_PROMPT_WITHDRAWN,
    INP_PUB_SHIFT_ENTERED,
    INP_PUB_SHIFT_LEFT
} InpPublished;

/* How the release of a held control resolves, from the record of its press. */
typedef enum InpReleaseKind {
    INP_RELEASE_SWALLOWED = 0,  /* the press was swallowed or consumed */
    INP_RELEASE_CLICK,          /* a pending encoder press released before the threshold */
    INP_RELEASE_DELIVERED,      /* the press was delivered as a gesture: Button 1 */
    INP_RELEASE_SHIFT_PENDING   /* a Shift button action that fires on release */
} InpReleaseKind;

/* Machine states, for status and diagnostics. */
typedef enum InpState {
    INP_STATE_NEUTRAL = 0,
    INP_STATE_SHIFT_READY,
    INP_STATE_SHIFT_BUTTON0,
    INP_STATE_SHIFT_BUTTON1,
    INP_STATE_SHIFT_SPENT,
    INP_STATE_PENDING,
    INP_STATE_START_PROMPT,
    INP_STATE_STOP_PROMPT,
    INP_STATE_CONSUMED
} InpState;

typedef struct InpStatus {
    uint8_t state;        /* InpState */
    uint8_t reserved[3];
    uint32_t emitted;     /* input events that produced a gesture */
    uint32_t absorbed;    /* input events recorded as presses that resolve later */
    uint32_t swallowed;   /* input events dropped, including releases of dropped presses */
    uint32_t rejected;    /* malformed input events, dropped before dispatch */
} InpStatus;

/* --- 1. Service interface ------------------------------------------------------ */

/* Resets the machine to Neutral with no control held and the default thresholds. */
void InputResolution_Init(void);
void InputResolution_Configure(const InpConfig *config);

/* An input event from the input subsystem. Every well-formed event is emitted,
 * absorbed or swallowed exactly once. Thresholds already due are resolved first. */
void InputResolution_OnInput(const InpInput *input);

/* The integration clock advanced. Resolves every pending press whose threshold is
 * due, earliest first. */
void InputResolution_OnTick(void);

/* True while a pending press has a threshold due at or before now_ms; the firmware
 * wiring uses it to post a tick only when one is needed. */
bool InputResolution_TickDue(uint32_t now_ms);

/* The Session region changed state. */
void InputResolution_OnSessionChanged(void);

/* Input reporting restarted, overflowed or went stale: every control is released. */
void InputResolution_OnReconcile(void);

void InputResolution_GetStatus(InpStatus *status);
bool InputResolution_ShiftActive(void);

/* --- 2. Guards and actions called by the diagram ------------------------------- */

/* Guards read a classification of the current event taken before dispatch, so they
 * give the same answer however many actions run. */
bool inp_release_is(InpReleaseKind kind);
bool inp_shift_armed(void);   /* Encoder 3 has a pending press armed for Shift */
bool inp_hold_is_shift(void); /* the threshold that fired is the armed Encoder 3 */
bool inp_on_page(void);       /* an operating page has the controls: no menu, no utility */
bool inp_session_active(void);

/* Actions that settle the current input event: exactly one runs per input. */
void inp_emit_press(void);         /* Button 1 down, delivered */
void inp_emit_release(void);       /* release of a delivered press */
void inp_emit_click(void);
void inp_emit_turn(void);
void inp_emit_shift(void);         /* encoder button pressed in Shift */
void inp_fire_shift_release(void); /* Shift plus Button 0 or Button 1 */
void inp_fire_chord(void);         /* Shift plus Buttons 0 and 1 */
void inp_begin_press(void);        /* start a pending click or hold */
void inp_begin_session_hold(void); /* start the Button 0 session hold */
void inp_mark_shift_pending(void); /* Button 0 or 1 waits for its release in Shift */
void inp_swallow(void);

/* Actions that settle no input by themselves. */
void inp_emit_hold(void);            /* the threshold of the current control fired */
void inp_cancel_pending(void);       /* every pending press becomes consumed */
void inp_cancel_turned_press(void);  /* push-turn cancels the turned encoder's press */
void inp_release_all(void);
void inp_issue(InpCommand command);
void inp_publish(InpPublished event);

/* --- 3. Integration functions, provided by the firmware wiring or a test ------- */

/* Hand a resolved gesture to the Context region, through the M7 queue. */
void inp_integration_emit(Gesture gesture);
/* Tell gesture consumers that every control is released. */
void inp_integration_release_all(void);
/* An operating page has the controls: Field or Instrument, with no menu open and no
 * utility open. Shift and the session hold are available only there. */
bool inp_integration_on_page(void);
/* The Session region is active (recording or finalizing). */
bool inp_integration_session_active(void);
/* The integration clock, in milliseconds, modulo 2^32. */
uint32_t inp_integration_now_ms(void);
/* Issue a command through the shared command policy. */
void inp_integration_issue(InpCommand command);
/* Publish a domain event. */
void inp_integration_publish(InpPublished event);

#endif /* SPOOKY_INPUT_RESOLUTION_PORT_H */
