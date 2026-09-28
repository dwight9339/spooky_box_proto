#ifndef SPOOKY_SESSION_CONTROL_H
#define SPOOKY_SESSION_CONTROL_H

/*
 * Authoritative Session-machine adapter (decision 0008, 8lw.4). External
 * start/stop requests and internal recorder outcomes enter through the M7 event
 * queue. The machine alone invokes recorder lifecycle operations. Foreground only.
 */

#include <stdbool.h>
#include <stdint.h>

#include "event_queue.h"

bool SessionControl_RequestStart(uint32_t seconds, bool radio_ready);
bool SessionControl_RequestStop(void);
void SessionControl_ReportBlockWritten(void);
void SessionControl_ReportCaptureFault(void);

void SessionControl_Init(void);
void SessionControl_Dispatch(const EvqEvent *event);
void SessionControl_Check(bool recorder_active);
uint32_t SessionControl_Mismatches(void);

#endif /* SPOOKY_SESSION_CONTROL_H */
