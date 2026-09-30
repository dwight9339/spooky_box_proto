/*
 * Host tests for the common audio sample timeline (decision 0012, hpq.1):
 * 32-bit counter extension across wrap, DMA snapshot resolution with a pending
 * completion, stream restart epochs, backwards counts, start alignment of the
 * radio and microphone streams, and event-stamp conversion. Host results only;
 * not hardware evidence of DMA timing or start latency.
 */

#include <stdio.h>
#include <string.h>

#include "audio_timeline.h"

static int failures;
static const char *current_test;

#define CHECK(cond)                                                                   \
    do {                                                                              \
        if (!(cond)) {                                                                \
            printf("FAIL %s (line %d): %s\n", current_test, __LINE__, #cond);        \
            ++failures;                                                               \
        }                                                                             \
    } while (0)

/* The current radio path: 1024 stereo frames per buffer, 16-bit samples. */
#define FPH 512u
#define IPF 2u
#define BUFFER_ITEMS (2u * FPH * IPF)

static AudioTimelineStream started(uint32_t raw_base)
{
    AudioTimelineStream stream;
    memset(&stream, 0, sizeof(stream));
    CHECK(AudioTimeline_StreamStart(&stream, FPH, IPF, raw_base) == AUDIO_TIMELINE_OK);
    return stream;
}

/* remaining_items for a DMA index within the circular buffer. */
static uint32_t ndtr_at(uint32_t index)
{
    return BUFFER_ITEMS - index;
}

static void test_extend32(void)
{
    current_test = "extend32";
    CHECK(AudioTimeline_Extend32(0u, 5u) == 5u);
    CHECK(AudioTimeline_Extend32(100u, 90u) == 90u);
    CHECK(AudioTimeline_Extend32(0xFFFFFFF0u, 0x10u) == 0x100000010ull);
    CHECK(AudioTimeline_Extend32(0x100000010ull, 0xFFFFFFF0u) == 0xFFFFFFF0ull);
    CHECK(AudioTimeline_Extend32(0x300000000ull, 0x7FFFFFFFu) == 0x37FFFFFFFull);
    CHECK(AudioTimeline_Extend32(3u, 1u) == 1u);
    CHECK(AudioTimeline_Extend32(3u, 0xFFFFFFFFu) == 0u); /* -1 saturates */
    /* A value more than the reference below zero saturates instead of wrapping. */
    CHECK(AudioTimeline_Extend32(3u, 0xFFFFFF00u) == 0u);
}

static void test_start_arguments_and_epochs(void)
{
    AudioTimelineStream stream;

    current_test = "start_arguments_and_epochs";
    memset(&stream, 0, sizeof(stream));
    CHECK(AudioTimeline_StreamStart(NULL, FPH, IPF, 0u) == AUDIO_TIMELINE_ERR_ARGUMENT);
    CHECK(AudioTimeline_StreamStart(&stream, 0u, IPF, 0u) == AUDIO_TIMELINE_ERR_ARGUMENT);
    CHECK(AudioTimeline_StreamStart(&stream, FPH, 0u, 0u) == AUDIO_TIMELINE_ERR_ARGUMENT);
    CHECK(AudioTimeline_StreamStart(&stream, 0x80000000u, 2u, 0u) == AUDIO_TIMELINE_ERR_ARGUMENT);
    CHECK(stream.epoch == 0u);

    CHECK(AudioTimeline_StreamStart(&stream, FPH, IPF, 0u) == AUDIO_TIMELINE_OK);
    CHECK(stream.epoch == 1u);
    CHECK(AudioTimeline_StreamStart(&stream, FPH, IPF, 0u) == AUDIO_TIMELINE_OK);
    CHECK(stream.epoch == 2u);

    stream.epoch = UINT32_MAX;
    CHECK(AudioTimeline_StreamStart(&stream, FPH, IPF, 0u) == AUDIO_TIMELINE_OK);
    CHECK(stream.epoch == 1u); /* epoch 0 is never issued */
}

static void test_observe_not_started_and_bounds(void)
{
    AudioTimelineStream stream;
    AudioTimelinePosition position;

    current_test = "observe_not_started_and_bounds";
    memset(&stream, 0, sizeof(stream));
    CHECK(AudioTimeline_StreamObserve(&stream, 0u, BUFFER_ITEMS, &position) ==
          AUDIO_TIMELINE_ERR_NOT_STARTED);
    stream = started(0u);
    CHECK(AudioTimeline_StreamObserve(&stream, 0u, BUFFER_ITEMS + 1u, &position) ==
          AUDIO_TIMELINE_ERR_ARGUMENT);
    CHECK(AudioTimeline_StreamObserve(&stream, 0u, BUFFER_ITEMS, NULL) ==
          AUDIO_TIMELINE_ERR_ARGUMENT);
}

static void test_observe_positions(void)
{
    AudioTimelineStream stream = started(0u);
    AudioTimelinePosition position;

    current_test = "observe_positions";
    CHECK(AudioTimeline_StreamObserve(&stream, 0u, BUFFER_ITEMS, &position) ==
          AUDIO_TIMELINE_OK);
    CHECK(position.epoch == stream.epoch);
    CHECK(position.frame == 0u);

    CHECK(AudioTimeline_StreamObserve(&stream, 0u, ndtr_at(200u), &position) ==
          AUDIO_TIMELINE_OK);
    CHECK(position.frame == 100u);

    /* Mid-frame: the left sample is written but the right is not. */
    CHECK(AudioTimeline_StreamObserve(&stream, 0u, ndtr_at(201u), &position) ==
          AUDIO_TIMELINE_OK);
    CHECK(position.frame == 100u);

    CHECK(AudioTimeline_StreamObserve(&stream, 1u, ndtr_at(1034u), &position) ==
          AUDIO_TIMELINE_OK);
    CHECK(position.frame == 517u);
}

/* With the completion interrupt masked, the DMA can already be in the next half
 * while the count has not been incremented. The same instant must resolve to the
 * same position whether or not the completion has been counted. */
static void test_pending_completion(void)
{
    AudioTimelineStream pending = started(0u);
    AudioTimelineStream counted = started(0u);
    AudioTimelinePosition a;
    AudioTimelinePosition b;

    current_test = "pending_completion";
    CHECK(AudioTimeline_StreamObserve(&pending, 0u, ndtr_at(1034u), &a) == AUDIO_TIMELINE_OK);
    CHECK(AudioTimeline_StreamObserve(&counted, 1u, ndtr_at(1034u), &b) == AUDIO_TIMELINE_OK);
    CHECK(a.frame == 517u);
    CHECK(a.frame == b.frame);
    /* The pending completion is not stored, so the later count is not double. */
    CHECK(pending.completed_halves == 0u);

    /* Transfer-complete: NDTR reads zero (or has reloaded) at index 0. */
    CHECK(AudioTimeline_StreamObserve(&pending, 1u, 0u, &a) == AUDIO_TIMELINE_OK);
    CHECK(AudioTimeline_StreamObserve(&counted, 2u, BUFFER_ITEMS, &b) == AUDIO_TIMELINE_OK);
    CHECK(a.frame == 2u * FPH);
    CHECK(a.frame == b.frame);
}

/* Every snapshot of a monotonic DMA walk resolves to a strictly monotonic
 * position, whether or not each completion has been counted yet. */
static void test_walk_is_monotonic(void)
{
    AudioTimelineStream stream = started(0u);
    AudioTimelinePosition position;
    uint64_t previous = 0u;
    int first = 1;

    current_test = "walk_is_monotonic";
    for (uint64_t item = 0u; item < 10u * BUFFER_ITEMS; item += 6u) {
        uint32_t index = (uint32_t)(item % BUFFER_ITEMS);
        uint32_t halves = (uint32_t)(item / (FPH * IPF));
        /* Delay counting on odd halves to model the masked interrupt. */
        uint32_t raw = halves;
        if ((halves % 2u == 1u) && (index % (FPH * IPF) < 64u)) {
            raw = halves - 1u;
        }
        CHECK(AudioTimeline_StreamObserve(&stream, raw, ndtr_at(index), &position) ==
              AUDIO_TIMELINE_OK);
        CHECK(position.frame == item / IPF);
        if (!first) {
            CHECK(position.frame > previous);
        }
        previous = position.frame;
        first = 0;
    }
}

static void test_wrap(void)
{
    AudioTimelineStream stream = started(0xFFFFFFF0u);
    AudioTimelinePosition position;

    current_test = "wrap";
    /* The caller's free-running count wraps shortly after the stream starts. */
    CHECK(AudioTimeline_StreamObserve(&stream, 0x00000010u, BUFFER_ITEMS, &position) ==
          AUDIO_TIMELINE_OK);
    CHECK(stream.completed_halves == 0x20u);
    CHECK(position.frame == 0x20u * FPH);

    /* Relative counts beyond 2^32 halves keep extending, in steps below 2^31. */
    stream = started(0u);
    uint32_t raw = 0u;
    for (int step = 0; step < 3; ++step) {
        raw += 0x70000000u;
        CHECK(AudioTimeline_StreamObserve(&stream, raw, BUFFER_ITEMS, &position) ==
              AUDIO_TIMELINE_OK);
    }
    CHECK(stream.completed_halves == 0x150000000ull);
    CHECK(position.frame == 0x150000000ull * FPH);
}

static void test_backwards_count_is_rejected(void)
{
    AudioTimelineStream stream = started(0u);
    AudioTimelinePosition position;

    current_test = "backwards_count_is_rejected";
    CHECK(AudioTimeline_StreamObserve(&stream, 6u, BUFFER_ITEMS, &position) ==
          AUDIO_TIMELINE_OK);
    /* The stream restarted underneath without a new epoch: a discontinuity. */
    CHECK(AudioTimeline_StreamObserve(&stream, 2u, BUFFER_ITEMS, &position) ==
          AUDIO_TIMELINE_ERR_BACKWARDS);
    CHECK(stream.completed_halves == 6u);
    CHECK(AudioTimeline_StreamObserve(&stream, 6u, BUFFER_ITEMS, &position) ==
          AUDIO_TIMELINE_OK);
    CHECK(position.frame == 6u * FPH);
}

static void test_restart_epoch(void)
{
    AudioTimelineStream stream = started(0u);
    AudioTimelinePosition before;
    AudioTimelinePosition after;
    AudioTimelineAlignment alignment;
    uint64_t offset;

    current_test = "restart_epoch";
    CHECK(AudioTimeline_StreamObserve(&stream, 4u, BUFFER_ITEMS, &before) ==
          AUDIO_TIMELINE_OK);
    CHECK(AudioTimeline_StreamStart(&stream, FPH, IPF, 4u) == AUDIO_TIMELINE_OK);
    /* Five halves since the restart: the DMA is at the start of the second half. */
    CHECK(AudioTimeline_StreamObserve(&stream, 9u, ndtr_at(FPH * IPF), &after) ==
          AUDIO_TIMELINE_OK);
    CHECK(after.epoch == before.epoch + 1u);
    CHECK(after.frame == 5u * FPH); /* counted from the new start */

    CHECK(AudioTimeline_Offset(before, after, &offset) == AUDIO_TIMELINE_ERR_STALE_EPOCH);
    CHECK(AudioTimeline_AlignStart(&stream, before, 0u, &alignment) ==
          AUDIO_TIMELINE_ERR_STALE_EPOCH);
}

static void test_align_start(void)
{
    AudioTimelineStream stream = started(0u);
    AudioTimelinePosition mic_start = { stream.epoch, (3u * FPH) + 100u };
    AudioTimelineAlignment alignment;

    current_test = "align_start";
    CHECK(AudioTimeline_AlignStart(&stream, mic_start, 40u, &alignment) ==
          AUDIO_TIMELINE_OK);
    CHECK(alignment.first_half_frame == 3u * FPH);
    CHECK(alignment.trim_frames == 140u);
    CHECK(alignment.origin.epoch == stream.epoch);
    CHECK(alignment.origin.frame == (3u * FPH) + 140u);

    /* Latency that reaches past the end of the half in progress. */
    mic_start.frame = (3u * FPH) + 500u;
    CHECK(AudioTimeline_AlignStart(&stream, mic_start, 40u, &alignment) ==
          AUDIO_TIMELINE_OK);
    CHECK(alignment.trim_frames == 540u);
    CHECK(alignment.first_half_frame + alignment.trim_frames == alignment.origin.frame);

    CHECK(AudioTimeline_AlignStart(&stream, mic_start, 40u, NULL) ==
          AUDIO_TIMELINE_ERR_ARGUMENT);
    mic_start.frame = 0u;
    CHECK(AudioTimeline_AlignStart(&stream, mic_start, UINT32_MAX, &alignment) ==
          AUDIO_TIMELINE_OK);
    mic_start.frame = 1u;
    CHECK(AudioTimeline_AlignStart(&stream, mic_start, UINT32_MAX, &alignment) ==
          AUDIO_TIMELINE_ERR_ARGUMENT);
}

/* End-to-end model of the start: a common signal indexed by absolute stream
 * frame reaches the radio (delivered in whole halves from the half in progress)
 * and the microphone (first sample mic_latency after its DMA start). After the
 * recorder drops trim_frames radio frames, both tracks carry the same frame. */
static void test_alignment_matches_both_tracks(void)
{
    AudioTimelineStream stream = started(0u);

    current_test = "alignment_matches_both_tracks";
    for (uint32_t into = 0u; into < FPH; into += 37u) {
        for (uint32_t latency = 0u; latency < 3u * FPH; latency += 101u) {
            AudioTimelinePosition mic_start = { stream.epoch, (7u * FPH) + into };
            AudioTimelineAlignment alignment;
            uint64_t radio_frame0;
            uint64_t mic_frame0;

            CHECK(AudioTimeline_AlignStart(&stream, mic_start, latency, &alignment) ==
                  AUDIO_TIMELINE_OK);
            /* The recorder receives halves starting at first_half_frame. */
            radio_frame0 = alignment.first_half_frame + alignment.trim_frames;
            mic_frame0 = mic_start.frame + latency;
            CHECK(radio_frame0 == mic_frame0);
            CHECK(radio_frame0 == alignment.origin.frame);
            CHECK(alignment.first_half_frame % FPH == 0u);
            CHECK(alignment.first_half_frame <= mic_start.frame);
            CHECK(mic_start.frame - alignment.first_half_frame < FPH);
        }
    }
}

static void test_offset(void)
{
    AudioTimelinePosition origin = { 3u, 1000u };
    AudioTimelinePosition position = { 3u, 1500u };
    AudioTimelinePosition invalid = { 0u, 1500u };
    uint64_t offset = 0u;

    current_test = "offset";
    CHECK(AudioTimeline_Offset(origin, position, &offset) == AUDIO_TIMELINE_OK);
    CHECK(offset == 500u);
    position.frame = 999u;
    CHECK(AudioTimeline_Offset(origin, position, &offset) ==
          AUDIO_TIMELINE_ERR_BEFORE_ORIGIN);
    CHECK(AudioTimeline_Offset(origin, invalid, &offset) == AUDIO_TIMELINE_ERR_NOT_STARTED);
    CHECK(AudioTimeline_Offset(invalid, origin, &offset) == AUDIO_TIMELINE_ERR_NOT_STARTED);
    CHECK(AudioTimeline_Offset(origin, origin, NULL) == AUDIO_TIMELINE_ERR_ARGUMENT);
}

static void test_stamp_offset(void)
{
    AudioTimelinePosition origin = { 2u, 48000u };
    AudioTimelineStamp stamp = { { 2u, 48000u + 4800u }, 240u };
    uint64_t latest = 0u;
    uint32_t uncertainty = 0u;

    current_test = "stamp_offset";
    CHECK(AudioTimeline_StampOffset(origin, stamp, &latest, &uncertainty) ==
          AUDIO_TIMELINE_OK);
    CHECK(latest == 4800u);
    CHECK(uncertainty == 240u);

    /* Observed 100 frames after the origin with 240 frames of latency: the event
     * is within the first 100 frames of this timeline, not before it. */
    stamp.position.frame = 48000u + 100u;
    CHECK(AudioTimeline_StampOffset(origin, stamp, &latest, &uncertainty) ==
          AUDIO_TIMELINE_OK);
    CHECK(latest == 100u);
    CHECK(uncertainty == 100u);

    stamp.position.frame = 47999u;
    CHECK(AudioTimeline_StampOffset(origin, stamp, &latest, &uncertainty) ==
          AUDIO_TIMELINE_ERR_BEFORE_ORIGIN);
    stamp.position.frame = 48100u;
    stamp.position.epoch = 1u;
    CHECK(AudioTimeline_StampOffset(origin, stamp, &latest, &uncertainty) ==
          AUDIO_TIMELINE_ERR_STALE_EPOCH);
    CHECK(AudioTimeline_StampOffset(origin, stamp, NULL, &uncertainty) ==
          AUDIO_TIMELINE_ERR_ARGUMENT);
}

int main(void)
{
    test_extend32();
    test_start_arguments_and_epochs();
    test_observe_not_started_and_bounds();
    test_observe_positions();
    test_pending_completion();
    test_walk_is_monotonic();
    test_wrap();
    test_backwards_count_is_rejected();
    test_restart_epoch();
    test_align_start();
    test_alignment_matches_both_tracks();
    test_offset();
    test_stamp_offset();

    if (failures != 0) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("audio_timeline_test: all tests passed\n");
    return 0;
}
