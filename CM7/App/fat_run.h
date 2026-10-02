#ifndef SPOOKY_FAT_RUN_H
#define SPOOKY_FAT_RUN_H

/* Bounded search for a run of free FAT32 clusters (jjy.9). FatFs f_expand scans
 * the whole FAT, one cluster at a time, when no run exists; on a nearly full
 * card that blocked RECORD START for seconds. The storage service feeds this
 * search one 512-byte FAT sector at a time, a few sectors per foreground pass,
 * and the recorder starts capture only after it ends. Skipping the search
 * instead moved FatFs's own search into the recording and overran the queues
 * (docs/evidence/2026-10-01-prealloc-search-trial.md). */

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
  uint32_t longest;    /* Longest free run seen so far, in clusters. */
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

/* Runway ahead of a growing file (jjy.17). When a write needs a cluster past the
 * file's allocation, FatFs scans forward from the file's last cluster, wrapping
 * at the end of the FAT, and takes the first free cluster: a long used stretch
 * makes that one f_write slow. Fed the FAT in that same order, the runway counts
 * the free clusters the file can grow into, crossing used gaps of at most
 * max_gap clusters, and stops at a longer gap or after one full lap. */
typedef enum FatRunwayState
{
  FAT_RUNWAY_SCANNING = 0,
  FAT_RUNWAY_BARRIER /* No further free cluster is reachable cheaply. */
} FatRunwayState;

typedef struct FatRunway
{
  uint32_t n_fatent;
  uint32_t max_gap;     /* Longest used stretch one allocation may cross. */
  uint32_t next;        /* Next cluster in FatFs allocation order. */
  uint32_t examined;
  uint32_t free;        /* Free clusters reachable before the barrier. */
  uint32_t gap;         /* Used clusters since the last free one. */
  uint32_t longest_gap; /* Longest gap followed by a free cluster. */
  FatRunwayState state;
} FatRunway;

/* start_cluster is the cluster after the file's last one. */
void FatRunway_Init(FatRunway *runway, uint32_t n_fatent, uint32_t start_cluster,
                    uint32_t max_gap);
uint32_t FatRunway_NextSector(const FatRunway *runway);
FatRunwayState FatRunway_Feed(FatRunway *runway, const uint8_t *sector,
                              uint32_t sector_index);

#endif /* SPOOKY_FAT_RUN_H */
