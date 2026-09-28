#ifndef SPOOKY_APP_EVENTS_H
#define SPOOKY_APP_EVENTS_H

/* The one M7 application event queue (decision 0007). Foreground only: never call
 * these from an ISR or DMA callback. Machines are wired to it one at a time, from
 * the Session shadow machine (full_spooky_proto-8lw.14) onwards. */

#include "event_queue.h"

bool AppEvents_Post(EvqClass event_class, uint16_t type, uint32_t arg0, uint32_t arg1);
uint32_t AppEvents_Service(EvqDispatch dispatch, void *context);
void AppEvents_GetStats(EvqStats *stats);

#endif /* SPOOKY_APP_EVENTS_H */
