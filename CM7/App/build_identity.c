#include "build_identity.h"

#include "stm32h7xx_hal.h"
#include "usb_test.h"

#include <stdio.h>
#include <string.h>

#ifndef SPOOKY_BUILD_ID
#define SPOOKY_BUILD_ID "unidentified"
#endif

#define BUILD_IDENTITY_BACKUP_MAGIC 0x53424931UL /* SBI1 */

/* This marker is intentionally present in the ELF for offline host preflight. */
static const char target_build_marker[] __attribute__((used)) =
  "SBID1:" SPOOKY_BUILD_ID;

static uint32_t boot_epoch;
static uint32_t reset_flags;
static bool initialized;

void BuildIdentity_Init(void)
{
  uint32_t previous;

  if (initialized)
  {
    return;
  }
  reset_flags = RCC->RSR;
  HAL_PWR_EnableBkUpAccess();
  __HAL_RCC_RTC_CLK_ENABLE();

  if (RTC->BKP30R != BUILD_IDENTITY_BACKUP_MAGIC)
  {
    RTC->BKP30R = BUILD_IDENTITY_BACKUP_MAGIC;
    previous = 0U;
  }
  else
  {
    previous = RTC->BKP31R;
  }
  boot_epoch = previous + 1U;
  if (boot_epoch == 0U)
  {
    boot_epoch = 1U;
  }
  RTC->BKP31R = boot_epoch;
  __HAL_RCC_CLEAR_RESET_FLAGS();
  initialized = true;
}

uint32_t BuildIdentity_BootEpoch(void)
{
  return boot_epoch;
}

uint32_t BuildIdentity_ResetFlags(void)
{
  return reset_flags;
}

uint32_t BuildIdentity_Capabilities(void)
{
  uint32_t capabilities = BUILD_IDENTITY_CAP_IDENTITY |
                          BUILD_IDENTITY_CAP_BOOT_EPOCH |
                          BUILD_IDENTITY_CAP_DIAGNOSTICS |
                          BUILD_IDENTITY_CAP_WAV_V1;
#if defined(SPOOKY_IPC_SMOKE)
  capabilities |= BUILD_IDENTITY_CAP_IPC_SMOKE;
#endif
  return capabilities;
}

const char *BuildIdentity_Id(void)
{
  return &target_build_marker[sizeof("SBID1:") - 1U];
}

bool BuildIdentity_HandleCommand(const char *command)
{
  char response[224];

  if ((command == NULL) ||
      ((strcmp(command, "DIAG IDENTITY") != 0) &&
       (strcmp(command, "IDENTITY") != 0)))
  {
    return false;
  }
  if (!initialized)
  {
    (void)UsbTest_SendText("ERR IDENTITY not initialized\r\n");
    return true;
  }
  (void)snprintf(response, sizeof(response),
    "OK IDENTITY V=%lu CORE=7 BUILD=%s BOOT=%lu RESET=%lu CAPS=%lu\r\n",
    (unsigned long)BUILD_IDENTITY_SCHEMA_VERSION, BuildIdentity_Id(),
    (unsigned long)boot_epoch, (unsigned long)reset_flags,
    (unsigned long)BuildIdentity_Capabilities());
  (void)UsbTest_SendText(response);
  return true;
}
