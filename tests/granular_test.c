/* One bounded granular voice (Common/Src/granular.c, full_spooky_proto-p04.7). */
#include "granular.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned failures;

#define CHECK(condition) do {                                               \
    if (!(condition)) {                                                     \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);       \
        ++failures;                                                         \
    }                                                                       \
} while (0)

static GranularEngine engine;
static GranularEngine other;
static int16_t clip[1000];
static uint16_t stereo[2u * 8192u];
static uint16_t reference[2u * 8192u];

static int16_t left(unsigned frame)
{
    return (int16_t)stereo[2u * frame];
}

static GranularParams params_with(uint16_t density, uint16_t size_ms, uint8_t envelope)
{
    GranularParams params;

    Granular_DefaultParams(&params);
    params.density = density;
    params.size_ms = size_ms;
    params.envelope_percent = envelope;
    params.level_percent = 100u;
    return params;
}

static void test_stopped_writes_nothing(void)
{
    Granular_Init(&engine, 1u);
    memset(stereo, 0xAA, 64u);
    CHECK(!Granular_Active(&engine));
    CHECK(!Granular_Render(&engine, stereo, 16u));
    CHECK(stereo[0] == 0xAAAAu);
    CHECK(!Granular_Start(&engine, NULL, 10u));
    CHECK(!Granular_Start(&engine, clip, 0u));
    CHECK(!Granular_Render(NULL, stereo, 16u));
    CHECK(!Granular_Active(NULL));
    CHECK(Granular_Start(&engine, clip, 1000u));
    Granular_Stop(&engine);
    CHECK(!Granular_Render(&engine, stereo, 16u));
}

static void test_clamp(void)
{
    GranularParams params;

    Granular_DefaultParams(&params);
    CHECK(Granular_ClampParams(&params));
    params.position_permille = 1200u;
    params.size_ms = 1u;
    params.density = 500u;
    params.pitch_semitones = -40;
    params.spray_permille = 2000u;
    params.envelope_percent = 150u;
    params.level_percent = 101u;
    CHECK(!Granular_ClampParams(&params));
    CHECK(params.position_permille == 1000u && params.size_ms == GRANULAR_SIZE_MS_MIN);
    CHECK(params.density == GRANULAR_DENSITY_MAX && params.pitch_semitones == GRANULAR_PITCH_MIN);
    CHECK(params.spray_permille == 1000u);
    CHECK(params.envelope_percent == 100u && params.level_percent == 100u);
    params.pitch_semitones = 30;
    Granular_ClampParams(&params);
    CHECK(params.pitch_semitones == GRANULAR_PITCH_MAX);
}

/* Triangles overlapping by half add to a steady level: a constant clip comes out
 * at level / sqrt(overlap) once the second grain is in. */
static void test_known_sample_steady(void)
{
    const GranularParams params = params_with(20u, 100u, 100u); /* overlap 2 */

    for (unsigned index = 0u; index < 1000u; ++index) {
        clip[index] = 10000;
    }
    Granular_Init(&engine, 7u);
    Granular_SetParams(&engine, &params);
    CHECK(Granular_Start(&engine, clip, 1000u));
    CHECK(Granular_Render(&engine, stereo, 8192u));
    CHECK(left(0u) == 0);           /* the first grain ramps up from silence */
    for (unsigned frame = 2400u; frame < 8192u; ++frame) {
        CHECK(abs(left(frame) - 7071) <= 4);
        CHECK(stereo[2u * frame + 1u] == stereo[2u * frame]);
    }
}

/* Pitch is the read rate: the clip is at half the output rate, so unity reads
 * half a sample per frame, an octave up one, an octave down a quarter. */
static void test_known_sample_pitch(void)
{
    static const int8_t pitches[] = {0, 12, -12};
    static const int32_t slopes_x4[] = {8, 16, 4}; /* output rise per 4 frames */

    for (unsigned index = 0u; index < 1000u; ++index) {
        clip[index] = (int16_t)(index * 4u);
    }
    for (unsigned case_index = 0u; case_index < 3u; ++case_index) {
        GranularParams params = params_with(1u, 500u, 0u); /* one grain, 2 ms ramps */

        params.pitch_semitones = pitches[case_index];
        Granular_Init(&engine, 3u);
        Granular_SetParams(&engine, &params);
        CHECK(Granular_Start(&engine, clip, 1000u));
        CHECK(Granular_Render(&engine, stereo, 400u));
        for (unsigned frame = 100u; frame < 300u; frame += 4u) {
            const int32_t expected = 2000 + (int32_t)(frame * (unsigned)slopes_x4[case_index]) / 4;

            CHECK(abs(left(frame) - expected) <= 3); /* two Q15 truncations */
        }
    }
}

/* More grains due than voices: the count stays bounded and the rest are dropped. */
static void test_bounded_grains(void)
{
    const GranularParams params = params_with(100u, 500u, 50u); /* overlap 50 */
    GranularStatus status;
    uint16_t positions[GRANULAR_MAX_GRAINS + 4u];
    uint8_t envelopes[GRANULAR_MAX_GRAINS + 4u];

    Granular_Init(&engine, 11u);
    Granular_SetParams(&engine, &params);
    CHECK(Granular_Start(&engine, clip, 1000u));
    for (unsigned half = 0u; half < 100u; ++half) {
        CHECK(Granular_Render(&engine, stereo, 512u));
    }
    Granular_GetStatus(&engine, &status);
    CHECK(status.active == GRANULAR_MAX_GRAINS);
    CHECK(status.active_high_water == GRANULAR_MAX_GRAINS);
    CHECK(status.grains_dropped > 0u);
    CHECK(status.grains_started + status.grains_dropped == 1u + (100u * 512u) / 480u);
    CHECK(status.renders == 100u);
    CHECK(Granular_GetGrains(&engine, positions, envelopes, GRANULAR_MAX_GRAINS + 4u) ==
          GRANULAR_MAX_GRAINS);
    CHECK(Granular_GetGrains(&engine, positions, envelopes, 3u) == 3u);
    for (unsigned grain = 0u; grain < 3u; ++grain) {
        CHECK(positions[grain] < 1000u);
    }
}

/* A grain limit caps the sounding grains below GRANULAR_MAX_GRAINS; grains due
 * at the limit are dropped. Grains above a lowered limit finish; a raised limit
 * lets more start. The limit is clamped to 1..GRANULAR_MAX_GRAINS. */
static void test_grain_limit(void)
{
    const GranularParams params = params_with(100u, 500u, 50u); /* overlap 50 */
    GranularStatus status;

    Granular_Init(&engine, 13u);
    CHECK(Granular_MaxGrains(&engine) == GRANULAR_MAX_GRAINS);
    Granular_SetMaxGrains(&engine, 0u);
    CHECK(Granular_MaxGrains(&engine) == 1u);
    Granular_SetMaxGrains(&engine, 99u);
    CHECK(Granular_MaxGrains(&engine) == GRANULAR_MAX_GRAINS);
    Granular_SetMaxGrains(&engine, 3u);
    Granular_SetParams(&engine, &params);
    CHECK(Granular_Start(&engine, clip, 1000u));
    for (unsigned half = 0u; half < 100u; ++half) {
        CHECK(Granular_Render(&engine, stereo, 512u));
    }
    Granular_GetStatus(&engine, &status);
    CHECK(status.active == 3u && status.active_high_water == 3u);
    CHECK(status.grains_dropped > 0u);
    CHECK(status.grains_started + status.grains_dropped == 1u + (100u * 512u) / 480u);
    Granular_SetMaxGrains(&engine, 8u);
    for (unsigned half = 0u; half < 100u; ++half) {
        CHECK(Granular_Render(&engine, stereo, 512u));
    }
    Granular_GetStatus(&engine, &status);
    CHECK(status.active == 8u && status.active_high_water == 8u);
    Granular_SetMaxGrains(&engine, 2u);
    CHECK(Granular_Render(&engine, stereo, 512u));
    Granular_GetStatus(&engine, &status);
    CHECK(status.active <= 8u);
    for (unsigned half = 0u; half < 100u; ++half) { /* 500 ms grains end within 47 halves */
        CHECK(Granular_Render(&engine, stereo, 512u));
    }
    Granular_GetStatus(&engine, &status);
    CHECK(status.active == 2u);
}

/* Without spray every grain starts at position. Spray stays within its share
 * of the clip around position; a full spray reaches every part of the clip. */
static void test_position_and_spray(void)
{
    GranularParams params = params_with(100u, 10u, 0u);
    GranularStatus status;
    bool seen[4] = {false, false, false, false};

    params.position_permille = 600u;
    Granular_Init(&engine, 5u);
    Granular_SetParams(&engine, &params);
    CHECK(Granular_Start(&engine, clip, 1000u));
    for (unsigned half = 0u; half < 20u; ++half) {
        Granular_Render(&engine, stereo, 512u);
        Granular_GetStatus(&engine, &status);
        CHECK(status.last_start == 600u);
    }
    params.position_permille = 500u;
    params.spray_permille = 200u;
    Granular_SetParams(&engine, &params);
    for (unsigned half = 0u; half < 400u; ++half) {
        Granular_Render(&engine, stereo, 480u); /* one grain per render */
        Granular_GetStatus(&engine, &status);
        CHECK(status.last_start >= 400u && status.last_start <= 600u);
    }
    params.spray_permille = 1000u;
    Granular_SetParams(&engine, &params);
    for (unsigned half = 0u; half < 400u; ++half) {
        Granular_Render(&engine, stereo, 480u);
        Granular_GetStatus(&engine, &status);
        CHECK(status.last_start < 1000u);
        seen[(status.last_start / 250u) % 4u] = true;
    }
    CHECK(seen[0] && seen[1] && seen[2] && seen[3]);
}

/* Interrupt halves split the stream anywhere; the output does not change. */
static void test_chunks_match(void)
{
    GranularParams params = params_with(37u, 45u, 30u);

    params.spray_permille = 300u;
    params.pitch_semitones = 5;
    for (unsigned index = 0u; index < 1000u; ++index) {
        clip[index] = (int16_t)((index * 7919u) % 20000u) - 10000;
    }
    Granular_Init(&engine, 99u);
    Granular_SetParams(&engine, &params);
    Granular_Start(&engine, clip, 1000u);
    CHECK(Granular_Render(&engine, reference, 4096u));
    Granular_Init(&other, 99u);
    Granular_SetParams(&other, &params);
    Granular_Start(&other, clip, 1000u);
    {
        static const unsigned sizes[] = {1u, 300u, 512u, 7u, 1000u, 2276u};
        unsigned done = 0u;

        for (unsigned index = 0u; index < 6u; ++index) {
            CHECK(Granular_Render(&other, &stereo[2u * done], sizes[index]));
            done += sizes[index];
        }
        CHECK(done == 4096u);
    }
    CHECK(memcmp(stereo, reference, 4096u * 2u * sizeof(uint16_t)) == 0);
}

/* New parameters take effect at the next render, not part-way through one, and
 * a shorter interval brings the next grain forward. */
static void test_params_take_effect_at_render(void)
{
    GranularParams params = params_with(1u, 100u, 50u);
    GranularStatus status;

    Granular_Init(&engine, 13u);
    Granular_SetParams(&engine, &params);
    Granular_Start(&engine, clip, 1000u);
    Granular_Render(&engine, stereo, 512u);
    Granular_GetStatus(&engine, &status);
    CHECK(status.grains_started == 1u);
    params.density = 100u;
    Granular_SetParams(&engine, &params);
    Granular_GetStatus(&engine, &status);
    CHECK(status.grains_started == 1u);
    Granular_Render(&engine, stereo, 512u);
    Granular_GetStatus(&engine, &status);
    CHECK(status.grains_started == 2u); /* brought forward to 480 frames in */
}

/* Full-scale grains summed past the output range saturate, never wrap. */
static void test_saturates(void)
{
    GranularParams params = params_with(100u, 500u, 100u);

    for (unsigned sign = 0u; sign < 2u; ++sign) {
        for (unsigned index = 0u; index < 1000u; ++index) {
            clip[index] = sign ? -32768 : 32767;
        }
        Granular_Init(&engine, 17u);
        params.level_percent = 100u;
        Granular_SetParams(&engine, &params);
        Granular_Start(&engine, clip, 1000u);
        for (unsigned half = 0u; half < 40u; ++half) {
            Granular_Render(&engine, stereo, 512u);
            for (unsigned frame = 0u; frame < 512u; ++frame) {
                CHECK(sign ? (left(frame) <= 0) : (left(frame) >= 0));
            }
        }
    }
    /* Level 0 is silence. */
    params.level_percent = 0u;
    Granular_SetParams(&engine, &params);
    Granular_Render(&engine, stereo, 512u);
    for (unsigned frame = 0u; frame < 512u; ++frame) {
        CHECK(left(frame) == 0);
    }
}

int main(void)
{
    test_stopped_writes_nothing();
    test_clamp();
    test_known_sample_steady();
    test_known_sample_pitch();
    test_bounded_grains();
    test_grain_limit();
    test_position_and_spray();
    test_chunks_match();
    test_params_take_effect_at_render();
    test_saturates();
    if (failures != 0u) {
        printf("%u failure(s)\n", failures);
        return 1;
    }
    printf("granular_test passed\n");
    return 0;
}
