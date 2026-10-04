#include "rolling_catalog.h"

#include <stddef.h>
#include <string.h>

void RollingCatalog_Init(RollingCatalog *catalog)
{
  if (catalog == NULL)
  {
    return;
  }
  (void)memset(catalog, 0, sizeof(*catalog));
  catalog->writing = -1;
}

void RollingCatalog_Discard(RollingCatalog *catalog)
{
  uint32_t slot;

  if (catalog == NULL)
  {
    return;
  }
  for (slot = 0U; slot < ROLLING_SLOTS; ++slot)
  {
    if (catalog->slots[slot].state != (uint8_t)ROLLING_SLOT_PINNED)
    {
      catalog->slots[slot].state = (uint8_t)ROLLING_SLOT_EMPTY;
      catalog->slots[slot].blocks = 0U;
    }
  }
  catalog->writing = -1;
}

int RollingCatalog_BeginSegment(RollingCatalog *catalog)
{
  int chosen = -1;
  uint32_t slot;

  if ((catalog == NULL) || (catalog->writing >= 0))
  {
    return -1;
  }
  for (slot = 0U; (slot < ROLLING_SLOTS) && (chosen < 0); ++slot)
  {
    if (catalog->slots[slot].state == (uint8_t)ROLLING_SLOT_EMPTY)
    {
      chosen = (int)slot;
    }
  }
  if (chosen < 0)
  {
    for (slot = 0U; slot < ROLLING_SLOTS; ++slot)
    {
      const RollingSlot *candidate = &catalog->slots[slot];

      if ((candidate->state == (uint8_t)ROLLING_SLOT_COMPLETE) &&
          ((chosen < 0) ||
           ((int32_t)(candidate->sequence - catalog->slots[chosen].sequence) < 0)))
      {
        chosen = (int)slot;
      }
    }
    if (chosen >= 0)
    {
      ++catalog->segments_reclaimed;
    }
  }
  if (chosen < 0)
  {
    ++catalog->allocation_failures;
    return -1;
  }
  catalog->slots[chosen].state = (uint8_t)ROLLING_SLOT_WRITING;
  catalog->slots[chosen].blocks = 0U;
  catalog->slots[chosen].sequence = catalog->next_sequence++;
  catalog->writing = (int8_t)chosen;
  ++catalog->segments_started;
  return chosen;
}

bool RollingCatalog_BlockWritten(RollingCatalog *catalog)
{
  RollingSlot *slot;

  if ((catalog == NULL) || (catalog->writing < 0))
  {
    return false;
  }
  slot = &catalog->slots[catalog->writing];
  if (slot->blocks < ROLLING_SEGMENT_BLOCKS)
  {
    ++slot->blocks;
  }
  return slot->blocks >= ROLLING_SEGMENT_BLOCKS;
}

void RollingCatalog_EndSegment(RollingCatalog *catalog)
{
  RollingSlot *slot;

  if ((catalog == NULL) || (catalog->writing < 0))
  {
    return;
  }
  slot = &catalog->slots[catalog->writing];
  slot->state = (uint8_t)((slot->blocks != 0U) ? ROLLING_SLOT_COMPLETE : ROLLING_SLOT_EMPTY);
  catalog->writing = -1;
}

uint32_t RollingCatalog_RetainedBlocks(const RollingCatalog *catalog)
{
  uint32_t blocks = 0U;
  uint32_t slot;

  if (catalog == NULL)
  {
    return 0U;
  }
  for (slot = 0U; slot < ROLLING_SLOTS; ++slot)
  {
    const uint8_t state = catalog->slots[slot].state;

    if ((state == (uint8_t)ROLLING_SLOT_COMPLETE) || (state == (uint8_t)ROLLING_SLOT_WRITING))
    {
      blocks += catalog->slots[slot].blocks;
    }
  }
  return blocks;
}

RollingSaveAdmission RollingCatalog_CheckSave(const RollingCatalog *catalog)
{
  if (catalog == NULL)
  {
    return ROLLING_SAVE_UNAVAILABLE;
  }
  if (catalog->save_active)
  {
    return ROLLING_SAVE_BUSY;
  }
  return (RollingCatalog_RetainedBlocks(catalog) == 0U) ? ROLLING_SAVE_UNAVAILABLE
                                                        : ROLLING_SAVE_ACCEPTED;
}

RollingSaveAdmission RollingCatalog_PinWindow(RollingCatalog *catalog)
{
  const RollingSaveAdmission admission = RollingCatalog_CheckSave(catalog);
  uint8_t newest_first[ROLLING_SLOTS];
  uint32_t count = 0U;
  uint32_t blocks = 0U;
  uint32_t index;

  if (admission != ROLLING_SAVE_ACCEPTED)
  {
    return admission;
  }
  /* Walk the complete segments from the newest back until the window is held. */
  while (blocks < ROLLING_WINDOW_BLOCKS)
  {
    int newest = -1;
    uint32_t slot;

    for (slot = 0U; slot < ROLLING_SLOTS; ++slot)
    {
      const RollingSlot *candidate = &catalog->slots[slot];

      if ((candidate->state == (uint8_t)ROLLING_SLOT_COMPLETE) &&
          ((newest < 0) ||
           ((int32_t)(candidate->sequence - catalog->slots[newest].sequence) > 0)))
      {
        newest = (int)slot;
      }
    }
    if (newest < 0)
    {
      break; /* everything retained is pinned */
    }
    catalog->slots[newest].state = (uint8_t)ROLLING_SLOT_PINNED;
    blocks += catalog->slots[newest].blocks;
    newest_first[count++] = (uint8_t)newest;
  }
  if (count == 0U)
  {
    return ROLLING_SAVE_UNAVAILABLE; /* only a segment being written holds data */
  }
  for (index = 0U; index < count; ++index)
  {
    catalog->pinned[index] = newest_first[count - 1U - index];
  }
  catalog->pinned_count = (uint8_t)count;
  catalog->pinned_blocks = blocks;
  catalog->save_active = true;
  return ROLLING_SAVE_ACCEPTED;
}

void RollingCatalog_SlotReleased(RollingCatalog *catalog, uint8_t slot)
{
  if ((catalog == NULL) || (slot >= ROLLING_SLOTS) ||
      (catalog->slots[slot].state != (uint8_t)ROLLING_SLOT_PINNED))
  {
    return;
  }
  catalog->slots[slot].state = (uint8_t)ROLLING_SLOT_EMPTY;
  catalog->slots[slot].blocks = 0U;
}

void RollingCatalog_SaveDone(RollingCatalog *catalog)
{
  uint32_t slot;

  if (catalog == NULL)
  {
    return;
  }
  for (slot = 0U; slot < ROLLING_SLOTS; ++slot)
  {
    RollingCatalog_SlotReleased(catalog, (uint8_t)slot);
  }
  catalog->save_active = false;
  catalog->pinned_count = 0U;
  catalog->pinned_blocks = 0U;
}
