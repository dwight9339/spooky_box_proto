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
#include "sd_media.h"
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

typedef enum StorageRunState
{
  STORAGE_RUN_SEARCHING = 0,
  STORAGE_RUN_FOUND,       /* The volume's allocation hint points at the run. */
  STORAGE_RUN_NOT_FOUND,   /* No free run long enough anywhere on the volume. */
  STORAGE_RUN_UNSUPPORTED, /* Not FAT32: f_expand must search by itself. */
  STORAGE_RUN_ERROR        /* Lease lost or a FAT read failed. */
} StorageRunState;

typedef struct StorageRunReport
{
  uint32_t cluster_bytes;
  uint32_t needed_clusters;
  uint32_t longest_clusters; /* Longest free run seen so far. */
  uint32_t fat_sectors;      /* FAT sectors read. */
  uint32_t steps;
} StorageRunReport;

typedef struct StorageRunwayReport
{
  uint32_t cluster_bytes;
  uint32_t file_clusters;   /* Clusters the file already holds. */
  uint32_t free_clusters;   /* Verified free clusters the file can grow into. */
  uint32_t longest_gap;     /* Longest used stretch an allocation will cross. */
  uint32_t fat_sectors;     /* FAT sectors read ahead. */
  bool barrier;             /* No further cluster is reachable cheaply. */
} StorageRunwayReport;

void StorageService_Init(SD_HandleTypeDef *sd);
FRESULT StorageService_Acquire(StorageOwner owner);
FRESULT StorageService_Release(StorageOwner owner);
/* Erases the card with a new FAT32 volume, then mounts it once to verify and
 * unmounts. Needs no existing filesystem, only a free lease. SDHC/SDXC only.
 * Blocks for the whole format; callers must be idle maintenance paths. */
bool StorageService_Format(StorageFormatReport *report);
/* Reads card identity and ratings for SD INFO under the STATUS lease. Needs no
 * filesystem; the card is deinitialized again afterwards. */
FRESULT StorageService_ReadMediaInfo(SdMediaInfo *info);
/* Contiguous free-run search for preallocation (jjy.9). FatFs f_expand reads the
 * whole FAT in one call when no run lies near the allocation hint, which blocked
 * the foreground for seconds on a nearly full card. Begin starts at the hint;
 * each Step reads FAT sectors for about budget_ms (at least one sector) and the
 * search wraps once around the volume. Once found, the hint points at the run,
 * so f_expand finds it at once. Use the volume only through f_expand in between. */
StorageRunState StorageService_BeginRunSearch(StorageOwner owner, uint64_t bytes);
StorageRunState StorageService_StepRunSearch(StorageOwner owner, uint32_t budget_ms,
                                             StorageRunReport *report);
/* Runway ahead of a growing file (jjy.17). The file holds the contiguous
 * clusters from first_cluster for allocated_bytes; past them FatFs allocates by
 * scanning forward. Each Step reads one FAT sector (128 clusters) in that order
 * and counts the free clusters reachable across used gaps of at most
 * max_gap_sectors FAT sectors. Step returns false when the lease is lost or a
 * read fails. FAT32 only (STORAGE_RUN_UNSUPPORTED otherwise). */
StorageRunState StorageService_BeginRunway(StorageOwner owner, uint32_t first_cluster,
                                           uint64_t allocated_bytes,
                                           uint32_t max_gap_sectors,
                                           StorageRunwayReport *report);
bool StorageService_StepRunway(StorageOwner owner, StorageRunwayReport *report);
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
