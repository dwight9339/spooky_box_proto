#include "recording_limits.h"

#include <stddef.h>

static uint64_t data_bytes(uint32_t frames)
{
  return (uint64_t)frames * RECORDING_LIMIT_BLOCK_ALIGN;
}

uint64_t RecordingLimits_FreeBytes(const RecordingLimits *limits,
                                   uint32_t frames_written)
{
  uint64_t used;

  if (limits == NULL)
  {
    return 0U;
  }
  used = RECORDING_LIMIT_WAV_HEADER_BYTES + data_bytes(frames_written);
  return (used < limits->free_bytes_at_open)
    ? limits->free_bytes_at_open - used : 0U;
}

RecordingLimitReason RecordingLimits_BeforeBlock(
  const RecordingLimits *limits, uint32_t frames_written)
{
  uint64_t remaining;

  if (limits == NULL)
  {
    return RECORDING_LIMIT_CARD_FULL;
  }
  if ((frames_written + RECORDING_LIMIT_BLOCK_FRAMES) >
      RECORDING_LIMIT_WAV_MAX_FRAMES)
  {
    return RECORDING_LIMIT_FILE_SIZE;
  }
  remaining = RecordingLimits_FreeBytes(limits, frames_written);
  if ((remaining < RECORDING_LIMIT_BLOCK_BYTES) ||
      ((remaining - RECORDING_LIMIT_BLOCK_BYTES) < limits->reserve_bytes))
  {
    return RECORDING_LIMIT_CARD_FULL;
  }
  return RECORDING_LIMIT_NONE;
}

bool RecordingLimits_Init(RecordingLimits *limits, uint32_t timed_seconds,
                          uint64_t free_bytes, uint32_t reserve_seconds,
                          uint64_t rolling_reserve_bytes,
                          uint64_t finalize_reserve_bytes)
{
  uint64_t session_reserve;

  if ((limits == NULL) || (timed_seconds > (UINT32_MAX /
      RECORDING_LIMIT_SAMPLE_RATE_HZ)))
  {
    return false;
  }
  session_reserve = (uint64_t)reserve_seconds *
    RECORDING_LIMIT_SAMPLE_RATE_HZ * RECORDING_LIMIT_BLOCK_ALIGN;
  if ((rolling_reserve_bytes > (UINT64_MAX - session_reserve)) ||
      (finalize_reserve_bytes >
       (UINT64_MAX - session_reserve - rolling_reserve_bytes)))
  {
    return false;
  }
  limits->free_bytes_at_open = free_bytes;
  limits->reserve_bytes = session_reserve + rolling_reserve_bytes +
    finalize_reserve_bytes;
  limits->target_frames = timed_seconds * RECORDING_LIMIT_SAMPLE_RATE_HZ;
  return RecordingLimits_BeforeBlock(limits, 0U) == RECORDING_LIMIT_NONE;
}

bool RecordingLimits_DurationReached(const RecordingLimits *limits,
                                     uint32_t frames_written)
{
  return (limits != NULL) && (limits->target_frames != 0U) &&
    (frames_written >= limits->target_frames);
}
