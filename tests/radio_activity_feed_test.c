/*
 * Host tests for the radio activity feed (full_spooky_proto-54w.32): the ring
 * from the capture callback, retune intervals as "no radio measurement"
 * (decision 0015) and onsets for the activity hold. Host results only.
 */

#include "radio_activity_feed.h"

#include <stdio.h>
#include <string.h>

static unsigned failures;

#define CHECK(condition) do {                                               \
    if (!(condition)) {                                                     \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);       \
        ++failures;                                                         \
    }                                                                       \
} while (0)

/* One radio half-buffer: 512 stereo frames. */
#define HALF_SAMPLES 1024u

static uint32_t next_block;
static uint32_t now_ms;

static void start(void)
{
    CHECK(RadioActivityFeed_Init());
    next_block = 0u;
    now_ms = 1000u;
}

/* The capture callback delivers `count` blocks at a steady level. */
static void post(int16_t level, unsigned count)
{
    int16_t samples[HALF_SAMPLES];
    unsigned index;

    for (index = 0u; index < HALF_SAMPLES; ++index) {
        samples[index] = ((index & 1u) != 0u) ? (int16_t)-level : level;
    }
    for (index = 0u; index < count; ++index) {
        RadioActivityFeed_OnBlock(next_block++, samples, HALF_SAMPLES);
    }
}

/* The block in progress is the next one the callback will deliver. */
static void service(bool running, bool tuning)
{
    RadioActivityFeed_Service(now_ms, running, tuning, true, next_block);
}

static void retune_with(uint32_t settle_blocks)
{
    RadioActivityFeed_OnRetuneStart(true, next_block, settle_blocks);
}

/* An interval with decision 0015 item 5's starting margin, one block. */
static void retune(void)
{
    retune_with(RADIO_ACTIVITY_FEED_SETTLE_BLOCKS);
}

/* Blocks at a steady level with the radio settled, drained as they arrive. */
static void settled(int16_t level, unsigned count)
{
    unsigned index;

    for (index = 0u; index < count; ++index) {
        post(level, 1u);
        service(true, false);
    }
}

static RadioActivityFeedStatus status_now(void)
{
    RadioActivityFeedStatus status;

    memset(&status, 0, sizeof(status));
    RadioActivityFeed_GetStatus(now_ms, &status);
    return status;
}

static void a_rise_fires_one_onset(void)
{
    start();
    settled(1000, 50u);
    CHECK(RadioActivityFeed_TakeOnset(RADIO_ACTIVITY_READER_CLASSIC) == RADIO_ONSET_NONE);
    CHECK(RadioActivityFeed_Valid(now_ms));
    settled(10000, 10u);
    CHECK(RadioActivityFeed_TakeOnset(RADIO_ACTIVITY_READER_CLASSIC) == RADIO_ONSET_LARGE);
    CHECK(RadioActivityFeed_TakeOnset(RADIO_ACTIVITY_READER_CLASSIC) == RADIO_ONSET_NONE);
    CHECK(status_now().blocks == 60u && status_now().measured == 60u);
}

/* Decision 0015 items 4 to 6: from the block in progress at the start stamp to
 * the block in progress when the radio is seen settled, plus one block. */
static void a_retune_interval_is_not_measured(void)
{
    start();
    settled(1000, 100u);              /* blocks 0 to 99 */
    retune(); /* block 100 in progress */
    CHECK(status_now().retuning && status_now().retunes == 1u);
    post(0, 6u);                      /* 100 to 105: the receiver mutes */
    service(true, true);
    CHECK(!RadioActivityFeed_Valid(now_ms));
    post(1000, 2u);                   /* 106, 107 */
    service(true, false);             /* settled seen with 108 in progress */
    CHECK(!status_now().retuning);
    CHECK(status_now().measured == 100u);
    post(1000, 3u);                   /* 108 and 109 settle; 110 measured */
    service(true, false);
    CHECK(status_now().blocks == 111u && status_now().measured == 101u);
    CHECK(RadioActivityFeed_Valid(now_ms));
    CHECK(RadioActivityFeed_TakeOnset(RADIO_ACTIVITY_READER_CLASSIC) == RADIO_ONSET_NONE);

    /* A block delivered before the stamp but drained only by the pass that
     * closes the interval is still measured. */
    post(1000, 1u);                   /* 111 */
    retune();                         /* from 112 */
    post(1000, 1u);                   /* 112 */
    service(true, false);             /* closes through 114 */
    CHECK(status_now().blocks == 113u && status_now().measured == 102u);
}

/* The mute and the return of the same level fire nothing; a louder new
 * landing after the interval is measured against the held averages. */
static void the_mute_end_is_not_an_onset(void)
{
    start();
    settled(1000, 100u);
    retune();
    post(0, 12u);                     /* about 128 ms, like an AM tune */
    service(true, true);
    post(1000, 1u);
    service(true, false);
    settled(1000, 40u);
    CHECK(RadioActivityFeed_TakeOnset(RADIO_ACTIVITY_READER_CLASSIC) == RADIO_ONSET_NONE);

    retune();
    post(0, 6u);
    service(true, true);
    post(1000, 1u);
    service(true, false);
    post(1000, 1u);
    post(5000, 10u);                  /* a louder station */
    service(true, false);
    CHECK(RadioActivityFeed_TakeOnset(RADIO_ACTIVITY_READER_CLASSIC) >= RADIO_ONSET_MEDIUM);
}

/* Decision 0015 item 8: back-to-back tunes give adjoining intervals. */
static void back_to_back_retunes_adjoin(void)
{
    start();
    post(1000, 10u);                  /* 0 to 9 */
    service(true, false);
    retune(); /* from 10 */
    post(0, 3u);                      /* 10 to 12 */
    retune(); /* the open interval continues */
    post(0, 3u);                      /* 13 to 15 */
    service(true, true);
    CHECK(status_now().retunes == 2u && status_now().measured == 10u);
    post(1000, 2u);                   /* 16, 17 */
    service(true, false);             /* closes through 19 */
    post(1000, 3u);                   /* 18, 19 unmeasured, 20 measured */
    service(true, false);
    CHECK(status_now().blocks == 21u && status_now().measured == 11u);
}

/* Decision 0015 item 10 can set a band's margin to zero: the interval ends
 * with the block in progress when the radio is seen settled. */
static void a_zero_margin_measures_the_next_block(void)
{
    start();
    settled(1000, 10u);               /* 0 to 9 */
    retune_with(0u);                  /* from 10 */
    post(0, 3u);                      /* 10 to 12 */
    service(true, true);
    post(1000, 1u);                   /* 13 */
    service(true, false);             /* closes through 14, in progress */
    post(1000, 2u);                   /* 14 unmeasured, 15 measured */
    service(true, false);
    CHECK(status_now().blocks == 16u && status_now().measured == 11u);
}

/* Adjoining intervals keep the larger margin, so an unqualified band switch
 * is never shortened by a tune that follows it. */
static void adjoining_intervals_keep_the_larger_margin(void)
{
    start();
    settled(1000, 10u);               /* 0 to 9 */
    retune_with(2u);                  /* from 10 */
    post(0, 2u);                      /* 10, 11 */
    retune_with(0u);                  /* continues; margin stays 2 */
    post(0, 2u);                      /* 12, 13 */
    service(true, false);             /* closes through 16 */
    post(1000, 4u);                   /* 14 to 16 unmeasured, 17 measured */
    service(true, false);
    CHECK(status_now().blocks == 18u && status_now().measured == 11u);
}

/* Without a stream position the interval starts at block 0 and stays open
 * until the position can be read again. */
static void an_unknown_position_keeps_blocks_unmeasured(void)
{
    start();
    settled(1000, 10u);
    RadioActivityFeed_OnRetuneStart(false, 12345u, RADIO_ACTIVITY_FEED_SETTLE_BLOCKS);
    post(1000, 3u);
    RadioActivityFeed_Service(now_ms, true, false, false, 12345u);
    CHECK(status_now().retuning && status_now().measured == 10u);
    post(1000, 3u);                   /* 13 to 15 */
    service(true, false);             /* closes through 17 */
    CHECK(!status_now().retuning && status_now().measured == 10u);
    settled(1000, 3u);                /* 16, 17 unmeasured, 18 measured */
    CHECK(status_now().measured == 11u);
}

static void nothing_is_measured_while_the_radio_is_not_running(void)
{
    start();
    post(1000, 20u);
    service(false, false);
    CHECK(status_now().measured == 0u && !RadioActivityFeed_Valid(now_ms));
    post(20000, 5u);
    service(false, false);
    CHECK(RadioActivityFeed_TakeOnset(RADIO_ACTIVITY_READER_CLASSIC) == RADIO_ONSET_NONE);
    /* The radio starts: the first measured block seeds the averages. */
    post(1000, 1u);
    service(true, false);
    CHECK(RadioActivityFeed_Valid(now_ms));
    CHECK(status_now().measured == 1u);
}

static void the_measurement_goes_stale(void)
{
    start();
    post(1000, 5u);
    service(true, false);
    CHECK(RadioActivityFeed_Valid(now_ms + RADIO_ACTIVITY_FEED_STALE_MS));
    CHECK(!RadioActivityFeed_Valid(now_ms + RADIO_ACTIVITY_FEED_STALE_MS + 1u));
    /* Passes with no new block do not refresh it. */
    now_ms += 200u;
    service(true, false);
    CHECK(!RadioActivityFeed_Valid(now_ms));
    CHECK(!status_now().valid);
}

static void a_full_ring_drops_the_newest(void)
{
    start();
    post(1000, RADIO_ACTIVITY_FEED_CAPACITY + 8u);
    CHECK(status_now().dropped == 8u);
    service(true, false);
    CHECK(status_now().high_water == RADIO_ACTIVITY_FEED_CAPACITY);
    CHECK(status_now().blocks == RADIO_ACTIVITY_FEED_CAPACITY);
    post(1000, 1u);
    service(true, false);
    CHECK(status_now().blocks == RADIO_ACTIVITY_FEED_CAPACITY + 1u);
}

static void a_band_switch_reseeds_the_averages(void)
{
    start();
    settled(1000, 50u);
    post(1000, 5u);                   /* the old band's last blocks, waiting */
    RadioActivityFeed_ResetAverages();
    post(10000, 10u);
    service(true, false);
    CHECK(RadioActivityFeed_TakeOnset(RADIO_ACTIVITY_READER_CLASSIC) == RADIO_ONSET_NONE);
}

/* Each reader sees every onset once, independently of the other. */
static void each_reader_takes_its_own_onsets(void)
{
    start();
    settled(1000, 100u);
    settled(10000, 10u);
    CHECK(RadioActivityFeed_TakeOnset(RADIO_ACTIVITY_READER_MATRIX) == RADIO_ONSET_LARGE);
    CHECK(RadioActivityFeed_TakeOnset(RADIO_ACTIVITY_READER_MATRIX) == RADIO_ONSET_NONE);
    CHECK(RadioActivityFeed_TakeOnset(RADIO_ACTIVITY_READER_CLASSIC) == RADIO_ONSET_LARGE);
    CHECK(RadioActivityFeed_TakeOnset(RADIO_ACTIVITY_READER_COUNT) == RADIO_ONSET_NONE);
}

static void the_largest_onset_is_kept_until_taken(void)
{
    start();
    settled(1000, 100u);
    settled(10000, 10u);
    settled(100, 300u);               /* falls and re-arms */
    post(200, 10u);                   /* a smaller rise */
    service(true, false);
    CHECK(RadioActivityFeed_TakeOnset(RADIO_ACTIVITY_READER_CLASSIC) == RADIO_ONSET_LARGE);
}

int main(void)
{
    a_rise_fires_one_onset();
    a_retune_interval_is_not_measured();
    the_mute_end_is_not_an_onset();
    back_to_back_retunes_adjoin();
    a_zero_margin_measures_the_next_block();
    adjoining_intervals_keep_the_larger_margin();
    an_unknown_position_keeps_blocks_unmeasured();
    nothing_is_measured_while_the_radio_is_not_running();
    the_measurement_goes_stale();
    a_full_ring_drops_the_newest();
    a_band_switch_reseeds_the_averages();
    the_largest_onset_is_kept_until_taken();
    each_reader_takes_its_own_onsets();
    RadioActivityFeed_GetStatus(0u, NULL);

    if (failures != 0u) {
        printf("%u failure(s)\n", failures);
        return 1;
    }
    printf("radio_activity_feed_test: all checks passed\n");
    return 0;
}
