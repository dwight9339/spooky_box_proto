/* Demo-only plain loop of the Instrument clip (clip_player.c,
 * full_spooky_proto-p04.6). */
#include "clip_player.h"

#include <stdio.h>
#include <string.h>

static unsigned failures;

#define CHECK(condition) do {                                               \
    if (!(condition)) {                                                     \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);       \
        ++failures;                                                         \
    }                                                                       \
} while (0)

static ClipPlayer player;
static uint16_t stereo[64];

static int16_t left(unsigned frame)
{
    return (int16_t)stereo[2u * frame];
}

static void test_stopped_writes_nothing(void)
{
    static const int16_t clip[] = {1, 2};

    ClipPlayer_Init(&player);
    memset(stereo, 0xAA, sizeof(stereo));
    CHECK(!ClipPlayer_Active(&player));
    CHECK(!ClipPlayer_Render(&player, stereo, 8u));
    CHECK(stereo[0] == 0xAAAAu && stereo[15] == 0xAAAAu);
    CHECK(!ClipPlayer_Start(&player, NULL, 2u));
    CHECK(!ClipPlayer_Start(&player, clip, 0u));
    CHECK(!ClipPlayer_Active(&player));
    CHECK(!ClipPlayer_Render(NULL, stereo, 8u));
    CHECK(!ClipPlayer_Active(NULL));
}

/* 24 kHz to 48 kHz: each clip sample, then the mean of it and the next; the last
 * one interpolates back to the first. Both channels carry the same sample. */
static void test_upsampled_loop(void)
{
    static const int16_t clip[] = {0, 100, 200};
    static const int16_t expected[] = {0, 50, 100, 150, 200, 100, 0, 50};

    ClipPlayer_Init(&player);
    CHECK(ClipPlayer_Start(&player, clip, 3u));
    CHECK(ClipPlayer_Active(&player));
    CHECK(ClipPlayer_Render(&player, stereo, 8u));
    for (unsigned frame = 0u; frame < 8u; ++frame) {
        CHECK(left(frame) == expected[frame]);
        CHECK(stereo[2u * frame + 1u] == stereo[2u * frame]);
    }
    CHECK(player.loops == 1u);
}

static void test_extremes_encode_as_halfwords(void)
{
    static const int16_t clip[] = {-32768, 32767};

    ClipPlayer_Init(&player);
    CHECK(ClipPlayer_Start(&player, clip, 2u));
    CHECK(ClipPlayer_Render(&player, stereo, 4u));
    CHECK(stereo[0] == 0x8000u && stereo[1] == 0x8000u);
    CHECK(left(1u) == 0);       /* (-32768 + 32767) / 2 rounds toward zero */
    CHECK(stereo[4] == 0x7FFFu);
    CHECK(left(3u) == 0);
}

/* Interrupt halves split the loop anywhere; the sequence does not change. */
static void test_halves_continue(void)
{
    static const int16_t clip[] = {10, -20, 30, -40, 50};
    uint16_t whole[2u * 23u];

    ClipPlayer_Init(&player);
    CHECK(ClipPlayer_Start(&player, clip, 5u));
    CHECK(ClipPlayer_Render(&player, whole, 23u));
    CHECK(ClipPlayer_Start(&player, clip, 5u));
    CHECK(ClipPlayer_Render(&player, stereo, 7u));
    CHECK(memcmp(stereo, whole, 7u * 2u * sizeof(uint16_t)) == 0);
    CHECK(ClipPlayer_Render(&player, stereo, 16u));
    CHECK(memcmp(stereo, &whole[14], 16u * 2u * sizeof(uint16_t)) == 0);
    CHECK(player.loops == 2u);
}

/* Stop silences the clip at once; a new start plays from the beginning. */
static void test_stop_and_restart(void)
{
    static const int16_t first[] = {7, 8, 9, 10};
    static const int16_t second[] = {-5};

    ClipPlayer_Init(&player);
    CHECK(ClipPlayer_Start(&player, first, 4u));
    CHECK(ClipPlayer_Render(&player, stereo, 3u));
    ClipPlayer_Stop(&player);
    CHECK(!ClipPlayer_Active(&player));
    memset(stereo, 0xAA, sizeof(stereo));
    CHECK(!ClipPlayer_Render(&player, stereo, 3u));
    CHECK(stereo[0] == 0xAAAAu);
    CHECK(ClipPlayer_Start(&player, first, 4u));
    CHECK(ClipPlayer_Render(&player, stereo, 1u));
    CHECK(left(0u) == 7);
    /* A one-sample clip is a steady level. */
    CHECK(ClipPlayer_Start(&player, second, 1u));
    CHECK(ClipPlayer_Render(&player, stereo, 5u));
    for (unsigned frame = 0u; frame < 5u; ++frame) {
        CHECK(left(frame) == -5);
    }
}

int main(void)
{
    test_stopped_writes_nothing();
    test_upsampled_loop();
    test_extremes_encode_as_halfwords();
    test_halves_continue();
    test_stop_and_restart();
    if (failures != 0u) {
        printf("%u failure(s)\n", failures);
        return 1;
    }
    printf("clip_player_test passed\n");
    return 0;
}
