#ifndef SPOOKY_STORAGE_LEASE_H
#define SPOOKY_STORAGE_LEASE_H

/* Portable exclusive-ownership state for the single FatFs volume. */

#include <stdbool.h>
#include <stdint.h>

typedef enum StorageOwner
{
  STORAGE_OWNER_NONE = 0,
  STORAGE_OWNER_RECORDER,
  STORAGE_OWNER_SD_STRESS,
  STORAGE_OWNER_WAV_TRANSFER,
  STORAGE_OWNER_STATUS,
  STORAGE_OWNER_FORMAT,
  STORAGE_OWNER_COUNT
} StorageOwner;

typedef struct StorageLease
{
  StorageOwner owner;
  uint32_t acquisitions;
  uint32_t busy_rejections;
  uint32_t invalid_releases;
} StorageLease;

void StorageLease_Init(StorageLease *lease);
bool StorageLease_TryAcquire(StorageLease *lease, StorageOwner owner);
bool StorageLease_Release(StorageLease *lease, StorageOwner owner);
const char *StorageOwner_Name(StorageOwner owner);

#endif /* SPOOKY_STORAGE_LEASE_H */
