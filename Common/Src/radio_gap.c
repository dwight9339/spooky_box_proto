#include "radio_gap.h"

#include <string.h>

void RadioGap_Reset(RadioGap *gap)
{
  if (gap != NULL)
  {
    gap->open = false;
    gap->first_block = 0U;
    gap->last_block = 0U;
    gap->opened = 0U;
    gap->closed = 0U;
  }
}

void RadioGap_OnSilencedBlock(RadioGap *gap, uint32_t block)
{
  if (gap == NULL)
  {
    return;
  }
  if (!gap->open)
  {
    gap->open = true;
    gap->first_block = block;
    ++gap->opened;
  }
  gap->last_block = block;
}

void RadioGap_Close(RadioGap *gap)
{
  if ((gap != NULL) && gap->open)
  {
    gap->open = false;
    ++gap->closed;
  }
}

void RadioGap_Snapshot(const RadioGap *gap, RadioGapSnapshot *snapshot)
{
  if ((gap == NULL) || (snapshot == NULL))
  {
    return;
  }
  snapshot->open = gap->open;
  snapshot->first_block = gap->first_block;
  snapshot->last_block = gap->last_block;
  snapshot->opened = gap->opened;
  snapshot->closed = gap->closed;
}

void RadioGap_ResetReporter(RadioGapReporter *reporter, const RadioGapSnapshot *now)
{
  if ((reporter == NULL) || (now == NULL))
  {
    return;
  }
  reporter->starts_reported = now->opened;
  reporter->ends_reported = now->closed;
  reporter->missed = 0U;
}

RadioGapEdges RadioGap_NewEdges(RadioGapReporter *reporter, const RadioGapSnapshot *now)
{
  RadioGapEdges edges;

  (void)memset(&edges, 0, sizeof(edges));
  if ((reporter == NULL) || (now == NULL))
  {
    return edges;
  }
  edges.first_frame = (uint64_t)now->first_block * RADIO_GAP_FRAMES_PER_BLOCK;
  edges.last_frame = ((uint64_t)now->last_block * RADIO_GAP_FRAMES_PER_BLOCK) +
                     (RADIO_GAP_FRAMES_PER_BLOCK - 1U);
  /* Only the latest gap's blocks are known. Its start is new when a gap began
   * since the last poll; its end is new when it is closed and a close happened
   * since. Every other unreported edge belonged to an older gap and is lost. */
  {
    const uint32_t new_starts = now->opened - reporter->starts_reported;
    const uint32_t new_ends = now->closed - reporter->ends_reported;
    uint32_t lost;

    edges.start = new_starts != 0U;
    edges.end = (new_ends != 0U) && !now->open;
    lost = (new_starts - (edges.start ? 1U : 0U)) + (new_ends - (edges.end ? 1U : 0U));
    reporter->missed += lost;
    reporter->starts_reported = now->opened;
    reporter->ends_reported = now->closed;
  }
  return edges;
}
