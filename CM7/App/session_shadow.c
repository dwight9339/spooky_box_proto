#include "session_shadow.h"

#include <stdio.h>

#include "app_events.h"
#include "diagnostics.h"
#include "session_port.h"

/* Argument encoding on the queue. */
#define START_OUTCOME_MASK  0xFFU
#define START_FINALIZED_BIT 0x100U
#define BLOCK_TARGET_BIT    0x1U
#define BLOCK_FINALIZED_BIT 0x2U

/* What the recorder reported for the event being dispatched. */
static SessionShadowStart observed_start;
static bool observed_target_reached;
static bool observed_finalized;

static bool agreed = true;
static uint32_t mismatches;

void SessionShadow_ReportStart(uint32_t seconds, SessionShadowStart outcome, bool finalized)
{
  (void)AppEvents_Post(EVQ_CLASS_INTERNAL, APP_EVENT_SESSION_START, seconds,
                       (uint32_t)outcome | (finalized ? START_FINALIZED_BIT : 0U));
}

void SessionShadow_ReportStop(void)
{
  (void)AppEvents_Post(EVQ_CLASS_INTERNAL, APP_EVENT_SESSION_STOP, 0U, 0U);
}

void SessionShadow_ReportBlockWritten(bool target_reached, bool finalized)
{
  (void)AppEvents_Post(EVQ_CLASS_INTERNAL, APP_EVENT_SESSION_BLOCK_WRITTEN,
                       (target_reached ? BLOCK_TARGET_BIT : 0U) |
                       (finalized ? BLOCK_FINALIZED_BIT : 0U), 0U);
}

void SessionShadow_ReportCaptureFault(bool finalized)
{
  (void)AppEvents_Post(EVQ_CLASS_INTERNAL, APP_EVENT_SESSION_CAPTURE_FAULT,
                       finalized ? 1U : 0U, 0U);
}

void SessionShadow_Init(void)
{
  agreed = true;
  mismatches = 0U;
  Session_Init();
}

void SessionShadow_Dispatch(const EvqEvent *event)
{
  switch (event->type)
  {
    case APP_EVENT_SESSION_START:
      observed_start = (SessionShadowStart)(event->arg1 & START_OUTCOME_MASK);
      observed_finalized = (event->arg1 & START_FINALIZED_BIT) != 0U;
      Session_OnStart(event->arg0);
      break;
    case APP_EVENT_SESSION_STOP:
      Session_OnStop();
      break;
    case APP_EVENT_SESSION_BLOCK_WRITTEN:
      observed_target_reached = (event->arg0 & BLOCK_TARGET_BIT) != 0U;
      observed_finalized = (event->arg0 & BLOCK_FINALIZED_BIT) != 0U;
      Session_OnBlockWritten();
      break;
    case APP_EVENT_SESSION_CAPTURE_FAULT:
      observed_finalized = event->arg0 != 0U;
      Session_OnCaptureFault();
      break;
    default:
      break;
  }
}

void SessionShadow_Check(bool recorder_active)
{
  const bool machine_active = Session_IsActive();
  const bool agree = machine_active == recorder_active;
  if (!agree && agreed)
  {
    ++mismatches;
    Diagnostics_Record(DIAG_SESSION_MISMATCH, (uint32_t)Session_GetState(),
                       recorder_active ? 1U : 0U);
    printf("[session] shadow MISMATCH state=%u recorder-active=%u\r\n",
           (unsigned int)Session_GetState(), recorder_active ? 1U : 0U);
  }
  else if (agree && !agreed)
  {
    printf("[session] shadow agrees again state=%u\r\n", (unsigned int)Session_GetState());
  }
  agreed = agree;
}

uint32_t SessionShadow_Mismatches(void)
{
  return mismatches;
}

/* --- Shadow integration: answer from the report, perform nothing --------------- */

bool ses_integration_can_start(uint32_t seconds)
{
  (void)seconds;
  return observed_start != SESSION_SHADOW_REJECTED;
}

bool ses_integration_open_file(uint32_t seconds)
{
  (void)seconds;
  return observed_start != SESSION_SHADOW_OPEN_FAILED;
}

bool ses_integration_start_capture(void)
{
  return observed_start == SESSION_SHADOW_STARTED;
}

void ses_integration_request_stop(void)
{
}

bool ses_integration_target_reached(void)
{
  return observed_target_reached;
}

void ses_integration_stop_capture(void)
{
}

bool ses_integration_finalize_file(void)
{
  return observed_finalized;
}

void ses_integration_publish(SesPublished event)
{
  static const char *const names[] = {"RecordingStarted", "RecordingRejected",
    "RecordingStopping", "RecordingCompleted", "RecordingAborted", "StopIgnored"};
  printf("[session] shadow %s\r\n",
         ((unsigned int)event < (sizeof(names) / sizeof(names[0]))) ? names[event] : "?");
}

void ses_integration_state_changed(SesState state)
{
  static const char *const names[] = {"Idle", "Recording", "Finalizing"};
  printf("[session] shadow state=%s\r\n",
         ((unsigned int)state < (sizeof(names) / sizeof(names[0]))) ? names[state] : "?");
}
