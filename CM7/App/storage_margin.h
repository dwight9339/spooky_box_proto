#ifndef SPOOKY_STORAGE_MARGIN_H
#define SPOOKY_STORAGE_MARGIN_H

/* Decision 0014 item 5: a recording's storage margin is low once an audio
 * queue's high-water mark or a single SD write reaches its threshold. The
 * warning latches for the rest of the recording and does not stop it. */

#include <stdbool.h>
#include <stdint.h>

typedef struct StorageMarginLimits
{
  uint32_t queue_blocks; /* Queue high-water that counts as low margin. */
  uint32_t write_ms;     /* Single-write duration that counts as low margin. */
} StorageMarginLimits;

typedef struct StorageMargin
{
  bool low;
  uint32_t queue_high_water; /* Values when the margin first became low. */
  uint32_t write_ms;
} StorageMargin;

void StorageMargin_Reset(StorageMargin *margin);
/* Feeds the current queue high-water mark and the last write; returns true only
 * on the update that first finds the margin low. */
bool StorageMargin_Update(StorageMargin *margin, const StorageMarginLimits *limits,
                          uint32_t queue_high_water, uint32_t write_ms);

#endif /* SPOOKY_STORAGE_MARGIN_H */
