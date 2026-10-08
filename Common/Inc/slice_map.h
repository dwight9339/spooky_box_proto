#ifndef SPOOKY_SLICE_MAP_H
#define SPOOKY_SLICE_MAP_H

/*
 * The Slicer's slice map (decision 0022 items 1, 2, 7, 11 and 13): an ordered
 * list of slices covering a clip without gaps or overlap. Slice n starts at
 * starts[n] and ends where slice n+1 starts; the first slice starts at 0 and
 * the last ends at the clip's end. Each slice has an enabled flag; a disabled
 * slice is silent wherever it would play.
 *
 * A shared data type (0021 item 5) so other engines may read it later. Equal
 * 4, 8 or 16 slicing is only how a map is initialized (0022 option 2).
 *
 * Not thread-safe: the owner copies a whole map to the render context.
 * Portable C with no HAL calls.
 */

#include <stdbool.h>
#include <stdint.h>

#define SLICE_MAP_MAX_SLICES 16U

typedef struct
{
  uint32_t starts[SLICE_MAP_MAX_SLICES]; /* clip samples; starts[0] is 0 */
  uint32_t clip_samples;                 /* the end of the last slice */
  uint8_t count;                         /* 1..SLICE_MAP_MAX_SLICES */
  uint8_t enabled[SLICE_MAP_MAX_SLICES]; /* nonzero: the slice sounds */
  uint8_t reserved[3];
} SliceMap;

/* count equal slices over clip_samples, all enabled; the last slice takes the
 * remainder. False, with an empty map (count 0), for a count outside
 * 1..SLICE_MAP_MAX_SLICES or a clip shorter than count samples. */
bool SliceMap_InitEqual(SliceMap *map, uint32_t clip_samples, uint8_t count);
/* The boundaries of a slice; 0 for a slice outside the map. */
uint32_t SliceMap_Start(const SliceMap *map, uint8_t slice);
uint32_t SliceMap_End(const SliceMap *map, uint8_t slice);
uint32_t SliceMap_Length(const SliceMap *map, uint8_t slice);
bool SliceMap_Enabled(const SliceMap *map, uint8_t slice);
/* Moves the end of a slice, which is also the next slice's start (0022 item 6),
 * so that the slice is length samples long, held so that both it and the next
 * slice keep at least min_length. The last slice's end is the clip end and does
 * not move. Returns true if the boundary moved. */
bool SliceMap_SetLength(SliceMap *map, uint8_t slice, uint32_t length, uint32_t min_length);
/* The slice holding a clip sample; the last slice for a sample past the end. */
uint8_t SliceMap_SliceAt(const SliceMap *map, uint32_t sample);
/* 0022 item 11: the slice of to that holds the start of from's slice (nearest
 * neighbour by time, not by index). */
uint8_t SliceMap_Remap(const SliceMap *from, uint8_t slice, const SliceMap *to);
/* The map's invariants: a count in range, slice 0 at 0, every slice at least
 * min_length long (so none overlaps the next) and the last ending inside the
 * clip. */
bool SliceMap_Valid(const SliceMap *map, uint32_t min_length);

#endif /* SPOOKY_SLICE_MAP_H */
