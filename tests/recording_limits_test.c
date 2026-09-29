#include <stdio.h>

#include "recording_limits.h"

static int failures;
static const char *current_test;

#define CHECK(condition)                                                         \
  do {                                                                           \
    if (!(condition)) {                                                          \
      printf("FAIL %s (line %d): %s\n", current_test, __LINE__, #condition);   \
      ++failures;                                                                \
    }                                                                            \
  } while (0)

static uint64_t minute_bytes(void)
{
  return 60ULL * RECORDING_LIMIT_SAMPLE_RATE_HZ *
    RECORDING_LIMIT_BLOCK_ALIGN;
}

static void open_ended_has_no_duration_target(void)
{
  RecordingLimits limits;
  CHECK(RecordingLimits_Init(&limits, 0U, 1000000000ULL, 60U, 0U, 0U));
  CHECK(limits.target_frames == 0U);
  CHECK(!RecordingLimits_DurationReached(&limits, UINT32_MAX));
}

static void timed_sessions_keep_the_requested_target(void)
{
  RecordingLimits limits;
  CHECK(RecordingLimits_Init(&limits, 3600U, 2000000000ULL, 60U, 0U, 0U));
  CHECK(limits.target_frames == 3600U * RECORDING_LIMIT_SAMPLE_RATE_HZ);
  CHECK(!RecordingLimits_DurationReached(&limits, limits.target_frames - 1U));
  CHECK(RecordingLimits_DurationReached(&limits, limits.target_frames));
}

static void reserve_includes_session_rolling_and_finalize_contributions(void)
{
  RecordingLimits limits;
  CHECK(RecordingLimits_Init(&limits, 0U, 1000000000ULL, 60U, 123456U, 8192U));
  CHECK(limits.reserve_bytes == minute_bytes() + 123456U + 8192U);
}

static void start_requires_one_block_beyond_the_reserve_and_header(void)
{
  RecordingLimits limits;
  const uint64_t exact = RECORDING_LIMIT_WAV_HEADER_BYTES + minute_bytes() +
    RECORDING_LIMIT_BLOCK_BYTES;
  CHECK(RecordingLimits_Init(&limits, 0U, exact, 60U, 0U, 0U));
  CHECK(!RecordingLimits_Init(&limits, 0U, exact - 1U, 60U, 0U, 0U));
}

static void card_full_stops_before_a_block_would_enter_the_reserve(void)
{
  RecordingLimits limits;
  const uint64_t free_bytes = RECORDING_LIMIT_WAV_HEADER_BYTES + minute_bytes() +
    (2ULL * RECORDING_LIMIT_BLOCK_BYTES);
  CHECK(RecordingLimits_Init(&limits, 0U, free_bytes, 60U, 0U, 0U));
  CHECK(RecordingLimits_BeforeBlock(&limits, 0U) == RECORDING_LIMIT_NONE);
  CHECK(RecordingLimits_BeforeBlock(&limits, RECORDING_LIMIT_BLOCK_FRAMES) ==
        RECORDING_LIMIT_NONE);
  CHECK(RecordingLimits_BeforeBlock(&limits, 2U * RECORDING_LIMIT_BLOCK_FRAMES) ==
        RECORDING_LIMIT_CARD_FULL);
  CHECK(RecordingLimits_FreeBytes(&limits, 2U * RECORDING_LIMIT_BLOCK_FRAMES) ==
        minute_bytes());
}

static void wav_limit_stops_cleanly_before_overflow(void)
{
  RecordingLimits limits;
  CHECK(RecordingLimits_Init(&limits, 0U, UINT64_MAX, 0U, 0U, 0U));
  CHECK(RecordingLimits_BeforeBlock(&limits,
        RECORDING_LIMIT_WAV_MAX_FRAMES - RECORDING_LIMIT_BLOCK_FRAMES) ==
        RECORDING_LIMIT_NONE);
  CHECK(RecordingLimits_BeforeBlock(&limits, RECORDING_LIMIT_WAV_MAX_FRAMES) ==
        RECORDING_LIMIT_FILE_SIZE);
}

#define RUN(test) do { current_test = #test; test(); } while (0)

int main(void)
{
  RUN(open_ended_has_no_duration_target);
  RUN(timed_sessions_keep_the_requested_target);
  RUN(reserve_includes_session_rolling_and_finalize_contributions);
  RUN(start_requires_one_block_beyond_the_reserve_and_header);
  RUN(card_full_stops_before_a_block_would_enter_the_reserve);
  RUN(wav_limit_stops_cleanly_before_overflow);
  if (failures != 0)
  {
    printf("recording_limits_test: %d failure(s)\n", failures);
    return 1;
  }
  puts("recording_limits_test: all tests passed");
  return 0;
}
