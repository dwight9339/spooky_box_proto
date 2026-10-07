/* The Slicer's slice map (Common/Src/slice_map.c, full_spooky_proto-p04.15,
 * decision 0022 items 1, 7 and 11). */
#include "slice_map.h"

#include <stdio.h>

static unsigned failures;

#define CHECK(condition) do {                                               \
    if (!(condition)) {                                                     \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);       \
        ++failures;                                                         \
    }                                                                       \
} while (0)

#define MIN_LENGTH 240u

/* No gaps or overlap: each slice ends where the next starts, the first starts
 * at 0 and the last ends at the clip's end, and the lengths add up to it. */
static void check_covers(const SliceMap *map)
{
    uint32_t total = 0u;

    CHECK(SliceMap_Start(map, 0u) == 0u);
    for (uint8_t slice = 0u; slice < map->count; ++slice) {
        if (slice + 1u < map->count) {
            CHECK(SliceMap_End(map, slice) == SliceMap_Start(map, (uint8_t)(slice + 1u)));
        }
        CHECK(SliceMap_End(map, slice) > SliceMap_Start(map, slice));
        total += SliceMap_Length(map, slice);
    }
    CHECK(SliceMap_End(map, (uint8_t)(map->count - 1u)) == map->clip_samples);
    CHECK(total == map->clip_samples);
}

static void test_equal_slices(void)
{
    static const uint8_t counts[] = {4u, 8u, 16u, 1u, 3u};
    static const uint32_t clips[] = {72000u, 120000u, 24000u, 71999u, 4001u};
    SliceMap map;

    for (unsigned c = 0u; c < sizeof(counts); ++c) {
        for (unsigned l = 0u; l < sizeof(clips) / sizeof(clips[0]); ++l) {
            CHECK(SliceMap_InitEqual(&map, clips[l], counts[c]));
            CHECK(map.count == counts[c]);
            check_covers(&map);
            CHECK(SliceMap_Valid(&map, MIN_LENGTH));
            for (uint8_t slice = 0u; slice < map.count; ++slice) {
                const uint32_t length = SliceMap_Length(&map, slice);

                CHECK(SliceMap_Enabled(&map, slice));
                /* Equal to within one sample. */
                CHECK(length == clips[l] / counts[c] || length == clips[l] / counts[c] + 1u);
            }
        }
    }
    /* A 3 s clip in 16: 4,500 samples each. */
    CHECK(SliceMap_InitEqual(&map, 72000u, 16u));
    CHECK(SliceMap_Start(&map, 5u) == 22500u && SliceMap_Length(&map, 5u) == 4500u);
}

static void test_rejects(void)
{
    SliceMap map;

    CHECK(!SliceMap_InitEqual(&map, 72000u, 0u) && map.count == 0u);
    CHECK(!SliceMap_InitEqual(&map, 72000u, 17u) && map.count == 0u);
    CHECK(!SliceMap_InitEqual(&map, 15u, 16u) && map.count == 0u);
    CHECK(!SliceMap_Valid(&map, 0u));
    CHECK(!SliceMap_InitEqual(NULL, 72000u, 16u));
    CHECK(SliceMap_InitEqual(&map, 72000u, 4u));
    CHECK(SliceMap_Start(&map, 4u) == 0u && SliceMap_Length(&map, 9u) == 0u);
    CHECK(!SliceMap_Enabled(&map, 4u));
}

/* Length moves the boundary shared with the next slice; both keep the minimum,
 * and the last slice's end is the clip end (0022 items 6 and 7). */
static void test_length_editing(void)
{
    SliceMap map;

    CHECK(SliceMap_InitEqual(&map, 72000u, 4u)); /* 18,000 each */
    CHECK(SliceMap_SetLength(&map, 1u, 20000u, MIN_LENGTH));
    CHECK(SliceMap_Length(&map, 1u) == 20000u && SliceMap_Length(&map, 2u) == 16000u);
    CHECK(SliceMap_Start(&map, 1u) == 18000u); /* its start does not move */
    check_covers(&map);

    /* Longer than room for the next slice: held at the next slice's minimum. */
    CHECK(SliceMap_SetLength(&map, 1u, 100000u, MIN_LENGTH));
    CHECK(SliceMap_Length(&map, 2u) == MIN_LENGTH);
    CHECK(!SliceMap_SetLength(&map, 1u, 100000u, MIN_LENGTH)); /* nothing moved */
    /* Shorter than the minimum: held at it. */
    CHECK(SliceMap_SetLength(&map, 1u, 3u, MIN_LENGTH));
    CHECK(SliceMap_Length(&map, 1u) == MIN_LENGTH);
    check_covers(&map);
    CHECK(SliceMap_Valid(&map, MIN_LENGTH));

    /* The last slice is not edited here. */
    CHECK(!SliceMap_SetLength(&map, 3u, 1000u, MIN_LENGTH));
    CHECK(SliceMap_End(&map, 3u) == 72000u);

    /* Many random edits keep every invariant. */
    {
        uint32_t random = 12345u;

        CHECK(SliceMap_InitEqual(&map, 72000u, 16u));
        for (unsigned round = 0u; round < 5000u; ++round) {
            random = random * 1103515245u + 12345u;
            (void)SliceMap_SetLength(&map, (uint8_t)((random >> 8) % 16u), (random >> 12) % 20000u,
                                     MIN_LENGTH);
            if (!SliceMap_Valid(&map, MIN_LENGTH)) {
                CHECK(SliceMap_Valid(&map, MIN_LENGTH));
                break;
            }
        }
        check_covers(&map);
    }
}

static void test_slice_at(void)
{
    SliceMap map;

    CHECK(SliceMap_InitEqual(&map, 72000u, 4u));
    CHECK(SliceMap_SliceAt(&map, 0u) == 0u);
    CHECK(SliceMap_SliceAt(&map, 17999u) == 0u);
    CHECK(SliceMap_SliceAt(&map, 18000u) == 1u);
    CHECK(SliceMap_SliceAt(&map, 71999u) == 3u);
    CHECK(SliceMap_SliceAt(&map, 999999u) == 3u);
}

/* 0022 item 11: a step moves to the new slice holding its old slice's start. */
static void test_remap(void)
{
    SliceMap sixteen;
    SliceMap four;
    SliceMap eight;

    CHECK(SliceMap_InitEqual(&sixteen, 72000u, 16u));
    CHECK(SliceMap_InitEqual(&four, 72000u, 4u));
    CHECK(SliceMap_InitEqual(&eight, 72000u, 8u));
    for (uint8_t slice = 0u; slice < 16u; ++slice) {
        CHECK(SliceMap_Remap(&sixteen, slice, &four) == slice / 4u);
        CHECK(SliceMap_Remap(&sixteen, slice, &eight) == slice / 2u);
    }
    for (uint8_t slice = 0u; slice < 4u; ++slice) {
        CHECK(SliceMap_Remap(&four, slice, &sixteen) == slice * 4u);
    }
    /* By time, not by index: a custom boundary decides. */
    CHECK(SliceMap_SetLength(&four, 1u, 22000u, MIN_LENGTH)); /* slice 2 starts at 40,000 */
    CHECK(SliceMap_Remap(&four, 2u, &sixteen) == 8u);          /* 40,000 is in 36,000..40,500 */
    /* Identity remap. */
    for (uint8_t slice = 0u; slice < 16u; ++slice) {
        CHECK(SliceMap_Remap(&sixteen, slice, &sixteen) == slice);
    }
    /* A slice past the old map is held at its last. */
    CHECK(SliceMap_Remap(&eight, 12u, &four) == 3u);
}

int main(void)
{
    test_equal_slices();
    test_rejects();
    test_length_editing();
    test_slice_at();
    test_remap();
    if (failures != 0u) {
        printf("%u failure(s)\n", failures);
        return 1;
    }
    printf("slice_map_test: all passed\n");
    return 0;
}
