#include "session_control.h"

#include <stdio.h>

#include "app_events.h"
#include "diagnostics.h"
#include "radio_recorder.h"
#include "sm/session_port.h"

static bool start_radio_ready;
static bool agreed = true;
static uint32_t mismatches;

bool SessionControl_RequestStart(uint32_t seconds, bool radio_ready)
{
  return AppEvents_Post(EVQ_CLASS_EXTERNAL_COMMAND, APP_EVENT_SESSION_START,
                        seconds, radio_ready ? 1U : 0U);
}

bool SessionControl_RequestStop(void)
{
  return AppEvents_Post(EVQ_CLASS_EXTERNAL_COMMAND, APP_EVENT_SESSION_STOP, 0U, 0U);
}

void SessionControl_ReportBlockWritten(void)
{
  (void)AppEvents_Post(EVQ_CLASS_INTERNAL, APP_EVENT_SESSION_BLOCK_WRITTEN, 0U, 0U);
}

void SessionControl_ReportCardFull(void)
{
  (void)AppEvents_Post(EVQ_CLASS_INTERNAL, APP_EVENT_SESSION_CARD_FULL, 0U, 0U);
}

void SessionControl_ReportFileLimit(void)
{
  (void)AppEvents_Post(EVQ_CLASS_INTERNAL, APP_EVENT_SESSION_FILE_LIMIT, 0U, 0U);
}

void SessionControl_ReportCaptureFault(void)
{
  (void)AppEvents_Post(EVQ_CLASS_INTERNAL, APP_EVENT_SESSION_CAPTURE_FAULT, 0U, 0U);
}

bool SessionControl_ReportPrepared(bool ok)
{
  return AppEvents_Post(EVQ_CLASS_INTERNAL, APP_EVENT_SESSION_PREPARED,
                        ok ? 1U : 0U, 0U);
}

void SessionControl_Init(void)
{
  start_radio_ready = false;
  agreed = true;
  mismatches = 0U;
  Session_Init();
}

void SessionControl_Dispatch(const EvqEvent *event)
{
  switch (event->type)
  {
    case APP_EVENT_SESSION_START:
      start_radio_ready = event->arg1 != 0U;
      Session_OnStart(event->arg0);
      break;
    case APP_EVENT_SESSION_STOP:
      Session_OnStop();
      break;
    case APP_EVENT_SESSION_BLOCK_WRITTEN:
      Session_OnBlockWritten();
      break;
    case APP_EVENT_SESSION_CARD_FULL:
      Session_OnCardFull();
      break;
    case APP_EVENT_SESSION_FILE_LIMIT:
      Session_OnFileLimit();
      break;
    case APP_EVENT_SESSION_CAPTURE_FAULT:
      Session_OnCaptureFault();
      break;
    case APP_EVENT_SESSION_PREPARED:
      Session_OnPrepared(event->arg0 != 0U);
      break;
    default:
      break;
  }
}

void SessionControl_Check(bool recorder_active)
{
  const bool machine_active = Session_IsActive();
  const bool agree = machine_active == recorder_active;
  if (!agree && agreed)
  {
    ++mismatches;
    Diagnostics_Record(DIAG_SESSION_MISMATCH, (uint32_t)Session_GetState(),
                       recorder_active ? 1U : 0U);
    printf("[session] authority MISMATCH state=%u recorder-active=%u\r\n",
           (unsigned int)Session_GetState(), recorder_active ? 1U : 0U);
  }
  else if (agree && !agreed)
  {
    printf("[session] authority agrees again state=%u\r\n",
           (unsigned int)Session_GetState());
  }
  agreed = agree;
}

uint32_t SessionControl_Mismatches(void)
{
  return mismatches;
}

/* --- Authoritative integration called only by session_port.c ---------------- */

bool ses_integration_can_start(uint32_t seconds)
{
  return RadioRecorder_CanStart(seconds, start_radio_ready);
}

bool ses_integration_open_file(uint32_t seconds)
{
  return RadioRecorder_OpenFile(seconds);
}

void ses_integration_discard_file(void)
{
  RadioRecorder_DiscardFile();
}

bool ses_integration_start_capture(void)
{
  return RadioRecorder_StartCapture();
}

void ses_integration_request_stop(void)
{
  RadioRecorder_RequestStop();
}

bool ses_integration_target_reached(void)
{
  return RadioRecorder_TargetReached();
}

void ses_integration_stop_capture(void)
{
  RadioRecorder_StopCapture();
}

bool ses_integration_finalize_file(void)
{
  return RadioRecorder_FinalizeFile();
}

void ses_integration_publish(SesPublished event)
{
  RadioRecorder_PublishSessionEvent(event);
}

void ses_integration_state_changed(SesState state)
{
  static const char *const names[] = {
    "Idle", "Recording", "Finalizing", "Preparing"
  };
  printf("[session] authority state=%s\r\n",
         ((unsigned int)state < (sizeof(names) / sizeof(names[0])))
           ? names[state] : "?");
}
