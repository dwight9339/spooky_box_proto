#ifndef SPOOKY_TARGET_LOGGER_H
#define SPOOKY_TARGET_LOGGER_H
#include "stm32h7xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct
{
  uint32_t queued_bytes; /* Includes the chunk currently owned by UART. */
  uint32_t high_water;
  uint32_t dropped_writes;
  uint32_t dropped_bytes;
  uint32_t transport_dropped_bytes;
  uint32_t transmitted_bytes;
  uint32_t transport_errors;
  uint32_t rejected_context;
  uint32_t in_flight;
} TargetLoggerStats;

/* One M7 UART owner, interrupt transmit only (no UART DMA configured).
 * printf/_write and Write are foreground-only and never wait for capacity.
 * ISR diagnostics use Diagnostics_Record, never libc formatting. */
void TargetLogger_Init(UART_HandleTypeDef *uart);
bool TargetLogger_Write(const void *data, uint32_t length);
void TargetLogger_Service(void);
void TargetLogger_GetStats(TargetLoggerStats *out);
/* Power-transition use only, after stopping recording. Requires active
 * SysTick and unmasked foreground context. On timeout, abort/drop pending
 * bytes so no logger IRQs keep waking the sleeping MCU. */
bool TargetLogger_Quiesce(uint32_t timeout_ms);
#endif
