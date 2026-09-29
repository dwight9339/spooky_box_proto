#ifndef SPOOKY_DIAGNOSTICS_H
#define SPOOKY_DIAGNOSTICS_H
#include "diag_history.h"
#include "foreground_budget.h"

void Diagnostics_Init(void);
/* Bounded numeric event, usable from normal IRQs or foreground on M7.
 * Not a HardFault/NMI crash recorder; no logging, USB or allocation here. */
void Diagnostics_Record(DiagEventType type, uint32_t arg0, uint32_t arg1);
/* Records duration only while recording. A duration over the service's fixed
 * budget emits DIAG_FOREGROUND_BUDGET and is therefore visible as a fault. */
void Diagnostics_ObserveForeground(ForegroundService service,
                                   uint32_t duration_ms, bool recording);
bool Diagnostics_HandleCommand(const char *command);
/* Foreground: observes service gaps/IPC/log loss and sends at most one short
 * USB line per call. USB backpressure never blocks the producer or loop. */
void Diagnostics_Service(void);
#endif
