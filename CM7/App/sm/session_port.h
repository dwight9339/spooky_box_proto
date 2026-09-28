#ifndef SPOOKY_SESSION_PORT_H
#define SPOOKY_SESSION_PORT_H

/*
 * Port of the Session machine (docs/design/behavior/SessionSm.puml, decision 0006).
 * Three parts:
 *
 * 1. The service interface the M7 dispatcher calls, one function per event.
 * 2. The guards and actions the diagram calls. Nothing else may be called from the
 *    diagram.
 * 3. The integration functions the port calls. In shadow mode
 *    (CM7/App/session_shadow.c) they answer from what the recorder reported and
 *    perform nothing; host tests provide fakes. With authority
 *    (full_spooky_proto-8lw.4) they will perform the actions.
 *
 * Portable C with no HAL calls. Not reentrant: call it from the dispatcher only.
 */

#include <stdbool.h>
#include <stdint.h>

typedef enum SesState {
    SES_STATE_IDLE = 0,
    SES_STATE_RECORDING,
    SES_STATE_FINALIZING
} SesState;

/* Domain events the region publishes (presentation.md). */
typedef enum SesPublished {
    SES_PUB_RECORDING_STARTED = 0,
    SES_PUB_RECORDING_REJECTED,
    SES_PUB_RECORDING_STOPPING,
    SES_PUB_RECORDING_COMPLETED,
    SES_PUB_RECORDING_ABORTED,
    SES_PUB_STOP_IGNORED
} SesPublished;

/* --- 1. Service interface ------------------------------------------------------ */

void Session_Init(void);
/* StartSession(duration): duration in seconds as requested; the can_start guard
 * decides whether it is valid. */
void Session_OnStart(uint32_t seconds);
void Session_OnStop(void);
/* One matched radio and microphone block was written. */
void Session_OnBlockWritten(void);
/* Card removed, DMA error, queue overrun or failed block write. */
void Session_OnCaptureFault(void);
SesState Session_GetState(void);
bool Session_IsActive(void);

/* --- 2. Guards and actions called by the diagram ------------------------------- */

/* Guards read results stored when the event arrived or when the action ran, so they
 * give the same answer however often they are evaluated. */
bool ses_can_start(void);
bool ses_file_opened(void);
bool ses_capture_started(void);
bool ses_target_reached(void);
bool ses_finalize_ok(void);

void ses_open_file(void);
void ses_start_capture(void);
void ses_request_stop(void);
void ses_stop_capture(void);
void ses_finalize_file(void);
void ses_publish(SesPublished event);
void ses_state_changed(void);

/* --- 3. Integration functions, provided by shadow mode, authority or a test ---- */

bool ses_integration_can_start(uint32_t seconds);
/* Open the session file: mount, name, create, preallocate, write the header. */
bool ses_integration_open_file(uint32_t seconds);
/* Start capture: PDM DMA. */
bool ses_integration_start_capture(void);
/* Stop at the next matched block. */
void ses_integration_request_stop(void);
/* The written audio reached the requested duration or the WAV size limit. */
bool ses_integration_target_reached(void);
void ses_integration_stop_capture(void);
/* Patch the WAV header and close the file. */
bool ses_integration_finalize_file(void);
void ses_integration_publish(SesPublished event);
void ses_integration_state_changed(SesState state);

#endif /* SPOOKY_SESSION_PORT_H */
