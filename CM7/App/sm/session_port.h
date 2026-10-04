#ifndef SPOOKY_SESSION_PORT_H
#define SPOOKY_SESSION_PORT_H

/*
 * Port of the Session machine (docs/design/behavior/SessionSm.puml, decision 0006).
 * Three parts:
 *
 * 1. The service interface the M7 dispatcher calls, one function per event.
 * 2. The guards and actions the diagram calls. Nothing else may be called from the
 *    diagram.
 * 3. The integration functions the port calls. CM7/App/session_control.c provides
 *    the authoritative recorder adapter; host tests provide fakes.
 *
 * Portable C with no HAL calls. Not reentrant: call it from the dispatcher only.
 */

#include <stdbool.h>
#include <stdint.h>

typedef enum SesState {
    SES_STATE_IDLE = 0,
    SES_STATE_RECORDING,
    SES_STATE_FINALIZING,
    SES_STATE_PREPARING /* file opened, preallocation in progress; not recording */
} SesState;

/* Domain events the region publishes (presentation.md). */
typedef enum SesPublished {
    SES_PUB_RECORDING_STARTED = 0,
    SES_PUB_RECORDING_REJECTED,
    SES_PUB_RECORDING_STOPPING,
    SES_PUB_RECORDING_COMPLETED,
    SES_PUB_RECORDING_ABORTED,
    SES_PUB_RECORDING_CARD_FULL,
    SES_PUB_RECORDING_FILE_LIMIT,
    SES_PUB_STOP_IGNORED,
    SES_PUB_RECORDING_PREPARING, /* start accepted; nothing captured yet */
    SES_PUB_RECORDING_CANCELLED  /* stopped while preparing; no file kept */
} SesPublished;

/* --- 1. Service interface ------------------------------------------------------ */

void Session_Init(void);
/* StartSession(duration): zero is open-ended; 1..3600 is a timed bench session.
 * The can_start guard decides whether the request is otherwise valid. */
void Session_OnStart(uint32_t seconds);
void Session_OnStop(void);
/* The recorder finished preparing the file: allocated (ok) or failed. */
void Session_OnPrepared(bool ok);
/* One matched radio and microphone block was written. */
void Session_OnBlockWritten(void);
/* Storage limits are reported only at a matched-block boundary. */
void Session_OnCardFull(void);
void Session_OnFileLimit(void);
/* Card removed, DMA error, queue overrun or failed block write. */
void Session_OnCaptureFault(void);
SesState Session_GetState(void);
bool Session_IsActive(void);

/* --- 2. Guards and actions called by the diagram ------------------------------- */

/* Guards read results stored when the event arrived or when the action ran, so they
 * give the same answer however often they are evaluated. */
bool ses_can_start(void);
bool ses_file_opened(void);
bool ses_prepare_ok(void);
bool ses_capture_started(void);
bool ses_target_reached(void);
bool ses_finalize_ok(void);

void ses_open_file(void);
void ses_start_capture(void);
void ses_discard_file(void);
void ses_request_stop(void);
void ses_stop_capture(void);
void ses_finalize_file(void);
void ses_publish(SesPublished event);
void ses_state_changed(void);

/* --- 3. Integration functions, provided by the authority adapter or a test ----- */

bool ses_integration_can_start(uint32_t seconds);
/* Mount and check the card, then begin preparing the session file: the
 * recorder names, creates and preallocates it in bounded foreground steps and
 * reports the result through Session_OnPrepared. */
bool ses_integration_open_file(uint32_t seconds);
/* Close and delete a file that was opened but never captured into. */
void ses_integration_discard_file(void);
/* Start capture: PDM DMA. */
bool ses_integration_start_capture(void);
/* Stop at the next matched block. */
void ses_integration_request_stop(void);
/* A stop was requested, the timed target was reached, or a storage limit says to
 * finalize at this matched-block boundary. */
bool ses_integration_target_reached(void);
void ses_integration_stop_capture(void);
/* Patch the WAV header and close the file. */
bool ses_integration_finalize_file(void);
void ses_integration_publish(SesPublished event);
void ses_integration_state_changed(SesState state);

#endif /* SPOOKY_SESSION_PORT_H */
