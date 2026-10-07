#include "slice_map.h"

#include <stddef.h>
#include <string.h>

bool SliceMap_InitEqual(SliceMap *map, uint32_t clip_samples, uint8_t count)
{
  uint32_t slice;

  if (map == NULL)
  {
    return false;
  }
  (void)memset(map, 0, sizeof(*map));
  if ((count == 0U) || (count > SLICE_MAP_MAX_SLICES) || (clip_samples < count))
  {
    return false;
  }
  map->clip_samples = clip_samples;
  map->count = count;
  for (slice = 0U; slice < count; ++slice)
  {
    map->starts[slice] = (uint32_t)(((uint64_t)clip_samples * slice) / count);
    map->enabled[slice] = 1U;
  }
  return true;
}

uint32_t SliceMap_Start(const SliceMap *map, uint8_t slice)
{
  if ((map == NULL) || (slice >= map->count))
  {
    return 0U;
  }
  return map->starts[slice];
}

uint32_t SliceMap_End(const SliceMap *map, uint8_t slice)
{
  if ((map == NULL) || (slice >= map->count))
  {
    return 0U;
  }
  return ((uint32_t)slice + 1U < map->count) ? map->starts[slice + 1U] : map->clip_samples;
}

uint32_t SliceMap_Length(const SliceMap *map, uint8_t slice)
{
  const uint32_t start = SliceMap_Start(map, slice);
  const uint32_t end = SliceMap_End(map, slice);

  return (end > start) ? (end - start) : 0U;
}

bool SliceMap_Enabled(const SliceMap *map, uint8_t slice)
{
  return (map != NULL) && (slice < map->count) && (map->enabled[slice] != 0U);
}

bool SliceMap_SetLength(SliceMap *map, uint8_t slice, uint32_t length, uint32_t min_length)
{
  uint32_t start;
  uint32_t next_end;
  uint32_t end;

  if ((map == NULL) || ((uint32_t)slice + 1U >= map->count))
  {
    return false; /* outside the map, or the last slice */
  }
  start = map->starts[slice];
  next_end = SliceMap_End(map, (uint8_t)(slice + 1U));
  if ((next_end - start) < (2U * min_length))
  {
    return false; /* already at both minimums */
  }
  if (length < min_length)
  {
    length = min_length;
  }
  end = start + length;
  if (end > (next_end - min_length))
  {
    end = next_end - min_length;
  }
  if (end == map->starts[slice + 1U])
  {
    return false;
  }
  map->starts[slice + 1U] = end;
  return true;
}

uint8_t SliceMap_SliceAt(const SliceMap *map, uint32_t sample)
{
  uint8_t slice = 0U;

  if ((map == NULL) || (map->count == 0U))
  {
    return 0U;
  }
  while (((uint32_t)slice + 1U < map->count) && (map->starts[slice + 1U] <= sample))
  {
    ++slice;
  }
  return slice;
}

uint8_t SliceMap_Remap(const SliceMap *from, uint8_t slice, const SliceMap *to)
{
  uint64_t start;

  if ((from == NULL) || (to == NULL) || (from->count == 0U) || (to->count == 0U))
  {
    return 0U;
  }
  if (slice >= from->count)
  {
    slice = (uint8_t)(from->count - 1U);
  }
  start = from->starts[slice];
  /* The same instant in to's clip, should the two clips differ in length. */
  if ((from->clip_samples != 0U) && (from->clip_samples != to->clip_samples))
  {
    start = (start * to->clip_samples) / from->clip_samples;
  }
  return SliceMap_SliceAt(to, (uint32_t)start);
}

bool SliceMap_Valid(const SliceMap *map, uint32_t min_length)
{
  uint8_t slice;

  if ((map == NULL) || (map->count == 0U) || (map->count > SLICE_MAP_MAX_SLICES) ||
      (map->starts[0] != 0U))
  {
    return false;
  }
  for (slice = 0U; slice < map->count; ++slice)
  {
    const uint32_t start = map->starts[slice];
    const uint32_t end = SliceMap_End(map, slice);

    if ((end <= start) || ((end - start) < min_length))
    {
      return false;
    }
  }
  return true;
}
