#include "session_port.h"

#include "SessionSm.h"

/* One machine per region, so one private instance. */
static SessionSm machine;

/* Results stored when the event arrived or when an action ran. */
static uint32_t start_seconds;
static bool can_start;
static bool file_opened;
static bool capture_started;
static bool target_reached;
static bool finalize_ok;

void Session_Init(void)
{
    start_seconds = 0u;
    can_start = false;
    file_opened = false;
    capture_started = false;
    target_reached = false;
    finalize_ok = false;
    SessionSm_ctor(&machine);
    SessionSm_start(&machine);
}

void Session_OnStart(uint32_t seconds)
{
    start_seconds = seconds;
    can_start = ses_integration_can_start(seconds);
    file_opened = false;
    capture_started = false;
    SessionSm_dispatch_event(&machine, SessionSm_EventId_START);
}

void Session_OnStop(void)
{
    SessionSm_dispatch_event(&machine, SessionSm_EventId_STOP);
}

void Session_OnBlockWritten(void)
{
    target_reached = ses_integration_target_reached();
    SessionSm_dispatch_event(&machine, SessionSm_EventId_BLOCK_WRITTEN);
}

void Session_OnCaptureFault(void)
{
    SessionSm_dispatch_event(&machine, SessionSm_EventId_CAPTURE_FAULT);
}

SesState Session_GetState(void)
{
    switch (machine.state_id) {
    case SessionSm_StateId_RECORDING:
        return SES_STATE_RECORDING;
    case SessionSm_StateId_FINALIZING:
        return SES_STATE_FINALIZING;
    default:
        return SES_STATE_IDLE;
    }
}

bool Session_IsActive(void)
{
    return Session_GetState() != SES_STATE_IDLE;
}

/* --- Guards and actions called by the diagram ---------------------------------- */

bool ses_can_start(void) { return can_start; }
bool ses_file_opened(void) { return file_opened; }
bool ses_capture_started(void) { return capture_started; }
bool ses_target_reached(void) { return target_reached; }
bool ses_finalize_ok(void) { return finalize_ok; }

void ses_open_file(void)
{
    file_opened = ses_integration_open_file(start_seconds);
}

void ses_start_capture(void)
{
    capture_started = ses_integration_start_capture();
}

void ses_request_stop(void)
{
    ses_integration_request_stop();
}

void ses_stop_capture(void)
{
    ses_integration_stop_capture();
}

void ses_finalize_file(void)
{
    finalize_ok = ses_integration_finalize_file();
}

void ses_publish(SesPublished event)
{
    ses_integration_publish(event);
}

void ses_state_changed(void)
{
    ses_integration_state_changed(Session_GetState());
}
