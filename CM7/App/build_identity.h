#ifndef SPOOKY_BUILD_IDENTITY_H
#define SPOOKY_BUILD_IDENTITY_H

#include <stdbool.h>
#include <stdint.h>

#define BUILD_IDENTITY_SCHEMA_VERSION 1U
#define BUILD_IDENTITY_CAP_IDENTITY    (1UL << 0)
#define BUILD_IDENTITY_CAP_BOOT_EPOCH  (1UL << 1)
#define BUILD_IDENTITY_CAP_DIAGNOSTICS (1UL << 2)
#define BUILD_IDENTITY_CAP_WAV_V1      (1UL << 3)
#define BUILD_IDENTITY_CAP_IPC_SMOKE   (1UL << 4)

/*
 * M7-owned target identity. Init snapshots reset cause and advances a retained
 * boot counter exactly once. The two reserved RTC backup registers are not
 * product state and must not be reused without changing this contract.
 */
void BuildIdentity_Init(void);
bool BuildIdentity_HandleCommand(const char *command);
uint32_t BuildIdentity_BootEpoch(void);
uint32_t BuildIdentity_ResetFlags(void);
uint32_t BuildIdentity_Capabilities(void);
const char *BuildIdentity_Id(void);

#endif /* SPOOKY_BUILD_IDENTITY_H */
