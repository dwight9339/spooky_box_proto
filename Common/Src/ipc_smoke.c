#include "ipc_smoke.h"
#include "stm32h7xx_hal.h"
#include <string.h>

#define IPC_SMOKE_HSEM 1U /* HSEM 0 remains dedicated to generated boot sync. */
#define IPC_SMOKE_ADDRESS 0x38000000U

typedef struct
{
  IpcSmokeFrame m7;
  IpcSmokeFrame m4;
} IpcSmokeShared;

/* Both linkers place this NOLOAD object at the same address. M7 alone clears
 * it before releasing M4. No startup .bss/.data initialization may touch it. */
static volatile IpcSmokeShared shared
  __attribute__((section(".ipc_shared"), aligned(32), used));
_Static_assert(sizeof(IpcSmokeShared) == 64U, "Unexpected mailbox layout");

static IpcSmokeDiagnostics diagnostics;
static uint32_t last_service_ms;

void IpcSmoke_Init(void)
{
  __HAL_RCC_HSEM_CLK_ENABLE();
  __HAL_RCC_D3SRAM1_CLK_SLEEP_ENABLE();
#if defined(CORE_CM7)
  MPU_Region_InitTypeDef region = {0};
  /* Reserve MPU region 7 for this opt-in experiment. Configure before the
   * first mailbox access and before any cache enable. Normal, shareable,
   * noncacheable SRAM; no dependence on today's disabled M7 D-cache. */
  HAL_MPU_Disable();
  region.Enable = MPU_REGION_ENABLE;
  region.Number = MPU_REGION_NUMBER7;
  region.BaseAddress = IPC_SMOKE_ADDRESS;
  region.Size = MPU_REGION_SIZE_256B;
  region.TypeExtField = MPU_TEX_LEVEL1;
  region.AccessPermission = MPU_REGION_FULL_ACCESS;
  region.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  region.IsShareable = MPU_ACCESS_SHAREABLE;
  region.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  region.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
  HAL_MPU_ConfigRegion(&region);
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
  shared.m7 = (IpcSmokeFrame){0};
  shared.m4 = (IpcSmokeFrame){0};
  __DMB();
#endif
  memset(&diagnostics, 0, sizeof(diagnostics));
  last_service_ms = HAL_GetTick() - IPC_SMOKE_PERIOD_MS;
}

void IpcSmoke_Service(void)
{
  const uint32_t now = HAL_GetTick();
  IpcSmokeFrame next = {0};
  if ((now - last_service_ms) < IPC_SMOKE_PERIOD_MS) return;
  last_service_ms = now;
  if (HAL_HSEM_FastTake(IPC_SMOKE_HSEM) != HAL_OK)
  {
    ++diagnostics.lock_busy;
    return;
  }
  __DMB();
#if defined(CORE_CM7)
  diagnostics.peer = shared.m4;
#else
  diagnostics.peer = shared.m7;
#endif
  IpcSmoke_Observe(&diagnostics.health, &diagnostics.peer, now,
                   diagnostics.local_sequence,
#if defined(CORE_CM7)
                   true
#else
                   false
#endif
  );
  next.magic = IPC_SMOKE_MAGIC;
  next.version = IPC_SMOKE_VERSION;
  next.size_bytes = sizeof(next);
  next.sequence = IpcSmoke_NextSequence(diagnostics.local_sequence);
  next.uptime_ms = now;
  next.peer_error = (uint32_t)diagnostics.health.error;
  /* Only acknowledge a complete frame that passed this core's checks. */
  if (diagnostics.health.error == IPC_SMOKE_VALID)
    next.ack_sequence = diagnostics.peer.sequence;
#if defined(CORE_CM7)
  next.payload = IpcSmoke_Challenge(next.sequence);
  shared.m7 = next;
#else
  if (diagnostics.health.error == IPC_SMOKE_VALID)
    next.payload = diagnostics.peer.payload;
  shared.m4 = next;
#endif
  diagnostics.local_sequence = next.sequence;
  __DMB();
  HAL_HSEM_Release(IPC_SMOKE_HSEM, 0U);
}

const IpcSmokeDiagnostics *IpcSmoke_GetDiagnostics(void)
{
  return &diagnostics;
}
