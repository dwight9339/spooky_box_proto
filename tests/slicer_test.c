/* The Slicer render core (Common/Src/slicer.c, full_spooky_proto-p04.15,
 * decision 0022 items 3, 13, 15 and 17). */
#include "slicer.h"

#include <stdio.h>
#include <stdlib.h>

static unsigned failures;

#define CHECK(condition) do {                                               \
    if (!(condition)) {                                                     \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);       \
        ++failures;                                                         \
    }                                                                       \
} while (0)

#define CLIP_SAMPLES 72000u /* 3 s at 24 kHz */
#define SLICE_SAMPLES (CLIP_SAMPLES / 16u)

static int16_t clip[CLIP_SAMPLES];
static uint16_t out[2u * 48000u];
static SlicerEngine engine;

/* Slice n holds the constant (n + 1) * 1000, so the output names the slice. */
static void fill_steps(void)
{
    for (uint32_t index = 0u; index < CLIP_SAMPLES; ++index) {
        clip[index] = (int16_t)(((index / SLICE_SAMPLES) + 1u) * 1000u);
    }
}

static int32_t left(uint32_t frame)
{
    return (int16_t)out[2u * frame];
}

static void start(void)
{
    SlicerSetup setup;

    Slicer_Init(&engine);
    Slicer_DefaultSetup(&setup, CLIP_SAMPLES, 16u);
    for (unsigned slice = 0u; slice < 16u; ++slice) {
        setup.slices[slice].level_percent = 100u;
    }
    Slicer_SetSetup(&engine, &setup);
    CHECK(Slicer_Start(&engine, clip, CLIP_SAMPLES));
}

/* The level of slice n at full level, after its fade-in: (n + 1) * 1000 at
 * Q15 unity, within the fixed-point rounding. */
static int near(int32_t value, int32_t expected)
{
    return abs(value - expected) <= ((expected / 200) + 2);
}

static void test_stopped_and_silent(void)
{
    Slicer_Init(&engine);
    CHECK(!Slicer_Render(&engine, out, 512u));
    CHECK(!Slicer_Trigger(&engine, 0u, 0u));
    CHECK(!Slicer_Start(&engine, NULL, 10u));
    start();
    /* Started but untriggered: silence, and a render. */
    out[0] = 1u;
    CHECK(Slicer_Render(&engine, out, 512u));
    CHECK(left(0u) == 0 && left(511u) == 0);
    CHECK(!Slicer_Trigger(&engine, 16u, 0u)); /* outside the map */
    Slicer_Stop(&engine);
    CHECK(!Slicer_Render(&engine, out, 512u));
}

/* A slice plays its own audio until its end, then silence (0022 item 3). */
static void test_slice_plays_to_its_end(void)
{
    const uint32_t frames = 2u * SLICE_SAMPLES; /* one slice at unity: 9,000 frames */
    SlicerStatus status;

    start();
    CHECK(Slicer_SliceFrames(&engine.setup, 5u) == frames);
    CHECK(Slicer_Trigger(&engine, 5u, 0u));
    CHECK(Slicer_Render(&engine, out, frames + 500u));
    CHECK(left(0u) == 0);                       /* fades in */
    CHECK(near(left(200u), 6000));              /* slice 5 */
    CHECK(near(left(frames - SLICER_FADE_FRAMES - 1u), 6000));
    CHECK(left(frames - 10u) < 6000 && left(frames - 10u) > 0); /* fading out */
    CHECK(left(frames) == 0 && left(frames + 499u) == 0);
    Slicer_GetStatus(&engine, &status);
    CHECK(status.sounding == SLICER_NO_SLICE && status.triggers == 1u);
}

/* A new trigger cuts the playing slice with a short crossfade: two voices for
 * SLICER_FADE_FRAMES, the output moving smoothly from one to the other. */
static void test_crossfade_cut(void)
{
    SlicerStatus status;
    int32_t largest_jump = 0;

    start();
    CHECK(Slicer_Trigger(&engine, 0u, 0u));
    CHECK(Slicer_Render(&engine, out, 1000u));
    CHECK(near(left(999u), 1000));
    CHECK(Slicer_Trigger(&engine, 9u, 0u));
    CHECK(engine.voices[0].active && engine.voices[1].active);
    CHECK(Slicer_Render(&engine, out, 1000u));
    for (uint32_t frame = 1u; frame < 1000u; ++frame) {
        const int32_t jump = abs(left(frame) - left(frame - 1u));

        largest_jump = (jump > largest_jump) ? jump : largest_jump;
    }
    /* From 1,000 to 10,000 over 96 frames: about 94 a frame at most, with the
     * two linear fades summed. */
    CHECK(largest_jump < 200);
    CHECK(near(left(SLICER_FADE_FRAMES + 10u), 10000));
    CHECK(!(engine.voices[0].active && engine.voices[1].active)); /* the old one ended */
    Slicer_GetStatus(&engine, &status);
    CHECK(status.sounding == 9u && status.triggers == 2u && status.fades_cut == 0u);
}

/* Bounded work: never more than two voices, however fast the cuts come. */
static void test_bounded_voices(void)
{
    SlicerStatus status;

    start();
    for (unsigned trigger = 0u; trigger < 50u; ++trigger) {
        CHECK(Slicer_Trigger(&engine, (uint8_t)(trigger % 16u), 0u));
        CHECK(Slicer_Render(&engine, out, 10u)); /* shorter than a fade */
    }
    Slicer_GetStatus(&engine, &status);
    CHECK(status.fades_cut == 48u); /* every cut after the second ends a fade */
    CHECK(status.triggers == 50u);
    /* The output never exceeds the louder of the two slices. */
    start();
    CHECK(Slicer_Trigger(&engine, 15u, 0u));
    CHECK(Slicer_Render(&engine, out, 500u));
    CHECK(Slicer_Trigger(&engine, 15u, 0u));
    CHECK(Slicer_Render(&engine, out, 500u));
    for (uint32_t frame = 0u; frame < 500u; ++frame) {
        CHECK(left(frame) <= 16000);
    }
}

/* Pitch by rate: an octave up plays the slice in half the frames; an octave
 * down in twice (0022 item 15). Gate trims the pitched slice (item 17). */
static void test_pitch_and_gate(void)
{
    SlicerSetup setup;
    uint32_t sounding = 0u;

    Slicer_DefaultSetup(&setup, CLIP_SAMPLES, 16u);
    CHECK(Slicer_SliceFrames(&setup, 0u) == 9000u);
    setup.slices[0].pitch_semitones = 12;
    CHECK(Slicer_SliceFrames(&setup, 0u) == 4500u);
    setup.slices[0].pitch_semitones = -12;
    CHECK(Slicer_SliceFrames(&setup, 0u) == 18000u);
    setup.slices[0].gate_percent = 50u;
    CHECK(Slicer_SliceFrames(&setup, 0u) == 9000u);
    setup.slices[0].pitch_semitones = 7; /* 1.4983: 9,000 / 1.4983 = 6,007 */
    setup.slices[0].gate_percent = 100u;
    CHECK(Slicer_SliceFrames(&setup, 0u) >= 6005u && Slicer_SliceFrames(&setup, 0u) <= 6009u);
    CHECK(Slicer_SliceFrames(&setup, 16u) == 0u);

    /* Rendered: an octave up with half gate sounds for 2,250 frames. */
    start();
    setup = engine.setup;
    setup.slices[3].pitch_semitones = 12;
    setup.slices[3].gate_percent = 50u;
    Slicer_SetSetup(&engine, &setup);
    CHECK(Slicer_Trigger(&engine, 3u, 0u)); /* takes the staged setup */
    CHECK(Slicer_Render(&engine, out, 4000u));
    for (uint32_t frame = 0u; frame < 4000u; ++frame) {
        sounding += (left(frame) != 0) ? 1u : 0u;
    }
    CHECK(sounding >= 2240u && sounding <= 2250u);
}

static void test_level_and_clamps(void)
{
    SlicerSetup setup;
    SlicerSlice slice = {-20, 0u, 200u, 0u};

    CHECK(!Slicer_ClampSlice(&slice));
    CHECK(slice.pitch_semitones == SLICER_PITCH_MIN && slice.gate_percent == SLICER_GATE_MIN &&
          slice.level_percent == 100u);
    Slicer_DefaultSlice(&slice);
    CHECK(Slicer_ClampSlice(&slice));
    CHECK(slice.pitch_semitones == 0 && slice.gate_percent == 100u && slice.level_percent == 80u);

    start();
    setup = engine.setup;
    setup.slices[2].level_percent = 50u;
    setup.slices[4].level_percent = 0u;
    Slicer_SetSetup(&engine, &setup);
    CHECK(Slicer_Trigger(&engine, 2u, 0u));
    CHECK(Slicer_Render(&engine, out, 500u));
    CHECK(near(left(400u), 1500));
    CHECK(Slicer_Trigger(&engine, 4u, 0u));
    CHECK(Slicer_Render(&engine, out, 500u));
    CHECK(left(400u) == 0);
}

/* A disabled slice is silent, but still cuts the one before it (0022 item 13). */
static void test_disabled_slice(void)
{
    SlicerSetup setup;
    SlicerStatus status;

    start();
    setup = engine.setup;
    setup.map.enabled[6] = 0u;
    Slicer_SetSetup(&engine, &setup);
    CHECK(Slicer_Trigger(&engine, 1u, 0u));
    CHECK(Slicer_Render(&engine, out, 500u));
    CHECK(near(left(400u), 2000));
    CHECK(Slicer_Trigger(&engine, 6u, 0u));
    CHECK(Slicer_Render(&engine, out, 500u));
    CHECK(left(200u) == 0);
    Slicer_GetStatus(&engine, &status);
    CHECK(status.silent == 1u && status.sounding == SLICER_NO_SLICE);
}

/* Joined mid-step: the slice reads from where it would be, and fades in. */
static void test_offset_trigger(void)
{
    start();
    /* Slice 0 at unity reads half a sample a frame: 8,000 frames in is sample
     * 4,000, still in slice 0. */
    CHECK(Slicer_Trigger(&engine, 0u, 8000u));
    CHECK(engine.voices[engine.current].index == 4000u);
    CHECK(Slicer_Render(&engine, out, 2000u));
    CHECK(near(left(500u), 1000));
    CHECK(left(1000u) == 0); /* 1,000 frames left: ended */
    /* Past the slice's end: nothing sounds. */
    CHECK(Slicer_Trigger(&engine, 1u, 9000u));
    CHECK(!engine.voices[engine.current].active);
}

/* Silence fades the playing slice out over a fade (a transport stop). */
static void test_silence(void)
{
    start();
    CHECK(Slicer_Trigger(&engine, 2u, 0u));
    CHECK(Slicer_Render(&engine, out, 500u));
    Slicer_Silence(&engine);
    CHECK(Slicer_Render(&engine, out, 500u));
    CHECK(left(0u) > 0 && left(SLICER_FADE_FRAMES + 1u) == 0);
}

/* A setup made for another clip length is made equal again on Start, keeping
 * its count (a new clip resets the boundaries, 0022 item 14). */
static void test_start_fits_the_map(void)
{
    SlicerSetup setup;

    Slicer_Init(&engine);
    Slicer_DefaultSetup(&setup, 120000u, 8u);
    CHECK(SliceMap_SetLength(&setup.map, 0u, 30000u, SLICER_MIN_SLICE_SAMPLES));
    setup.slices[0].pitch_semitones = 5;
    Slicer_SetSetup(&engine, &setup);
    CHECK(Slicer_Start(&engine, clip, CLIP_SAMPLES));
    CHECK(engine.setup.map.count == 8u && engine.setup.map.clip_samples == CLIP_SAMPLES);
    CHECK(SliceMap_Length(&engine.setup.map, 0u) == CLIP_SAMPLES / 8u);
    CHECK(SliceMap_Valid(&engine.setup.map, SLICER_MIN_SLICE_SAMPLES));
    CHECK(engine.setup.slices[0].pitch_semitones == 5); /* parameters kept */
}

int main(void)
{
    fill_steps();
    test_stopped_and_silent();
    test_slice_plays_to_its_end();
    test_crossfade_cut();
    test_bounded_voices();
    test_pitch_and_gate();
    test_level_and_clamps();
    test_disabled_slice();
    test_offset_trigger();
    test_silence();
    test_start_fits_the_map();
    if (failures != 0u) {
        printf("%u failure(s)\n", failures);
        return 1;
    }
    printf("slicer_test: all passed\n");
    return 0;
}
