#ifndef SPOOKY_FAT_RUN_H
#define SPOOKY_FAT_RUN_H

/* Bounded search for a run of free FAT32 clusters (jjy.9). FatFs f_expand scans
 * the whole FAT, one cluster at a time, when no run exists; on a nearly full
 * card that blocked RECORD START for seconds. A caller feeds this search one
 * 512-byte FAT sector at a time and stops at its own budget.
 *
 * Not yet linked into the firmware. Using it to skip preallocation at RECORD
 * START moved the allocation search into the recording and overran the queues
 * (docs/evidence/2026-10-01-prealloc-search-trial.md); the planned use is a
 * stepped search that finishes before capture starts (jjy.9). */

#include <stdbool.h>
#include <stdint.h>

#define FAT_RUN_SECTOR_BYTES 512U
#define FAT_RUN_ENTRIES_PER_SECTOR (FAT_RUN_SECTOR_BYTES / 4U)

typedef enum FatRunState
{
  FAT_RUN_SEARCHING = 0,
  FAT_RUN_FOUND,
  FAT_RUN_NOT_FOUND /* Every cluster examined without a long enough run. */
} FatRunState;

typedef struct FatRunSearch
{
  uint32_t n_fatent;   /* Clusters + 2, as FatFs counts them. */
  uint32_t needed;     /* Free clusters required in one run. */
  uint32_t next;       /* Next cluster to examine. */
  uint32_t examined;   /* Clusters examined so far. */
  uint32_t run_start;
  uint32_t run_length;
  FatRunState state;
} FatRunSearch;

/* Starts at start_cluster (clamped to the valid range 2..n_fatent-1) and wraps
 * once around the FAT. A run never spans the wrap. */
void FatRun_Init(FatRunSearch *search, uint32_t n_fatent, uint32_t start_cluster,
                 uint32_t needed);
/* FAT sector, relative to the FAT base, holding the next cluster to examine. */
uint32_t FatRun_NextSector(const FatRunSearch *search);
/* Examines the clusters of sector_index (as returned by FatRun_NextSector) from
 * the next one onward, stopping early when a run is found. */
FatRunState FatRun_Feed(FatRunSearch *search, const uint8_t *sector,
                        uint32_t sector_index);

#endif /* SPOOKY_FAT_RUN_H */
