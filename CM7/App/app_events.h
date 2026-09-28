#ifndef SPOOKY_APP_EVENTS_H
#define SPOOKY_APP_EVENTS_H

/* The one M7 application event queue (decision 0007). Foreground only: never call
 * these from an ISR or DMA callback. Machines are wired to it one at a time, from
 * the Session shadow machine (full_spooky_proto-8lw.14) onwards. */

#include "event_queue.h"

/* Event types on the M7 queue. Arguments are defined by the module that posts. */
typedef enum AppEventType {
  APP_EVENT_RECONCILE = EVQ_TYPE_RECONCILE, /* every held control is released */
  APP_EVENT_SESSION_START,                  /* session_shadow.c */
  APP_EVENT_SESSION_STOP,
  APP_EVENT_SESSION_BLOCK_WRITTEN,
  APP_EVENT_SESSION_CAPTURE_FAULT
} AppEventType;

bool AppEvents_Post(EvqClass event_class, uint16_t type, uint32_t arg0, uint32_t arg1);
uint32_t AppEvents_Service(EvqDispatch dispatch, void *context);
void AppEvents_GetStats(EvqStats *stats);

#endif /* SPOOKY_APP_EVENTS_H */
