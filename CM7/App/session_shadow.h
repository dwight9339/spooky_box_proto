#ifndef SPOOKY_SESSION_SHADOW_H
#define SPOOKY_SESSION_SHADOW_H

/*
 * Session machine in shadow mode (decision 0006 item 18, full_spooky_proto-8lw.14).
 * The recorder keeps authority and reports each outcome here. Each report is posted
 * to the M7 event queue as an internal event; the dispatcher later feeds it to the
 * Session machine, whose guards are answered from the report and whose actions do
 * nothing. After the queue drains, the machine must agree with the recorder about
 * whether a session is active; a disagreement is logged and recorded as a
 * SESSION_MISMATCH diagnostic fault. Foreground only.
 */

#include <stdbool.h>
#include <stdint.h>

#include "event_queue.h"

typedef enum SessionShadowStart {
  SESSION_SHADOW_STARTED = 0,
  SESSION_SHADOW_REJECTED,      /* can_start failed: card, card busy, radio, PDM, duration */
  SESSION_SHADOW_OPEN_FAILED,   /* mount, name, create, preallocate or header failed */
  SESSION_SHADOW_CAPTURE_FAILED /* PDM DMA did not start; the file was finalized or not */
} SessionShadowStart;

/* Reports from the recorder and the CLI command path. */
void SessionShadow_ReportStart(uint32_t seconds, SessionShadowStart outcome, bool finalized);
void SessionShadow_ReportStop(void);
/* finalized is meaningful only when the block ended the session. */
void SessionShadow_ReportBlockWritten(bool target_reached, bool finalized);
void SessionShadow_ReportCaptureFault(bool finalized);

/* Dispatcher side. */
void SessionShadow_Init(void);
void SessionShadow_Dispatch(const EvqEvent *event);
/* Compare the machine with the recorder; call only when the queue is empty. */
void SessionShadow_Check(bool recorder_active);
uint32_t SessionShadow_Mismatches(void);

#endif /* SPOOKY_SESSION_SHADOW_H */
