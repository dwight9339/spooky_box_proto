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

void StorageService_Init(SD_HandleTypeDef *sd);
FRESULT StorageService_Acquire(StorageOwner owner);
FRESULT StorageService_Release(StorageOwner owner);
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
