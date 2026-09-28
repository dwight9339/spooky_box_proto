#include "storage_service.h"

#include "main.h"
#include "sd_diskio.h"

#define STORAGE_MOUNT_CLOCK_DIV     2U
#define STORAGE_TRANSFER_CLOCK_DIV  0U

static SD_HandleTypeDef *storage_sd;
static FATFS storage_filesystem;
static StorageLease storage_lease;
static bool storage_mounted;
static FRESULT storage_last_result;

static void StorageSetClockDiv(uint32_t clock_div)
{
  if ((storage_sd == NULL) || (storage_sd->Instance == NULL))
  {
    return;
  }
  storage_sd->Init.ClockDiv = clock_div;
  if (HAL_SD_GetState(storage_sd) != HAL_SD_STATE_RESET)
  {
    MODIFY_REG(storage_sd->Instance->CLKCR, SDMMC_CLKCR_CLKDIV, clock_div);
  }
}

static FRESULT StorageUnmount(void)
{
  FRESULT result = FR_OK;

  if (storage_mounted)
  {
    result = f_mount(NULL, "", 0U);
    storage_mounted = false;
  }
  if ((storage_sd != NULL) &&
      (HAL_SD_GetState(storage_sd) != HAL_SD_STATE_RESET))
  {
    if ((HAL_SD_DeInit(storage_sd) != HAL_OK) && (result == FR_OK))
    {
      result = FR_DISK_ERR;
    }
  }
  SdDiskIo_Reset();
  storage_last_result = result;
  return result;
}

void StorageService_Init(SD_HandleTypeDef *sd)
{
  storage_sd = sd;
  storage_mounted = false;
  storage_last_result = FR_OK;
  StorageLease_Init(&storage_lease);
  SdDiskIo_Reset();

  if (storage_sd == NULL)
  {
    return;
  }
  storage_sd->Instance = SDMMC1;
  storage_sd->Init.ClockEdge = SDMMC_CLOCK_EDGE_RISING;
  storage_sd->Init.ClockPowerSave = SDMMC_CLOCK_POWER_SAVE_DISABLE;
  storage_sd->Init.BusWide = SDMMC_BUS_WIDE_4B;
  storage_sd->Init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_ENABLE;
  storage_sd->Init.ClockDiv = STORAGE_MOUNT_CLOCK_DIV;
}

FRESULT StorageService_Acquire(StorageOwner owner)
{
  FRESULT result;

  if ((storage_sd == NULL) || !StorageService_CardPresent())
  {
    storage_last_result = FR_NOT_READY;
    return storage_last_result;
  }
  if (!StorageLease_TryAcquire(&storage_lease, owner))
  {
    storage_last_result = FR_LOCKED;
    return storage_last_result;
  }

  StorageSetClockDiv(STORAGE_MOUNT_CLOCK_DIV);
  result = f_mount(&storage_filesystem, "", 1U);
  if (result != FR_OK)
  {
    (void)StorageUnmount();
    (void)StorageLease_Release(&storage_lease, owner);
    storage_last_result = result;
    return result;
  }
  storage_mounted = true;
  StorageSetClockDiv(STORAGE_TRANSFER_CLOCK_DIV);
  storage_last_result = FR_OK;
  return FR_OK;
}

FRESULT StorageService_Release(StorageOwner owner)
{
  FRESULT result;

  if (storage_lease.owner != owner)
  {
    (void)StorageLease_Release(&storage_lease, owner);
    storage_last_result = FR_LOCKED;
    return storage_last_result;
  }
  result = StorageUnmount();
  (void)StorageLease_Release(&storage_lease, owner);
  storage_last_result = result;
  return result;
}

bool StorageService_GetFreeBytes(StorageOwner owner, uint64_t *free_bytes)
{
  FATFS *filesystem;
  DWORD free_clusters;

  if ((free_bytes == NULL) || !storage_mounted ||
      (storage_lease.owner != owner) ||
      (f_getfree("", &free_clusters, &filesystem) != FR_OK))
  {
    return false;
  }
  *free_bytes = (uint64_t)free_clusters * filesystem->csize * 512U;
  return true;
}

bool StorageService_GetCardInfo(StorageOwner owner,
                                HAL_SD_CardInfoTypeDef *info)
{
  return (info != NULL) && storage_mounted &&
    (storage_lease.owner == owner) && (storage_sd != NULL) &&
    (HAL_SD_GetCardInfo(storage_sd, info) == HAL_OK);
}

bool StorageService_CardPresent(void)
{
  return SD_CARD_IS_PRESENT();
}

uint32_t StorageService_HalError(void)
{
  return (storage_sd != NULL) ? HAL_SD_GetError(storage_sd) : 0U;
}

uint32_t StorageService_ClockDiv(void)
{
  return (storage_sd != NULL) ? storage_sd->Init.ClockDiv : 0U;
}

StorageOwner StorageService_Owner(void)
{
  return storage_lease.owner;
}

bool StorageService_GetStatus(StorageServiceStatus *status)
{
  if (status == NULL)
  {
    return false;
  }
  status->owner = storage_lease.owner;
  status->mounted = storage_mounted;
  status->acquisitions = storage_lease.acquisitions;
  status->busy_rejections = storage_lease.busy_rejections;
  status->invalid_releases = storage_lease.invalid_releases;
  status->last_result = storage_last_result;
  return true;
}

const char *StorageService_ResultName(FRESULT result)
{
  static const char *const names[] = {
    "FR_OK", "FR_DISK_ERR", "FR_INT_ERR", "FR_NOT_READY", "FR_NO_FILE",
    "FR_NO_PATH", "FR_INVALID_NAME", "FR_DENIED", "FR_EXIST",
    "FR_INVALID_OBJECT", "FR_WRITE_PROTECTED", "FR_INVALID_DRIVE",
    "FR_NOT_ENABLED", "FR_NO_FILESYSTEM", "FR_MKFS_ABORTED", "FR_TIMEOUT",
    "FR_LOCKED", "FR_NOT_ENOUGH_CORE", "FR_TOO_MANY_OPEN_FILES",
    "FR_INVALID_PARAMETER"
  };
  return ((unsigned int)result < (sizeof(names) / sizeof(names[0])))
    ? names[(unsigned int)result] : "FR_UNKNOWN";
}
