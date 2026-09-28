#include "storage_lease.h"

#include <assert.h>
#include <string.h>

int main(void)
{
  StorageLease lease;

  StorageLease_Init(&lease);
  assert(lease.owner == STORAGE_OWNER_NONE);
  assert(lease.acquisitions == 0U);
  assert(StorageLease_TryAcquire(&lease, STORAGE_OWNER_RECORDER));
  assert(lease.owner == STORAGE_OWNER_RECORDER);
  assert(lease.acquisitions == 1U);

  assert(!StorageLease_TryAcquire(&lease, STORAGE_OWNER_WAV_TRANSFER));
  assert(!StorageLease_TryAcquire(&lease, STORAGE_OWNER_RECORDER));
  assert(lease.busy_rejections == 2U);
  assert(lease.owner == STORAGE_OWNER_RECORDER);

  assert(!StorageLease_Release(&lease, STORAGE_OWNER_SD_STRESS));
  assert(lease.invalid_releases == 1U);
  assert(lease.owner == STORAGE_OWNER_RECORDER);
  assert(StorageLease_Release(&lease, STORAGE_OWNER_RECORDER));
  assert(lease.owner == STORAGE_OWNER_NONE);

  assert(StorageLease_TryAcquire(&lease, STORAGE_OWNER_SD_STRESS));
  assert(StorageLease_Release(&lease, STORAGE_OWNER_SD_STRESS));
  assert(StorageLease_TryAcquire(&lease, STORAGE_OWNER_WAV_TRANSFER));
  assert(StorageLease_Release(&lease, STORAGE_OWNER_WAV_TRANSFER));
  assert(StorageLease_TryAcquire(&lease, STORAGE_OWNER_STATUS));
  assert(StorageLease_Release(&lease, STORAGE_OWNER_STATUS));
  assert(lease.acquisitions == 4U);

  assert(!StorageLease_TryAcquire(&lease, STORAGE_OWNER_NONE));
  assert(!StorageLease_TryAcquire(&lease, STORAGE_OWNER_COUNT));
  assert(!StorageLease_Release(&lease, STORAGE_OWNER_NONE));
  assert(strcmp(StorageOwner_Name(STORAGE_OWNER_RECORDER), "RECORDER") == 0);
  assert(strcmp(StorageOwner_Name(STORAGE_OWNER_COUNT), "INVALID") == 0);
  return 0;
}
