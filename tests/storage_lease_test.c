#include "storage_lease.h"

#include <stdio.h>
#include <string.h>

static unsigned failures;

/* Unlike assert(), these checks remain active in Release builds. */
#define CHECK(condition) do {                                               \
    if (!(condition)) {                                                     \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);       \
        ++failures;                                                         \
    }                                                                       \
} while (0)

int main(void)
{
  StorageLease lease;

  StorageLease_Init(&lease);
  CHECK(lease.owner == STORAGE_OWNER_NONE);
  CHECK(lease.acquisitions == 0U);
  CHECK(StorageLease_TryAcquire(&lease, STORAGE_OWNER_RECORDER));
  CHECK(lease.owner == STORAGE_OWNER_RECORDER);
  CHECK(lease.acquisitions == 1U);

  CHECK(!StorageLease_TryAcquire(&lease, STORAGE_OWNER_WAV_TRANSFER));
  CHECK(!StorageLease_TryAcquire(&lease, STORAGE_OWNER_RECORDER));
  CHECK(lease.busy_rejections == 2U);
  CHECK(lease.owner == STORAGE_OWNER_RECORDER);

  CHECK(!StorageLease_Release(&lease, STORAGE_OWNER_SD_STRESS));
  CHECK(lease.invalid_releases == 1U);
  CHECK(lease.owner == STORAGE_OWNER_RECORDER);
  CHECK(StorageLease_Release(&lease, STORAGE_OWNER_RECORDER));
  CHECK(lease.owner == STORAGE_OWNER_NONE);

  CHECK(StorageLease_TryAcquire(&lease, STORAGE_OWNER_SD_STRESS));
  CHECK(StorageLease_Release(&lease, STORAGE_OWNER_SD_STRESS));
  CHECK(StorageLease_TryAcquire(&lease, STORAGE_OWNER_WAV_TRANSFER));
  CHECK(StorageLease_Release(&lease, STORAGE_OWNER_WAV_TRANSFER));
  CHECK(StorageLease_TryAcquire(&lease, STORAGE_OWNER_STATUS));
  CHECK(StorageLease_Release(&lease, STORAGE_OWNER_STATUS));
  CHECK(lease.acquisitions == 4U);

  CHECK(!StorageLease_TryAcquire(&lease, STORAGE_OWNER_NONE));
  CHECK(!StorageLease_TryAcquire(&lease, STORAGE_OWNER_COUNT));
  CHECK(!StorageLease_Release(&lease, STORAGE_OWNER_NONE));
  CHECK(strcmp(StorageOwner_Name(STORAGE_OWNER_RECORDER), "RECORDER") == 0);
  CHECK(strcmp(StorageOwner_Name(STORAGE_OWNER_FORMAT), "FORMAT") == 0);
  CHECK(strcmp(StorageOwner_Name(STORAGE_OWNER_COUNT), "INVALID") == 0);

  if (failures != 0U)
  {
    printf("%u failure(s)\n", failures);
    return 1;
  }
  printf("storage_lease_test: all checks passed\n");
  return 0;
}
