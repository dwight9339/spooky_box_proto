#ifndef SPOOKY_DIAGNOSTICS_H
#define SPOOKY_DIAGNOSTICS_H
#include "diag_history.h"

void Diagnostics_Init(void);
/* Bounded numeric event, usable from normal IRQs or foreground on M7.
 * Not a HardFault/NMI crash recorder; no logging, USB or allocation here. */
void Diagnostics_Record(DiagEventType type, uint32_t arg0, uint32_t arg1);
bool Diagnostics_HandleCommand(const char *command);
/* Foreground: observes service gaps/IPC/log loss and sends at most one short
 * USB line per call. USB backpressure never blocks the producer or loop. */
void Diagnostics_Service(void);
#endif
