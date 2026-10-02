#include "storage_service.h"

#include "diskio.h"
#include "fat_run.h"
#include "main.h"
#include "sd_diskio.h"

#include <string.h>

/*
 * SDMMC_CK = 75 MHz PLL1Q / (2 * CLKDIV) = 18.75 MHz. The card is never switched
 * to high speed (CMD6), so the bus must stay within the 25 MHz default-speed limit
 * for every card class. CLKDIV 0 bypasses the divider and runs the bus at 75 MHz.
 */
#define STORAGE_CLOCK_DIV  2U

/* FAT32 with 32 KiB clusters, the SD Association layout for SDHC; one FAT. */
#define STORAGE_FORMAT_CLUSTER_BYTES  32768U
#define STORAGE_FORMAT_WORK_BYTES      8192U

static SD_HandleTypeDef *storage_sd;
static FATFS storage_filesystem;
static StorageLease storage_lease;
static bool storage_mounted;
static FRESULT storage_last_result;
static BYTE storage_format_work[STORAGE_FORMAT_WORK_BYTES];
static FatRunSearch storage_run;
static StorageRunReport storage_run_report;
static BYTE storage_run_sector[FAT_RUN_SECTOR_BYTES];
static FatRunway storage_runway;
static StorageRunwayReport storage_runway_report;

_Static_assert((SD_MEDIA_CARD_SDSC == CARD_SDSC) &&
               (SD_MEDIA_CARD_SDHC_SDXC == CARD_SDHC_SDXC),
               "sd_media card types must match the HAL");

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
  storage_sd->Init.ClockDiv = STORAGE_CLOCK_DIV;
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

  result = f_mount(&storage_filesystem, "", 1U);
  if (result != FR_OK)
  {
    (void)StorageUnmount();
    (void)StorageLease_Release(&storage_lease, owner);
    storage_last_result = result;
    return result;
  }
  storage_mounted = true;
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

bool StorageService_Format(StorageFormatReport *report)
{
  HAL_SD_CardInfoTypeDef info;
  FATFS *filesystem;
  DWORD value;
  uint32_t start = HAL_GetTick();

  if (report == NULL)
  {
    return false;
  }
  (void)memset(report, 0, sizeof(*report));
  if ((storage_sd == NULL) || !StorageService_CardPresent())
  {
    report->result = FR_NOT_READY;
  }
  else if (!StorageLease_TryAcquire(&storage_lease, STORAGE_OWNER_FORMAT))
  {
    report->result = FR_LOCKED;
  }
  else
  {
    if ((disk_initialize(0U) & STA_NOINIT) != 0U)
    {
      report->result = FR_NOT_READY;
    }
    else if (HAL_SD_GetCardInfo(storage_sd, &info) != HAL_OK)
    {
      report->result = FR_DISK_ERR;
    }
    else
    {
      report->card_type = info.CardType;
      report->sectors = info.LogBlockNbr;
      if (!SdMedia_RecordingSupported(info.CardType))
      {
        report->unsupported = true;
        report->result = FR_DENIED;
      }
      else
      {
        if (disk_ioctl(0U, GET_BLOCK_SIZE, &value) == RES_OK)
        {
          report->align_sectors = value;
        }
        report->result = f_mkfs("", FM_FAT32, STORAGE_FORMAT_CLUSTER_BYTES,
                                storage_format_work, sizeof(storage_format_work));
        if (report->result == FR_OK)
        {
          report->result = f_mount(&storage_filesystem, "", 1U);
          storage_mounted = (report->result == FR_OK);
        }
        if ((report->result == FR_OK) &&
            ((report->result = f_getfree("", &value, &filesystem)) == FR_OK))
        {
          report->cluster_bytes = (uint32_t)filesystem->csize * 512U;
          report->free_bytes = (uint64_t)value * report->cluster_bytes;
        }
      }
    }
    (void)StorageUnmount();
    (void)StorageLease_Release(&storage_lease, STORAGE_OWNER_FORMAT);
  }
  report->duration_ms = HAL_GetTick() - start;
  storage_last_result = report->result;
  return report->result == FR_OK;
}

FRESULT StorageService_ReadMediaInfo(SdMediaInfo *info)
{
  HAL_SD_CardInfoTypeDef card;
  HAL_SD_CardCIDTypeDef cid;
  HAL_SD_CardStatusTypeDef status;
  FRESULT result = FR_OK;

  if (info == NULL)
  {
    return FR_INVALID_PARAMETER;
  }
  (void)memset(info, 0, sizeof(*info));
  if ((storage_sd == NULL) || !StorageService_CardPresent())
  {
    return FR_NOT_READY;
  }
  if (!StorageLease_TryAcquire(&storage_lease, STORAGE_OWNER_STATUS))
  {
    return FR_LOCKED;
  }
  if ((disk_initialize(0U) & STA_NOINIT) != 0U)
  {
    result = FR_NOT_READY;
  }
  else if ((HAL_SD_GetCardInfo(storage_sd, &card) != HAL_OK) ||
           (HAL_SD_GetCardCID(storage_sd, &cid) != HAL_OK) ||
           (HAL_SD_GetCardStatus(storage_sd, &status) != HAL_OK))
  {
    result = FR_DISK_ERR;
  }
  else
  {
    info->card_type = card.CardType;
    info->sectors = card.LogBlockNbr;
    info->manufacturer_id = cid.ManufacturerID;
    info->oem_id = cid.OEM_AppliID;
    info->product_name1 = cid.ProdName1;
    info->product_name2 = cid.ProdName2;
    info->product_revision = cid.ProdRev;
    info->serial = cid.ProdSN;
    info->manufacture_date = cid.ManufactDate;
    info->speed_class_code = status.SpeedClass;
    info->uhs_speed_grade = status.UhsSpeedGrade;
    info->video_speed_class = status.VideoSpeedClass;
    info->au_size_code = status.AllocationUnitSize;
  }
  (void)StorageUnmount();
  (void)StorageLease_Release(&storage_lease, STORAGE_OWNER_STATUS);
  storage_last_result = result;
  return result;
}

StorageRunState StorageService_BeginRunSearch(StorageOwner owner, uint64_t bytes)
{
  FATFS *const fs = &storage_filesystem;

  (void)memset(&storage_run_report, 0, sizeof(storage_run_report));
  if (!storage_mounted || (storage_lease.owner != owner) || (bytes == 0U))
  {
    return STORAGE_RUN_ERROR;
  }
  if ((fs->fs_type != FS_FAT32) || (fs->csize == 0U))
  {
    return STORAGE_RUN_UNSUPPORTED;
  }
  storage_run_report.cluster_bytes = (uint32_t)fs->csize * FAT_RUN_SECTOR_BYTES;
  storage_run_report.needed_clusters = (uint32_t)(
    (bytes + storage_run_report.cluster_bytes - 1U) / storage_run_report.cluster_bytes);
  FatRun_Init(&storage_run, fs->n_fatent, fs->last_clst + 1U,
              storage_run_report.needed_clusters);
  /* A valid FSInfo free count that is already too small needs no scan. */
  if ((fs->free_clst <= (fs->n_fatent - 2U)) &&
      (fs->free_clst < storage_run_report.needed_clusters))
  {
    storage_run.state = FAT_RUN_NOT_FOUND;
  }
  return (storage_run.state == FAT_RUN_SEARCHING) ? STORAGE_RUN_SEARCHING :
    STORAGE_RUN_NOT_FOUND;
}

/* One FAT sector, relative to the FAT base. FatFs's window is used when it holds
 * the sector, since it may be newer than the card. */
static const BYTE *StorageReadFatSector(uint32_t index)
{
  FATFS *const fs = &storage_filesystem;
  const DWORD sector = fs->fatbase + index;

  if (sector == fs->winsect)
  {
    return fs->win;
  }
  return (disk_read(fs->drv, storage_run_sector, sector, 1U) == RES_OK)
    ? storage_run_sector : NULL;
}

StorageRunState StorageService_BeginRunway(StorageOwner owner, uint32_t first_cluster,
                                           uint64_t allocated_bytes,
                                           uint32_t max_gap_sectors,
                                           StorageRunwayReport *report)
{
  FATFS *const fs = &storage_filesystem;
  StorageRunState state = STORAGE_RUN_SEARCHING;
  uint32_t end;

  (void)memset(&storage_runway_report, 0, sizeof(storage_runway_report));
  storage_runway_report.barrier = true;
  if (!storage_mounted || (storage_lease.owner != owner) || (allocated_bytes == 0U) ||
      (first_cluster < 2U) || (first_cluster >= fs->n_fatent))
  {
    state = STORAGE_RUN_ERROR;
  }
  else if ((fs->fs_type != FS_FAT32) || (fs->csize == 0U))
  {
    state = STORAGE_RUN_UNSUPPORTED;
  }
  else
  {
    storage_runway_report.cluster_bytes = (uint32_t)fs->csize * FAT_RUN_SECTOR_BYTES;
    storage_runway_report.file_clusters = (uint32_t)(
      (allocated_bytes + storage_runway_report.cluster_bytes - 1U) /
      storage_runway_report.cluster_bytes);
    end = first_cluster + storage_runway_report.file_clusters;
    FatRunway_Init(&storage_runway, fs->n_fatent, (end >= fs->n_fatent) ? 2U : end,
                   max_gap_sectors * FAT_RUN_ENTRIES_PER_SECTOR);
    storage_runway_report.barrier = storage_runway.state != FAT_RUNWAY_SCANNING;
  }
  if (report != NULL)
  {
    *report = storage_runway_report;
  }
  return state;
}

bool StorageService_StepRunway(StorageOwner owner, StorageRunwayReport *report)
{
  const BYTE *data;
  uint32_t index;
  bool ok = true;

  if (!storage_mounted || (storage_lease.owner != owner))
  {
    ok = false;
  }
  else if (!storage_runway_report.barrier)
  {
    index = FatRunway_NextSector(&storage_runway);
    data = StorageReadFatSector(index);
    if (data == NULL)
    {
      ok = false;
    }
    else
    {
      ++storage_runway_report.fat_sectors;
      storage_runway_report.barrier =
        FatRunway_Feed(&storage_runway, data, index) != FAT_RUNWAY_SCANNING;
      storage_runway_report.free_clusters = storage_runway.free;
      storage_runway_report.longest_gap = storage_runway.longest_gap;
    }
  }
  if (report != NULL)
  {
    *report = storage_runway_report;
  }
  return ok;
}

StorageRunState StorageService_StepRunSearch(StorageOwner owner, uint32_t budget_ms,
                                             StorageRunReport *report)
{
  const uint32_t start = HAL_GetTick();
  StorageRunState state = STORAGE_RUN_SEARCHING;

  if (!storage_mounted || (storage_lease.owner != owner))
  {
    state = STORAGE_RUN_ERROR;
  }
  else
  {
    ++storage_run_report.steps;
    while (storage_run.state == FAT_RUN_SEARCHING)
    {
      const uint32_t index = FatRun_NextSector(&storage_run);
      const BYTE *data = StorageReadFatSector(index);

      if (data == NULL)
      {
        state = STORAGE_RUN_ERROR;
        break;
      }
      ++storage_run_report.fat_sectors;
      (void)FatRun_Feed(&storage_run, data, index);
      if ((HAL_GetTick() - start) >= budget_ms)
      {
        break;
      }
    }
    storage_run_report.longest_clusters = storage_run.longest;
    if (state != STORAGE_RUN_ERROR)
    {
      if (storage_run.state == FAT_RUN_FOUND)
      {
        /* f_expand starts at last_clst, so the run is the first one it meets. */
        storage_filesystem.last_clst = storage_run.run_start - 1U;
        state = STORAGE_RUN_FOUND;
      }
      else if (storage_run.state == FAT_RUN_NOT_FOUND)
      {
        state = STORAGE_RUN_NOT_FOUND;
      }
    }
  }
  if (report != NULL)
  {
    *report = storage_run_report;
  }
  return state;
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
