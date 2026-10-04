/* Demo-only rolling-capture catalog (rolling_catalog.c, full_spooky_proto-p04.5). */
#include "rolling_catalog.h"

#include <stdio.h>
#include <string.h>

static unsigned failures;

#define CHECK(condition) do {                                               \
    if (!(condition)) {                                                     \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);       \
        ++failures;                                                         \
    }                                                                       \
} while (0)

static RollingCatalog catalog;

/* Writes blocks the way demo_rolling.c does: rotate when a segment is full. */
static void write_blocks(uint32_t count)
{
    for (uint32_t block = 0u; block < count; ++block) {
        if (catalog.writing < 0) {
            CHECK(RollingCatalog_BeginSegment(&catalog) >= 0);
        }
        if (RollingCatalog_BlockWritten(&catalog)) {
            RollingCatalog_EndSegment(&catalog);
        }
    }
}

static unsigned count_state(RollingSlotState state)
{
    unsigned count = 0u;

    for (unsigned slot = 0u; slot < ROLLING_SLOTS; ++slot) {
        count += (catalog.slots[slot].state == (uint8_t)state) ? 1u : 0u;
    }
    return count;
}

static void test_wrap_keeps_the_window(void)
{
    RollingCatalog_Init(&catalog);
    CHECK(RollingCatalog_RetainedBlocks(&catalog) == 0u);
    for (uint32_t block = 1u; block <= 5000u; ++block) {
        write_blocks(1u);
        if (block >= ROLLING_WINDOW_BLOCKS) {
            CHECK(RollingCatalog_RetainedBlocks(&catalog) >= ROLLING_WINDOW_BLOCKS);
        } else {
            CHECK(RollingCatalog_RetainedBlocks(&catalog) == block);
        }
        CHECK(count_state(ROLLING_SLOT_WRITING) <= 1u);
    }
    CHECK(catalog.allocation_failures == 0u);
    CHECK(catalog.segments_reclaimed > 0u);
    /* After warm-up every slot is in use: twelve complete segments and one being
     * written, or thirteen complete right at a boundary. */
    CHECK(count_state(ROLLING_SLOT_EMPTY) == 0u);
    CHECK(RollingCatalog_RetainedBlocks(&catalog) <= ROLLING_SLOTS * ROLLING_SEGMENT_BLOCKS);
}

static void test_reclaim_takes_the_oldest(void)
{
    uint32_t oldest = 0xFFFFFFFFu;
    int oldest_slot = -1;
    int chosen;

    RollingCatalog_Init(&catalog);
    write_blocks(ROLLING_SLOTS * ROLLING_SEGMENT_BLOCKS); /* every slot complete */
    CHECK(count_state(ROLLING_SLOT_COMPLETE) == ROLLING_SLOTS);
    for (unsigned slot = 0u; slot < ROLLING_SLOTS; ++slot) {
        if (catalog.slots[slot].sequence < oldest) {
            oldest = catalog.slots[slot].sequence;
            oldest_slot = (int)slot;
        }
    }
    chosen = RollingCatalog_BeginSegment(&catalog);
    CHECK(chosen == oldest_slot);
    CHECK(catalog.segments_reclaimed == 1u);
    /* A second segment cannot start while one is being written. */
    CHECK(RollingCatalog_BeginSegment(&catalog) == -1);
}

static void check_pinned_in_order(void)
{
    for (unsigned index = 1u; index < catalog.pinned_count; ++index) {
        const RollingSlot *older = &catalog.slots[catalog.pinned[index - 1u]];
        const RollingSlot *newer = &catalog.slots[catalog.pinned[index]];

        CHECK(newer->sequence > older->sequence);
        CHECK(older->state == (uint8_t)ROLLING_SLOT_PINNED);
    }
}

static void test_save_pins_the_newest_window(void)
{
    uint32_t newest_sequence;

    RollingCatalog_Init(&catalog);
    const uint32_t partial = 2010u % ROLLING_SEGMENT_BLOCKS; /* 26 blocks */

    write_blocks(2010u);
    CHECK(catalog.writing >= 0);
    newest_sequence = catalog.slots[catalog.writing].sequence;
    CHECK(catalog.slots[catalog.writing].blocks == partial);
    RollingCatalog_EndSegment(&catalog);
    CHECK(RollingCatalog_PinWindow(&catalog) == ROLLING_SAVE_ACCEPTED);
    /* The partial segment plus eleven full ones: 26 + 704 blocks. */
    CHECK(catalog.pinned_count == 12u);
    CHECK(catalog.pinned_blocks == partial + ROLLING_WINDOW_BLOCKS);
    CHECK(catalog.pinned_blocks >= ROLLING_WINDOW_BLOCKS);
    CHECK(catalog.slots[catalog.pinned[catalog.pinned_count - 1u]].sequence == newest_sequence);
    check_pinned_in_order();
    /* Pinned segments are not the window's any more, and the stream continues in
     * the one remaining slot. */
    CHECK(RollingCatalog_RetainedBlocks(&catalog) == ROLLING_SEGMENT_BLOCKS);
    {
        const int next = RollingCatalog_BeginSegment(&catalog);

        CHECK(next >= 0);
        CHECK(catalog.slots[next].state == (uint8_t)ROLLING_SLOT_WRITING);
        for (unsigned index = 0u; index < catalog.pinned_count; ++index) {
            CHECK(catalog.pinned[index] != (uint8_t)next);
        }
    }
}

static void test_save_at_a_boundary_and_before_warm_up(void)
{
    RollingCatalog_Init(&catalog);
    write_blocks(ROLLING_SEGMENT_BLOCKS * 20u); /* exactly at a boundary */
    CHECK(catalog.writing < 0);
    CHECK(RollingCatalog_PinWindow(&catalog) == ROLLING_SAVE_ACCEPTED);
    CHECK(catalog.pinned_count == 11u);
    CHECK(catalog.pinned_blocks == ROLLING_WINDOW_BLOCKS);
    RollingCatalog_SaveDone(&catalog);

    /* Less than a window written: the save holds everything retained. */
    RollingCatalog_Init(&catalog);
    write_blocks(100u);
    RollingCatalog_EndSegment(&catalog);
    CHECK(RollingCatalog_PinWindow(&catalog) == ROLLING_SAVE_ACCEPTED);
    CHECK(catalog.pinned_count == 2u);
    CHECK(catalog.pinned_blocks == 100u);
    check_pinned_in_order();
}

static void test_second_save_is_busy(void)
{
    RollingCatalog_Init(&catalog);
    write_blocks(300u);
    RollingCatalog_EndSegment(&catalog);
    CHECK(RollingCatalog_CheckSave(&catalog) == ROLLING_SAVE_ACCEPTED);
    CHECK(RollingCatalog_PinWindow(&catalog) == ROLLING_SAVE_ACCEPTED);
    write_blocks(5u);
    CHECK(RollingCatalog_CheckSave(&catalog) == ROLLING_SAVE_BUSY);
    CHECK(RollingCatalog_PinWindow(&catalog) == ROLLING_SAVE_BUSY);
    /* After the first commits, another save is allowed. */
    RollingCatalog_SaveDone(&catalog);
    CHECK(!catalog.save_active);
    CHECK(count_state(ROLLING_SLOT_PINNED) == 0u);
    RollingCatalog_EndSegment(&catalog);
    CHECK(RollingCatalog_PinWindow(&catalog) == ROLLING_SAVE_ACCEPTED);
    CHECK(catalog.pinned_blocks == 5u);
}

static void test_nothing_to_save(void)
{
    RollingCatalog_Init(&catalog);
    CHECK(RollingCatalog_CheckSave(&catalog) == ROLLING_SAVE_UNAVAILABLE);
    CHECK(RollingCatalog_PinWindow(&catalog) == ROLLING_SAVE_UNAVAILABLE);
    CHECK(!catalog.save_active);
    /* Data only in the open segment cannot be pinned until it is closed. */
    write_blocks(3u);
    CHECK(RollingCatalog_PinWindow(&catalog) == ROLLING_SAVE_UNAVAILABLE);
    CHECK(!catalog.save_active);
    CHECK(count_state(ROLLING_SLOT_PINNED) == 0u);
    CHECK(RollingCatalog_RetainedBlocks(&catalog) == 3u);
}

static void test_allocation_failure_while_pinned(void)
{
    RollingCatalog_Init(&catalog);
    write_blocks(5000u);
    RollingCatalog_EndSegment(&catalog);
    CHECK(RollingCatalog_PinWindow(&catalog) == ROLLING_SAVE_ACCEPTED);
    /* The stream fills the free slot while the save still holds the rest. */
    write_blocks(ROLLING_SEGMENT_BLOCKS);
    CHECK(catalog.writing < 0);
    {
        const uint32_t failures_before = catalog.allocation_failures;
        unsigned free_slots = count_state(ROLLING_SLOT_EMPTY) +
                              count_state(ROLLING_SLOT_COMPLETE);

        if (free_slots == 1u) {
            /* Only the segment just written is not pinned: it is reclaimed, which
             * keeps the stream going at the cost of the newest unsaved audio. */
            CHECK(RollingCatalog_BeginSegment(&catalog) >= 0);
        }
        RollingCatalog_EndSegment(&catalog);
        /* Pin every remaining slot to force exhaustion. */
        for (unsigned slot = 0u; slot < ROLLING_SLOTS; ++slot) {
            catalog.slots[slot].state = (uint8_t)ROLLING_SLOT_PINNED;
        }
        CHECK(RollingCatalog_BeginSegment(&catalog) == -1);
        CHECK(catalog.allocation_failures == failures_before + 1u);
    }
    /* Releasing slots as their files are renamed makes room again. */
    RollingCatalog_SlotReleased(&catalog, 4u);
    CHECK(catalog.slots[4].state == (uint8_t)ROLLING_SLOT_EMPTY);
    CHECK(RollingCatalog_BeginSegment(&catalog) == 4);
}

static void test_discard_keeps_a_save(void)
{
    RollingCatalog_Init(&catalog);
    write_blocks(900u);
    RollingCatalog_EndSegment(&catalog);
    CHECK(RollingCatalog_PinWindow(&catalog) == ROLLING_SAVE_ACCEPTED);
    write_blocks(20u);
    RollingCatalog_Discard(&catalog);
    CHECK(catalog.writing < 0);
    CHECK(RollingCatalog_RetainedBlocks(&catalog) == 0u);
    CHECK(count_state(ROLLING_SLOT_PINNED) == catalog.pinned_count);
    CHECK(catalog.save_active);
    RollingCatalog_SaveDone(&catalog);
    CHECK(count_state(ROLLING_SLOT_EMPTY) == ROLLING_SLOTS);
    /* Null arguments are ignored. */
    RollingCatalog_Init(NULL);
    RollingCatalog_Discard(NULL);
    CHECK(RollingCatalog_BeginSegment(NULL) == -1);
    CHECK(!RollingCatalog_BlockWritten(NULL));
    RollingCatalog_EndSegment(NULL);
    CHECK(RollingCatalog_RetainedBlocks(NULL) == 0u);
    CHECK(RollingCatalog_CheckSave(NULL) == ROLLING_SAVE_UNAVAILABLE);
    RollingCatalog_SlotReleased(NULL, 0u);
    RollingCatalog_SaveDone(NULL);
}

int main(void)
{
    test_wrap_keeps_the_window();
    test_reclaim_takes_the_oldest();
    test_save_pins_the_newest_window();
    test_save_at_a_boundary_and_before_warm_up();
    test_second_save_is_busy();
    test_nothing_to_save();
    test_allocation_failure_while_pinned();
    test_discard_keeps_a_save();
    if (failures != 0u) {
        printf("%u failure(s)\n", failures);
        return 1;
    }
    printf("rolling_catalog_test passed\n");
    return 0;
}
