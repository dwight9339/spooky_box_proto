#include "app_dispatch.h"

#include "app_events.h"
#include "radio_adapter.h"
#include "radio_recorder.h"
#include "session_control.h"

/* Static routing table (decision 0007 items 9 and 10). An event that goes to more
 * than one machine runs them in the order Session, Radio, Context, InputResolution.
 * Session and Radio are wired so far. */
static void Route(void *context, const EvqEvent *event)
{
  (void)context;
  switch (event->type)
  {
    case APP_EVENT_SESSION_START:
    case APP_EVENT_SESSION_STOP:
    case APP_EVENT_SESSION_BLOCK_WRITTEN:
    case APP_EVENT_SESSION_CARD_FULL:
    case APP_EVENT_SESSION_FILE_LIMIT:
    case APP_EVENT_SESSION_CAPTURE_FAULT:
    case APP_EVENT_SESSION_PREPARED:
      SessionControl_Dispatch(event);
      break;
    case APP_EVENT_RADIO_COMMAND:
    case APP_EVENT_RADIO_STARTED:
    case APP_EVENT_RADIO_TUNE_DONE:
    case APP_EVENT_RADIO_TUNE_FAILED:
    case APP_EVENT_RADIO_AUDIO_FAULT:
      RadioAdapter_Dispatch(event);
      break;
    case APP_EVENT_RECONCILE: /* no wired machine tracks held controls yet */
    default:
      break;
  }
}

void AppDispatch_Init(void)
{
  SessionControl_Init();
  RadioAdapter_Init();
}

void AppDispatch_Service(void)
{
  EvqStats stats;
  (void)AppEvents_Service(Route, NULL);
  AppEvents_GetStats(&stats);
  if (stats.count == 0U)
  {
    SessionControl_Check(RadioRecorder_IsActive());
  }
}
