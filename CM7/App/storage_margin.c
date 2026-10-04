#include "storage_margin.h"

#include <stddef.h>

void StorageMargin_Reset(StorageMargin *margin)
{
  if (margin != NULL)
  {
    margin->low = false;
    margin->queue_high_water = 0U;
    margin->write_ms = 0U;
  }
}

bool StorageMargin_Update(StorageMargin *margin, const StorageMarginLimits *limits,
                          uint32_t queue_high_water, uint32_t write_ms)
{
  if ((margin == NULL) || (limits == NULL) || margin->low)
  {
    return false;
  }
  if ((queue_high_water < limits->queue_blocks) && (write_ms < limits->write_ms))
  {
    return false;
  }
  margin->low = true;
  margin->queue_high_water = queue_high_water;
  margin->write_ms = write_ms;
  return true;
}
