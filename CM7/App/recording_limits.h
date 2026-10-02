#ifndef SPOOKY_RECORDING_LIMITS_H
#define SPOOKY_RECORDING_LIMITS_H

/* Portable recording-ending arithmetic (decision 0008 items 2-4).
 * The recorder obtains free_bytes once when it opens the volume. No filesystem
 * query belongs on the per-block recording path. */

#include <stdbool.h>
#include <stdint.h>

#define RECORDING_LIMIT_SAMPLE_RATE_HZ       48000U
#define RECORDING_LIMIT_CHANNELS                 3U
#define RECORDING_LIMIT_BYTES_PER_SAMPLE         2U
#define RECORDING_LIMIT_BLOCK_FRAMES          4096U
#define RECORDING_LIMIT_BLOCK_ALIGN \
  (RECORDING_LIMIT_CHANNELS * RECORDING_LIMIT_BYTES_PER_SAMPLE)
#define RECORDING_LIMIT_BLOCK_BYTES \
  (RECORDING_LIMIT_BLOCK_FRAMES * RECORDING_LIMIT_BLOCK_ALIGN)
#define RECORDING_LIMIT_WAV_HEADER_BYTES        44U
#ifndef SPOOKY_RECORDING_WAV_MAX_FRAMES
#define SPOOKY_RECORDING_WAV_MAX_FRAMES \
  ((UINT32_MAX - 36U) / RECORDING_LIMIT_BLOCK_ALIGN)
#endif
#define RECORDING_LIMIT_WAV_MAX_FRAMES SPOOKY_RECORDING_WAV_MAX_FRAMES

typedef enum RecordingLimitReason
{
  RECORDING_LIMIT_NONE = 0,
  RECORDING_LIMIT_DURATION,
  RECORDING_LIMIT_CARD_FULL,
  RECORDING_LIMIT_FILE_SIZE,
  RECORDING_LIMIT_ALLOCATION /* The next block needs space not verified cheap to allocate. */
} RecordingLimitReason;

typedef struct RecordingLimits
{
  uint64_t free_bytes_at_open;
  uint64_t reserve_bytes;
  uint32_t target_frames; /* zero means open-ended */
  uint64_t allocation_bytes; /* file bytes safe to write (jjy.17); zero: no limit */
} RecordingLimits;

/* timed_seconds is zero for an open-ended session. The reserve is one configured
 * session margin plus the rolling-capture and finalize contributions. */
bool RecordingLimits_Init(RecordingLimits *limits, uint32_t timed_seconds,
                          uint64_t free_bytes, uint32_t reserve_seconds,
                          uint64_t rolling_reserve_bytes,
                          uint64_t finalize_reserve_bytes);

/* Check before writing another matched block. A non-NONE result means finalize at
 * the current matched boundary without issuing that write. */
RecordingLimitReason RecordingLimits_BeforeBlock(
  const RecordingLimits *limits, uint32_t frames_written);

/* Sets how many file bytes, header included, the recording may fill without an
 * allocation search on the recording path (jjy.17). Init leaves it unlimited. */
void RecordingLimits_SetAllocation(RecordingLimits *limits, uint64_t bytes);

/* File bytes, header included, once frames_written frames are written. */
uint64_t RecordingLimits_FileBytes(uint32_t frames_written);

/* Check the duration after a successfully written matched block. */
bool RecordingLimits_DurationReached(const RecordingLimits *limits,
                                     uint32_t frames_written);

uint64_t RecordingLimits_FreeBytes(const RecordingLimits *limits,
                                   uint32_t frames_written);

#endif /* SPOOKY_RECORDING_LIMITS_H */
