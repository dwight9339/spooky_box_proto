#ifndef SPOOKY_IPC_SMOKE_H
#define SPOOKY_IPC_SMOKE_H

#include "ipc_smoke_protocol.h"

typedef struct
{
  IpcSmokeHealth health;
  IpcSmokeFrame peer;
  uint32_t local_sequence;
  uint32_t lock_busy;
} IpcSmokeDiagnostics;

/* M7: call once before the boot HSEM releases M4. M4: call after HAL_Init.
 * Requires a paired system reset; independent core reset is unsupported. */
void IpcSmoke_Init(void);
/* Foreground only; one nonblocking HSEM attempt per 100 ms, no logging/ISR. */
void IpcSmoke_Service(void);
const IpcSmokeDiagnostics *IpcSmoke_GetDiagnostics(void);

#endif
