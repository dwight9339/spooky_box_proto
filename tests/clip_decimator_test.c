/* Demo-only Instrument clip range and decimation (clip_decimator.c,
 * full_spooky_proto-p04.6). */
#include "clip_decimator.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static unsigned failures;

#define CHECK(condition) do {                                               \
    if (!(condition)) {                                                     \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);       \
        ++failures;                                                         \
    }                                                                       \
} while (0)

#define FULL_SEGMENT 262144u /* 64 recorder blocks of 4,096 frames */
#define PI 3.14159265358979323846

static int16_t frames[CLIP_SOURCE_FRAMES * CLIP_SOURCE_CHANNELS];
static int16_t out[CLIP_MAX_SAMPLES + 8u];
static int16_t reference[CLIP_MAX_SAMPLES + 8u];

static void check_part(const ClipRange *range, unsigned index, unsigned segment,
                       uint32_t first, uint32_t count)
{
    CHECK(range->parts[index].segment == segment);
    CHECK(range->parts[index].first_frame == first);
    CHECK(range->parts[index].frames == count);
}

static void test_range_one_segment(void)
{
    const uint32_t held[] = {FULL_SEGMENT};
    ClipRange range;

    _Static_assert(CLIP_SOURCE_FRAMES == 144000u, "3 s of 48 kHz source frames");
    CHECK(ClipRange_Select(held, 1u, CLIP_SOURCE_FRAMES, &range));
    CHECK(range.count == 1u);
    CHECK(range.frames == CLIP_SOURCE_FRAMES);
    check_part(&range, 0u, 0u, FULL_SEGMENT - CLIP_SOURCE_FRAMES, CLIP_SOURCE_FRAMES);
}

/* The newest segment, partial at the save point, and the end of the one before. */
static void test_range_spans_two_segments(void)
{
    const uint32_t held[] = {FULL_SEGMENT, FULL_SEGMENT, FULL_SEGMENT, 4096u};
    ClipRange range;

    CHECK(ClipRange_Select(held, 4u, CLIP_SOURCE_FRAMES, &range));
    CHECK(range.count == 2u);
    CHECK(range.frames == CLIP_SOURCE_FRAMES);
    check_part(&range, 0u, 2u, FULL_SEGMENT - (CLIP_SOURCE_FRAMES - 4096u),
               CLIP_SOURCE_FRAMES - 4096u);
    check_part(&range, 1u, 3u, 0u, 4096u);
}

static void test_range_short_capture(void)
{
    const uint32_t even[] = {5000u};
    const uint32_t odd[] = {5001u};
    const uint32_t two_odd[] = {3u, 4u};
    const uint32_t lone_oldest[] = {1u, 4u};
    ClipRange range;

    CHECK(ClipRange_Select(even, 1u, CLIP_SOURCE_FRAMES, &range));
    CHECK(range.count == 1u);
    check_part(&range, 0u, 0u, 0u, 5000u);
    /* An odd count drops the oldest frame, never the newest. */
    CHECK(ClipRange_Select(odd, 1u, CLIP_SOURCE_FRAMES, &range));
    CHECK(range.frames == 5000u);
    check_part(&range, 0u, 0u, 1u, 5000u);
    CHECK(ClipRange_Select(two_odd, 2u, CLIP_SOURCE_FRAMES, &range));
    CHECK(range.count == 2u);
    CHECK(range.frames == 6u);
    check_part(&range, 0u, 0u, 1u, 2u);
    check_part(&range, 1u, 1u, 0u, 4u);
    /* Dropping it empties the oldest part, which goes. */
    CHECK(ClipRange_Select(lone_oldest, 2u, CLIP_SOURCE_FRAMES, &range));
    CHECK(range.count == 1u);
    CHECK(range.frames == 4u);
    check_part(&range, 0u, 1u, 0u, 4u);
    CHECK(range.parts[1].frames == 0u);
}

static void test_range_limits(void)
{
    uint32_t tiny[40];
    const uint32_t trailing_empty[] = {FULL_SEGMENT, 0u};
    const uint32_t single_frame[] = {1u};
    const uint32_t empty[] = {0u, 0u};
    ClipRange range;

    /* More segments than parts: the clip is shorter but still ends at the save. */
    for (unsigned index = 0u; index < 40u; ++index) {
        tiny[index] = 4096u;
    }
    CHECK(ClipRange_Select(tiny, 40u, CLIP_SOURCE_FRAMES, &range));
    CHECK(range.count == CLIP_RANGE_MAX_PARTS);
    CHECK(range.frames == 8192u);
    check_part(&range, 0u, 38u, 0u, 4096u);
    check_part(&range, 1u, 39u, 0u, 4096u);
    /* Empty segments are skipped. */
    CHECK(ClipRange_Select(trailing_empty, 2u, CLIP_SOURCE_FRAMES, &range));
    CHECK(range.count == 1u);
    check_part(&range, 0u, 0u, FULL_SEGMENT - CLIP_SOURCE_FRAMES, CLIP_SOURCE_FRAMES);
    /* Nothing to load. */
    CHECK(!ClipRange_Select(single_frame, 1u, CLIP_SOURCE_FRAMES, &range));
    CHECK(range.frames == 0u && range.count == 0u);
    CHECK(!ClipRange_Select(empty, 2u, CLIP_SOURCE_FRAMES, &range));
    CHECK(!ClipRange_Select(empty, 0u, CLIP_SOURCE_FRAMES, &range));
    CHECK(!ClipRange_Select(NULL, 1u, CLIP_SOURCE_FRAMES, &range));
    CHECK(!ClipRange_Select(single_frame, 1u, CLIP_SOURCE_FRAMES, NULL));
}

static void fill(uint32_t count, int16_t left, int16_t right, int16_t mic)
{
    for (uint32_t frame = 0u; frame < count; ++frame) {
        frames[frame * 3u] = left;
        frames[frame * 3u + 1u] = right;
        frames[frame * 3u + 2u] = mic;
    }
}

static uint32_t decimate(uint32_t count)
{
    ClipDecimator decimator;

    ClipDecimator_Init(&decimator);
    return ClipDecimator_Process(&decimator, frames, count, out, CLIP_MAX_SAMPLES);
}

/* Steady input comes out unchanged from the first sample: the filter starts
 * from the first frame, not from silence. The microphone never leaks in. */
static void test_dc_and_mono_sum(void)
{
    fill(400u, 1000, 1000, 30000);
    CHECK(decimate(400u) == 200u);
    for (unsigned sample = 0u; sample < 200u; ++sample) {
        CHECK(out[sample] == 1000);
    }
    fill(400u, 2000, 0, -30000);
    decimate(400u);
    CHECK(out[0] == 1000 && out[199] == 1000);
    fill(400u, 1000, -1000, 0);
    decimate(400u);
    CHECK(out[0] == 0 && out[199] == 0);
    fill(400u, 32767, 32767, 0);
    decimate(400u);
    CHECK(out[0] == 32767 && out[199] == 32767);
    fill(400u, -32768, -32768, 0);
    decimate(400u);
    CHECK(out[0] == -32768 && out[199] == -32768);
}

/* A full-scale step overshoots the filter: the output saturates, never wraps. */
static void test_step_saturates(void)
{
    fill(200u, -32768, -32768, 0);
    fill(200u, -32768, -32768, 0);
    for (uint32_t frame = 100u; frame < 200u; ++frame) {
        frames[frame * 3u] = 32767;
        frames[frame * 3u + 1u] = 32767;
    }
    CHECK(decimate(200u) == 100u);
    for (unsigned sample = 70u; sample < 100u; ++sample) {
        CHECK(out[sample] > 30000);
    }
    for (unsigned sample = 0u; sample < 30u; ++sample) {
        CHECK(out[sample] < -30000);
    }
}

/* An impulse on an even frame lands on one output only (a halfband's zero taps)
 * after the 19-frame delay; each polyphase branch sums to one half. */
static void test_impulse_and_delay(void)
{
    int32_t sum = 0;

    fill(400u, 0, 0, 0);
    frames[100u * 3u] = 16384;
    frames[100u * 3u + 1u] = 16384;
    CHECK(decimate(400u) == 200u);
    for (unsigned sample = 0u; sample < 200u; ++sample) {
        CHECK(out[sample] == ((sample == 59u) ? 8192 : 0));
    }
    fill(400u, 0, 0, 0);
    frames[101u * 3u] = 16384;
    frames[101u * 3u + 1u] = 16384;
    decimate(400u);
    CHECK(out[59] == 5165 && out[60] == 5165);
    CHECK(out[58] == -1592 && out[61] == -1592);
    for (unsigned sample = 0u; sample < 200u; ++sample) {
        sum += out[sample];
    }
    CHECK(sum >= 8182 && sum <= 8202);
}

static double tone_rms(double hz, int16_t amplitude, uint32_t count)
{
    double power = 0.0;
    uint32_t samples;
    const uint32_t skip = 64u; /* past the start */

    for (uint32_t frame = 0u; frame < count; ++frame) {
        const int16_t value = (int16_t)lrint(amplitude * sin(2.0 * PI * hz * frame /
                                                             CLIP_SOURCE_RATE_HZ));
        frames[frame * 3u] = value;
        frames[frame * 3u + 1u] = value;
        frames[frame * 3u + 2u] = 0;
    }
    samples = decimate(count);
    for (uint32_t sample = skip; sample < samples; ++sample) {
        power += (double)out[sample] * out[sample];
    }
    return sqrt(power / (double)(samples - skip));
}

/* Passband kept, the band that would alias removed. */
static void test_frequency_response(void)
{
    const double full = 20000.0 / sqrt(2.0);

    CHECK(fabs(tone_rms(1000.0, 20000, 48000u) / full - 1.0) < 0.005);
    CHECK(fabs(tone_rms(8000.0, 20000, 48000u) / full - 1.0) < 0.005);
    /* At least 60 dB down from 15 kHz up (the design gives 73.7 dB at 15 kHz). */
    CHECK(tone_rms(15000.0, 20000, 48000u) / full < 0.001);
    CHECK(tone_rms(18000.0, 20000, 48000u) / full < 0.001);
    CHECK(tone_rms(23000.0, 20000, 48000u) / full < 0.001);
}

/* The load feeds the decimator in chunks; their sizes must not matter. */
static void test_chunks_match_one_pass(void)
{
    static const uint32_t sizes[] = {1u, 7u, 2048u, 2u, 333u, 2047u};
    ClipDecimator decimator;
    uint32_t seed = 12345u;
    uint32_t done = 0u;
    uint32_t written = 0u;
    uint32_t reference_count;
    unsigned next = 0u;

    for (uint32_t index = 0u; index < CLIP_SOURCE_FRAMES * 3u; ++index) {
        seed = seed * 1103515245u + 12345u;
        frames[index] = (int16_t)(seed >> 16);
    }
    reference_count = decimate(CLIP_SOURCE_FRAMES);
    CHECK(reference_count == CLIP_MAX_SAMPLES);
    memcpy(reference, out, sizeof(reference));
    memset(out, 0x55, sizeof(out));
    ClipDecimator_Init(&decimator);
    while (done < CLIP_SOURCE_FRAMES) {
        uint32_t size = sizes[next++ % (sizeof(sizes) / sizeof(sizes[0]))];

        if (size > CLIP_SOURCE_FRAMES - done) {
            size = CLIP_SOURCE_FRAMES - done;
        }
        written += ClipDecimator_Process(&decimator, &frames[done * 3u], size, &out[written],
                                         CLIP_MAX_SAMPLES - written);
        done += size;
    }
    CHECK(written == CLIP_MAX_SAMPLES);
    CHECK(memcmp(out, reference, CLIP_MAX_BYTES) == 0);
}

static void test_capacity_and_arguments(void)
{
    ClipDecimator decimator;

    fill(10u, 100, 100, 0);
    memset(out, 0x55, sizeof(out));
    ClipDecimator_Init(&decimator);
    CHECK(ClipDecimator_Process(&decimator, frames, 10u, out, 3u) == 3u);
    CHECK(out[2] == 100);
    CHECK(out[3] == 0x5555);
    CHECK(ClipDecimator_Process(&decimator, frames, 10u, out, 0u) == 0u);
    CHECK(ClipDecimator_Process(NULL, frames, 10u, out, 5u) == 0u);
    CHECK(ClipDecimator_Process(&decimator, NULL, 10u, out, 5u) == 0u);
    CHECK(ClipDecimator_Process(&decimator, frames, 10u, NULL, 5u) == 0u);
    /* An odd frame waits for its pair. */
    ClipDecimator_Init(&decimator);
    CHECK(ClipDecimator_Process(&decimator, frames, 1u, out, 5u) == 0u);
    CHECK(ClipDecimator_Process(&decimator, frames, 1u, out, 5u) == 1u);
}

int main(void)
{
    test_range_one_segment();
    test_range_spans_two_segments();
    test_range_short_capture();
    test_range_limits();
    test_dc_and_mono_sum();
    test_step_saturates();
    test_impulse_and_delay();
    test_frequency_response();
    test_chunks_match_one_pass();
    test_capacity_and_arguments();
    if (failures != 0u) {
        printf("%u failure(s)\n", failures);
        return 1;
    }
    printf("clip_decimator_test passed\n");
    return 0;
}
