#include "storage_lease.h"

#include <stddef.h>

void StorageLease_Init(StorageLease *lease)
{
  if (lease == NULL)
  {
    return;
  }
  lease->owner = STORAGE_OWNER_NONE;
  lease->acquisitions = 0U;
  lease->busy_rejections = 0U;
  lease->invalid_releases = 0U;
}

bool StorageLease_TryAcquire(StorageLease *lease, StorageOwner owner)
{
  if ((lease == NULL) || (owner <= STORAGE_OWNER_NONE) ||
      (owner >= STORAGE_OWNER_COUNT))
  {
    if (lease != NULL)
    {
      ++lease->busy_rejections;
    }
    return false;
  }
  if (lease->owner != STORAGE_OWNER_NONE)
  {
    ++lease->busy_rejections;
    return false;
  }
  lease->owner = owner;
  ++lease->acquisitions;
  return true;
}

bool StorageLease_Release(StorageLease *lease, StorageOwner owner)
{
  if ((lease == NULL) || (owner <= STORAGE_OWNER_NONE) ||
      (owner >= STORAGE_OWNER_COUNT) || (lease->owner != owner))
  {
    if (lease != NULL)
    {
      ++lease->invalid_releases;
    }
    return false;
  }
  lease->owner = STORAGE_OWNER_NONE;
  return true;
}

const char *StorageOwner_Name(StorageOwner owner)
{
  static const char *const names[] = {
    "NONE", "RECORDER", "SD_STRESS", "WAV_TRANSFER", "STATUS", "FORMAT"
  };
  return ((unsigned int)owner < (sizeof(names) / sizeof(names[0])))
    ? names[(unsigned int)owner] : "INVALID";
}
