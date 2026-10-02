#ifndef SPOOKY_ISR_TIMING_H
#define SPOOKY_ISR_TIMING_H

/*
 * Bench-only M7 interrupt timing for jjy.11, compiled only with
 * SPOOKY_ISR_TIMING_QUALIFICATION. IsrTiming_Install copies the flash vector
 * table to RAM and routes SysTick and every external interrupt through one
 * trampoline. The trampoline times the original handler with the DWT cycle
 * counter and tracks bursts: interrupts that follow each other with less than
 * 2 us of foreground time between them. The trampoline itself adds a few
 * dozen cycles per interrupt.
 */

#include <stdbool.h>

/* Call once, early in main, after the system clock is configured: thresholds
 * use SystemCoreClock. Interrupts are masked while the table is switched. */
void IsrTiming_Install(void);
/* Input is the existing CLI's uppercase, trimmed command. */
bool IsrTiming_HandleCommand(const char *command);

#endif /* SPOOKY_ISR_TIMING_H */
