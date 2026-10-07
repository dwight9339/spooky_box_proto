/* The Instrument transport and step pattern (Common/Src/transport.c and
 * step_pattern.c, full_spooky_proto-p04.14, decisions 0026 and 0027). */
#include "step_pattern.h"
#include "transport.h"

#include <stdio.h>

static unsigned failures;

#define CHECK(condition) do {                                               \
    if (!(condition)) {                                                     \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);       \
        ++failures;                                                         \
    }                                                                       \
} while (0)

/* Advances one frame at a time and returns the frame on which the step changes. */
static uint32_t frames_to_step_change(Transport *transport, uint8_t steps_per_beat)
{
    const uint32_t step = Transport_Step(transport, steps_per_beat);
    uint32_t frames = 0u;

    while (Transport_Step(transport, steps_per_beat) == step && frames < 1000000u) {
        Transport_Advance(transport, 1u);
        ++frames;
    }
    return frames;
}

static void test_stopped(void)
{
    Transport transport;

    Transport_Init(&transport);
    CHECK(!transport.running && transport.bpm_x100 == TRANSPORT_BPM_DEFAULT_X100);
    Transport_Advance(&transport, 48000u);
    CHECK(Transport_BeatsQ16(&transport) == 0u);
    CHECK(Transport_FramesToNextStep(&transport, 4u) == 0u);
}

/* A beat is 60 / BPM seconds: 24,000 frames at 120, 48,000 at 60, 14,400 at 200. */
static void test_timing_at_several_tempos(void)
{
    static const uint32_t tempos[] = {6000u, 12000u, 20000u, 9600u};
    static const uint32_t beat_frames[] = {48000u, 24000u, 14400u, 30000u};
    Transport transport;

    for (unsigned index = 0u; index < 4u; ++index) {
        Transport_Init(&transport);
        Transport_SetTempo(&transport, tempos[index]);
        Transport_Start(&transport);
        Transport_Advance(&transport, beat_frames[index] * 8u);
        CHECK(Transport_BeatsQ16(&transport) == (8u << 16));
        CHECK(Transport_Step(&transport, 4u) == 32u);
        CHECK(Transport_Step(&transport, 2u) == 16u);
        CHECK(Transport_Step(&transport, 1u) == 8u);
    }
    /* At 120 BPM a sixteenth is 6,000 frames, an eighth 12,000. */
    Transport_Init(&transport);
    Transport_Start(&transport);
    CHECK(frames_to_step_change(&transport, 4u) == 6000u);
    CHECK(frames_to_step_change(&transport, 4u) == 6000u);
    CHECK(frames_to_step_change(&transport, 2u) == 12000u); /* from 12,000 to 24,000 */
    CHECK(frames_to_step_change(&transport, 2u) == 12000u);
}

/* FramesToNextStep lands exactly on the frame the step changes, also when a step
 * is not a whole number of frames (96.5 BPM). */
static void test_frames_to_next_step(void)
{
    static const uint32_t tempos[] = {12000u, 9650u, 4000u, 20000u, 13333u};
    static const uint8_t divisions[] = {4u, 2u, 1u};
    Transport transport;
    Transport probe;

    for (unsigned t = 0u; t < 5u; ++t) {
        for (unsigned d = 0u; d < 3u; ++d) {
            Transport_Init(&transport);
            Transport_SetTempo(&transport, tempos[t]);
            Transport_Start(&transport);
            Transport_Advance(&transport, 777u);
            for (unsigned step = 0u; step < 20u; ++step) {
                const uint32_t before = Transport_Step(&transport, divisions[d]);
                const uint32_t frames = Transport_FramesToNextStep(&transport, divisions[d]);

                CHECK(frames >= 1u);
                probe = transport;
                Transport_Advance(&probe, frames - 1u);
                CHECK(Transport_Step(&probe, divisions[d]) == before);
                Transport_Advance(&transport, frames);
                CHECK(Transport_Step(&transport, divisions[d]) == before + 1u);
            }
        }
    }
}

/* A tempo change keeps the beat position; only the rate changes. */
static void test_tempo_change_keeps_position(void)
{
    Transport transport;
    uint64_t beats;

    Transport_Init(&transport);
    Transport_Start(&transport);
    Transport_Advance(&transport, 36000u); /* 1.5 beats at 120 */
    beats = Transport_BeatsQ16(&transport);
    CHECK(beats == (3u << 15));
    Transport_SetTempo(&transport, 6000u);
    CHECK(Transport_BeatsQ16(&transport) == beats);
    Transport_Advance(&transport, 24000u); /* half a beat at 60 */
    CHECK(Transport_BeatsQ16(&transport) == (2u << 16));
    Transport_SetTempo(&transport, 1u);
    CHECK(transport.bpm_x100 == TRANSPORT_BPM_MIN_X100);
    Transport_SetTempo(&transport, 99999u);
    CHECK(transport.bpm_x100 == TRANSPORT_BPM_MAX_X100);
    Transport_Stop(&transport);
    Transport_Start(&transport);
    CHECK(Transport_BeatsQ16(&transport) == 0u);
}

/* Decision 0026 item 3's table, 16 steps of a 24 kHz clip. */
static void test_fit(void)
{
    static const uint32_t samples[] = {120000u, 96000u, 72000u, 48000u, 24000u, 41143u};
    static const uint32_t tempo[] = {9600u, 12000u, 8000u, 12000u, 20000u, 14000u};
    static const uint8_t division[] = {2u, 2u, 4u, 4u, 4u, 4u};
    uint32_t bpm;
    uint8_t steps_per_beat;

    for (unsigned index = 0u; index < 6u; ++index) {
        CHECK(Transport_Fit(samples[index], 24000u, 16u, &bpm, &steps_per_beat));
        CHECK(bpm == tempo[index]);
        CHECK(steps_per_beat == division[index]);
    }
    /* A tie (3.43 s: 70 at 1/16, 140 at 1/8) goes to the finer division. */
    CHECK(Transport_Fit(82286u, 24000u, 16u, &bpm, &steps_per_beat));
    CHECK(steps_per_beat == 4u && bpm == 7000u);
    /* 1/4 for long clips: 10 s is 96 BPM at 1/4. */
    CHECK(Transport_Fit(240000u, 24000u, 16u, &bpm, &steps_per_beat));
    CHECK(steps_per_beat == 1u && bpm == 9600u);
    /* Beyond 13.7 s nothing fits: 1/16, clamped to the editable minimum. */
    CHECK(Transport_Fit(480000u, 24000u, 16u, &bpm, &steps_per_beat));
    CHECK(steps_per_beat == 4u && bpm == TRANSPORT_BPM_MIN_X100);
    CHECK(!Transport_Fit(0u, 24000u, 16u, &bpm, &steps_per_beat));
    CHECK(!Transport_Fit(72000u, 24000u, 0u, &bpm, &steps_per_beat));
    CHECK(!Transport_Fit(72000u, 24000u, 16u, NULL, &steps_per_beat));
}

/* Step advance and wrap; the playhead follows the pattern's own division. */
static void test_playhead(void)
{
    StepPattern pattern;
    Transport transport;

    StepPattern_InitSweep(&pattern, 1000u);
    CHECK(pattern.length == 16u && pattern.steps_per_beat == 4u);
    CHECK(pattern.steps[0].value == 0u && pattern.steps[1].value == 62u);
    CHECK(pattern.steps[8].value == 500u && pattern.steps[15].value == 937u);
    for (unsigned step = 0u; step < 16u; ++step) {
        CHECK(pattern.steps[step].on);
    }
    Transport_Init(&transport);
    Transport_Start(&transport);
    CHECK(StepPattern_Playhead(&pattern, &transport) == 0u);
    Transport_Advance(&transport, 6000u * 15u);
    CHECK(StepPattern_Playhead(&pattern, &transport) == 15u);
    Transport_Advance(&transport, 6000u);
    CHECK(StepPattern_Playhead(&pattern, &transport) == 0u); /* wrap */
    pattern.steps_per_beat = 2u;
    CHECK(StepPattern_Playhead(&pattern, &transport) == 8u); /* 4 beats in, at 1/8 */
    pattern.length = 6u;
    CHECK(StepPattern_Playhead(&pattern, &transport) == 2u);
}

/* The knob offsets the step value around its centre and is held in range. */
static void test_offset(void)
{
    CHECK(StepPattern_Offset(300u, 500u, 500u, 1000u) == 300u);
    CHECK(StepPattern_Offset(300u, 600u, 500u, 1000u) == 400u);
    CHECK(StepPattern_Offset(300u, 100u, 500u, 1000u) == 0u);
    CHECK(StepPattern_Offset(900u, 800u, 500u, 1000u) == 1000u);
    CHECK(StepPattern_Offset(0u, 0u, 500u, 1000u) == 0u);
    CHECK(StepPattern_Offset(1000u, 1000u, 500u, 1000u) == 1000u);
}

static void test_divisions(void)
{
    CHECK(StepPattern_NextDivision(4u, 1) == 2u);
    CHECK(StepPattern_NextDivision(2u, 1) == 1u);
    CHECK(StepPattern_NextDivision(1u, 1) == 1u);
    CHECK(StepPattern_NextDivision(1u, -5) == 4u);
    CHECK(StepPattern_NextDivision(4u, -1) == 4u);
}

int main(void)
{
    test_stopped();
    test_timing_at_several_tempos();
    test_frames_to_next_step();
    test_tempo_change_keeps_position();
    test_fit();
    test_playhead();
    test_offset();
    test_divisions();
    if (failures != 0u) {
        printf("%u failure(s)\n", failures);
        return 1;
    }
    printf("transport_test passed\n");
    return 0;
}
