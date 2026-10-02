#ifndef SPOOKY_STORAGE_SERVICE_H
#define SPOOKY_STORAGE_SERVICE_H

/*
 * Sole lifecycle owner for the M7 FatFs volume and SDMMC1 handle. File-format
 * clients retain their own FIL objects, but may use them only while holding the
 * matching exclusive owner lease. Foreground only; never call from ISR/DMA code.
 */

#include <stdbool.h>
#include <stdint.h>

#include "ff.h"
#include "stm32h7xx_hal.h"
#include "storage_lease.h"

typedef struct StorageServiceStatus
{
  StorageOwner owner;
  bool mounted;
  uint32_t acquisitions;
  uint32_t busy_rejections;
  uint32_t invalid_releases;
  FRESULT last_result;
} StorageServiceStatus;

typedef struct StorageFormatReport
{
  FRESULT result;
  bool unsupported;       /* Card is not SDHC/SDXC; nothing was written. */
  uint32_t card_type;     /* HAL CARD_SDSC, CARD_SDHC_SDXC, ... */
  uint32_t sectors;
  uint32_t align_sectors; /* Data-area alignment from the card's AU size. */
  uint32_t cluster_bytes;
  uint64_t free_bytes;    /* Read back from the freshly mounted volume. */
  uint32_t duration_ms;
} StorageFormatReport;

void StorageService_Init(SD_HandleTypeDef *sd);
FRESULT StorageService_Acquire(StorageOwner owner);
FRESULT StorageService_Release(StorageOwner owner);
/* Erases the card with a new FAT32 volume, then mounts it once to verify and
 * unmounts. Needs no existing filesystem, only a free lease. SDHC/SDXC only.
 * Blocks for the whole format; callers must be idle maintenance paths. */
bool StorageService_Format(StorageFormatReport *report);
bool StorageService_GetFreeBytes(StorageOwner owner, uint64_t *free_bytes);
bool StorageService_GetCardInfo(StorageOwner owner,
                                HAL_SD_CardInfoTypeDef *info);
bool StorageService_CardPresent(void);
uint32_t StorageService_HalError(void);
uint32_t StorageService_ClockDiv(void);
StorageOwner StorageService_Owner(void);
bool StorageService_GetStatus(StorageServiceStatus *status);
const char *StorageService_ResultName(FRESULT result);

#endif /* SPOOKY_STORAGE_SERVICE_H */
