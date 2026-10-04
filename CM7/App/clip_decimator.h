#ifndef SPOOKY_CLIP_DECIMATOR_H
#define SPOOKY_CLIP_DECIMATOR_H

/*
 * Demo-only Instrument clip preparation (decision 0011 item 15,
 * full_spooky_proto-p04.6). demo_clip.c reads a committed capture and this
 * module turns it into the clip: the mono sum of the two radio channels,
 * decimated from 48 kHz to 24 kHz, 16-bit, at most 3 s (144,000 bytes), ending
 * at the save point. It does no I/O.
 *
 * - Range selection: the newest CLIP_SOURCE_FRAMES frames of a capture, read
 *   from its newest segments (at most CLIP_RANGE_MAX_PARTS; every segment but
 *   the newest is a full 5.461 s, so two always reach 3 s). The frame count is
 *   even, so the clip ends exactly at the save point.
 * - Decimation: (L + R) / 2, then a 39-tap halfband low-pass (Kaiser, beta 7.5:
 *   passband flat to 0.01 dB below 9 kHz, at least 73 dB down from 15 kHz) and
 *   every second output. The filter starts from the first frame repeated, so the
 *   clip has no step at its start; its delay is 19 source frames (0.4 ms).
 *
 * Portable C with no HAL or FatFs calls.
 */

#include <stdbool.h>
#include <stdint.h>

#define CLIP_SOURCE_RATE_HZ 48000U
#define CLIP_RATE_HZ 24000U
#define CLIP_MAX_SECONDS 3U
#define CLIP_MAX_SAMPLES (CLIP_RATE_HZ * CLIP_MAX_SECONDS)                 /* 72,000 */
#define CLIP_MAX_BYTES (CLIP_MAX_SAMPLES * 2U)                             /* 144,000 */
#define CLIP_SOURCE_FRAMES (CLIP_MAX_SAMPLES * (CLIP_SOURCE_RATE_HZ / CLIP_RATE_HZ))
/* Capture frames: radio left, radio right, microphone (the recorder's layout). */
#define CLIP_SOURCE_CHANNELS 3U
#define CLIP_RANGE_MAX_PARTS 2U
#define CLIP_DECIMATOR_TAPS 39U

_Static_assert(CLIP_MAX_BYTES == 144000U, "decision 0011 item 15: at most 144,000 bytes");
_Static_assert((CLIP_SOURCE_RATE_HZ % CLIP_RATE_HZ) == 0U, "an integer decimation factor");

typedef struct
{
  uint8_t segment;       /* index of the segment in the capture, oldest first */
  uint32_t first_frame;  /* first frame read from that segment */
  uint32_t frames;
} ClipRangePart;

typedef struct
{
  uint8_t count;                              /* parts, in playback order */
  ClipRangePart parts[CLIP_RANGE_MAX_PARTS];
  uint32_t frames;                            /* source frames, even */
} ClipRange;

typedef struct
{
  int32_t history[CLIP_DECIMATOR_TAPS * 2U]; /* (L + R), written twice: one window */
  uint32_t next;                             /* where the next frame goes */
  bool primed;
  bool odd;                                  /* one frame of the next pair is in */
} ClipDecimator;

/* The newest min(wanted, held) frames of a capture whose segments hold
 * segment_frames[0..count-1] frames (oldest first), from at most
 * CLIP_RANGE_MAX_PARTS segments, rounded down to an even count. False when that
 * leaves nothing. Empty segments are skipped. */
bool ClipRange_Select(const uint32_t *segment_frames, uint32_t segment_count,
                      uint32_t wanted_frames, ClipRange *range);

void ClipDecimator_Init(ClipDecimator *decimator);
/* Takes frame_count source frames (CLIP_SOURCE_CHANNELS interleaved int16 each)
 * and writes one clip sample per two frames, up to capacity. Returns the samples
 * written. Chunk boundaries do not change the output. */
uint32_t ClipDecimator_Process(ClipDecimator *decimator, const int16_t *frames,
                               uint32_t frame_count, int16_t *out, uint32_t capacity);

#endif /* SPOOKY_CLIP_DECIMATOR_H */
