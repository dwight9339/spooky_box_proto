#ifndef SPOOKY_AUDIO_TIMELINE_H
#define SPOOKY_AUDIO_TIMELINE_H
#include <stdbool.h>
#include <stdint.h>

/* Common audio sample timeline (decision 0012; Beads full_spooky_proto-hpq.1).
 *
 * Portable arithmetic only: nothing here touches hardware or runs in an ISR.
 * The caller reads the radio DMA counters under its own interrupt policy and
 * passes them in. The caller also serializes all calls on one stream.
 *
 * A stream position counts radio frames since the radio stream started (one
 * epoch). The radio DMA fills a circular buffer of two halves, and every
 * completed half adds frames_per_half frames. Halves replaced by silence while
 * the stream gate is closed (decision 0004) still count. Sessions, captures and
 * event stamps are all expressed on this count, so they share one timeline. */

typedef enum
{
  AUDIO_TIMELINE_OK = 0,
  AUDIO_TIMELINE_ERR_ARGUMENT,
  AUDIO_TIMELINE_ERR_NOT_STARTED,
  AUDIO_TIMELINE_ERR_STALE_EPOCH,   /* position belongs to an earlier stream */
  AUDIO_TIMELINE_ERR_BACKWARDS,     /* completed-half count moved backwards */
  AUDIO_TIMELINE_ERR_BEFORE_ORIGIN
} AudioTimelineResult;

/* Epoch 0 is never issued, so a zeroed position is invalid. */
typedef struct
{
  uint32_t epoch;
  uint64_t frame;
} AudioTimelinePosition;

typedef struct
{
  uint32_t epoch;
  uint32_t frames_per_half;
  uint32_t items_per_frame;   /* DMA data items per frame: 2 for 16-bit stereo */
  uint32_t raw_base;          /* raw completed-half count when the stream started */
  uint64_t completed_halves;  /* halves counted since start, extended to 64 bits */
} AudioTimelineStream;

/* Start alignment for one capture. The recorder drops trim_frames radio frames,
 * counted from first_half_frame (the start of the half in progress when the
 * microphone was started), so that radio frame 0 and microphone sample 0 both
 * sit at origin. */
typedef struct
{
  AudioTimelinePosition origin;
  uint64_t first_half_frame;
  uint32_t trim_frames;
} AudioTimelineAlignment;

/* The event occurred within [position.frame - uncertainty_frames,
 * position.frame]: position is when it was observed, and uncertainty_frames is
 * the declared worst-case latency of the source that observed it. */
typedef struct
{
  AudioTimelinePosition position;
  uint32_t uncertainty_frames;
} AudioTimelineStamp;

/* Extends a wrapping 32-bit counter to the 64-bit value nearest reference.
 * Results that would fall below zero saturate at zero. */
uint64_t AudioTimeline_Extend32(uint64_t reference, uint32_t raw);

/* Starts a new epoch. raw_completed_halves is the caller's free-running
 * completed-half count at the moment the radio DMA started at index 0. */
AudioTimelineResult AudioTimeline_StreamStart(AudioTimelineStream *stream,
                                              uint32_t frames_per_half,
                                              uint32_t items_per_frame,
                                              uint32_t raw_completed_halves);

/* Resolves a DMA snapshot into a stream position. remaining_items is the DMA
 * down-counter (NDTR) for the whole circular buffer. Both values must be read
 * with the completion interrupt masked, for less than one half period: a
 * completion that is pending but not yet counted is detected from the half the
 * DMA is filling and added here. A backwards count leaves the stream unchanged
 * and reports AUDIO_TIMELINE_ERR_BACKWARDS; the caller must start a new epoch. */
AudioTimelineResult AudioTimeline_StreamObserve(AudioTimelineStream *stream,
                                                uint32_t raw_completed_halves,
                                                uint32_t remaining_items,
                                                AudioTimelinePosition *position);

/* mic_start is the position observed immediately before the microphone DMA
 * was started, with radio capture enabled atomically with that observation.
 * mic_latency_frames is the measured constant from that start to the first
 * microphone sample. */
AudioTimelineResult AudioTimeline_AlignStart(const AudioTimelineStream *stream,
                                             AudioTimelinePosition mic_start,
                                             uint32_t mic_latency_frames,
                                             AudioTimelineAlignment *alignment);

/* Frames from origin to position on the same epoch. */
AudioTimelineResult AudioTimeline_Offset(AudioTimelinePosition origin,
                                         AudioTimelinePosition position,
                                         uint64_t *offset);

/* Converts a stamp to an offset from origin. The uncertainty is clipped at the
 * origin: an event observed after the origin cannot have occurred before it on
 * this timeline. */
AudioTimelineResult AudioTimeline_StampOffset(AudioTimelinePosition origin,
                                              AudioTimelineStamp stamp,
                                              uint64_t *latest_offset,
                                              uint32_t *uncertainty_frames);
#endif
