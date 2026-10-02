#ifndef SPOOKY_APP_EVENTS_H
#define SPOOKY_APP_EVENTS_H

/* The one M7 application event queue (decision 0007). Foreground only: never call
 * these from an ISR or DMA callback. Machines are wired to it one at a time, from
 * the authoritative Session machine (full_spooky_proto-8lw.4) onwards. */

#include "event_queue.h"

/* Event types on the M7 queue. Arguments are defined by the module that posts. */
typedef enum AppEventType {
  APP_EVENT_RECONCILE = EVQ_TYPE_RECONCILE, /* every held control is released */
  APP_EVENT_SESSION_START, /* arg0 seconds (0=open); arg1 radio ready */
  APP_EVENT_SESSION_STOP,
  APP_EVENT_SESSION_BLOCK_WRITTEN,
  APP_EVENT_SESSION_CARD_FULL,
  APP_EVENT_SESSION_FILE_LIMIT,
  APP_EVENT_SESSION_CAPTURE_FAULT,
  APP_EVENT_SESSION_PREPARED,  /* arg0 1 if the file was preallocated */
  APP_EVENT_RADIO_COMMAND,     /* arg0 packed RadCommand fields; arg1 kHz or band */
  APP_EVENT_RADIO_STARTED,     /* arg0 1 if the radio started */
  APP_EVENT_RADIO_TUNE_DONE,
  APP_EVENT_RADIO_TUNE_FAILED,
  APP_EVENT_RADIO_AUDIO_FAULT
} AppEventType;

bool AppEvents_Post(EvqClass event_class, uint16_t type, uint32_t arg0, uint32_t arg1);
uint32_t AppEvents_Service(EvqDispatch dispatch, void *context);
void AppEvents_GetStats(EvqStats *stats);

#endif /* SPOOKY_APP_EVENTS_H */
