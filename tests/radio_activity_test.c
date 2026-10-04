#include "radio_activity.h"

#include <stdio.h>

static unsigned failures;

#define CHECK(condition) do {                                               \
    if (!(condition)) {                                                     \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);       \
        ++failures;                                                         \
    }                                                                       \
} while (0)

typedef struct {
    unsigned count;
    RadioOnset first;
    unsigned first_block; /* 1-based within the feed */
} FeedResult;

static FeedResult feed(uint32_t level, unsigned blocks, bool measuring)
{
    FeedResult result = {0u, RADIO_ONSET_NONE, 0u};
    unsigned index;

    for (index = 0u; index < blocks; ++index) {
        const RadioOnset onset = RadioActivity_OnBlock(level, measuring);

        if (onset != RADIO_ONSET_NONE) {
            if (result.count == 0u) {
                result.first = onset;
                result.first_block = index + 1u;
            }
            ++result.count;
        }
    }
    return result;
}

static void init_default(void)
{
    RadioActivityConfig config;

    RadioActivity_DefaultConfig(&config);
    CHECK(RadioActivity_Init(&config));
}

static void mean_abs_covers_full_scale(void)
{
    static const int16_t samples[] = {-32768, 32767, 0, 1};

    CHECK(RadioActivity_MeanAbs(samples, 4u) == 16384u);
    CHECK(RadioActivity_MeanAbs(samples, 0u) == 0u);
    CHECK(RadioActivity_MeanAbs(NULL, 4u) == 0u);
}

static void config_is_validated(void)
{
    RadioActivityConfig config;

    CHECK(!RadioActivity_Init(NULL));
    RadioActivity_DefaultConfig(&config);
    config.slow_alpha_q16 = 0u;
    CHECK(!RadioActivity_Init(&config));
    RadioActivity_DefaultConfig(&config);
    config.fast_alpha_q16 = 65537u;
    CHECK(!RadioActivity_Init(&config));
    RadioActivity_DefaultConfig(&config);
    config.onset_ratio_q8[2] = config.onset_ratio_q8[1];
    CHECK(!RadioActivity_Init(&config));
    RadioActivity_DefaultConfig(&config);
    config.rearm_ratio_q8 = config.onset_ratio_q8[0];
    CHECK(!RadioActivity_Init(&config));
    RadioActivity_DefaultConfig(&config);
    config.level_floor = 0u;
    CHECK(!RadioActivity_Init(&config));
    RadioActivity_DefaultConfig(&config);
    config.max_hold_blocks = 0u;
    CHECK(!RadioActivity_Init(&config));
}

static void steady_sound_never_fires_however_loud(void)
{
    FeedResult result;

    init_default();
    result = feed(1000u, 400u, true);
    CHECK(result.count == 0u);
    init_default();
    result = feed(30000u, 400u, true);
    CHECK(result.count == 0u);
}

static void onset_size_is_the_highest_ratio_crossed(void)
{
    RadioActivityStatus status;
    FeedResult result;

    /* Doubling crosses 1.5x only; the hold bounds the wait. */
    init_default();
    (void)feed(1000u, 200u, true);
    result = feed(2000u, 50u, true);
    CHECK(result.count == 1u);
    CHECK(result.first == RADIO_ONSET_SMALL);
    CHECK(result.first_block <= 8u);

    /* Four times louder reaches 2.5x but not 4x: the slow average starts
     * rising at once, so a 3x step peaks near 2.47x and stays small. */
    init_default();
    (void)feed(1000u, 200u, true);
    result = feed(3000u, 50u, true);
    CHECK(result.count == 1u);
    CHECK(result.first == RADIO_ONSET_SMALL);
    init_default();
    (void)feed(1000u, 200u, true);
    result = feed(4000u, 50u, true);
    CHECK(result.count == 1u);
    CHECK(result.first == RADIO_ONSET_MEDIUM);

    /* Eight times louder passes medium on the first block, then reaches
     * large; it reports large once, not medium then large. */
    init_default();
    (void)feed(1000u, 200u, true);
    result = feed(8000u, 50u, true);
    CHECK(result.count == 1u);
    CHECK(result.first == RADIO_ONSET_LARGE);
    CHECK(result.first_block <= 4u);
    CHECK(RadioActivity_GetStatus(&status));
    CHECK(status.onsets[0] == 0u);
    CHECK(status.onsets[1] == 0u);
    CHECK(status.onsets[2] == 1u);
}

static void short_burst_reports_its_peak(void)
{
    FeedResult result;

    /* A two-block burst: the ratio peaks and falls, and the onset reports
     * the level reached at the peak. */
    init_default();
    (void)feed(1000u, 200u, true);
    result = feed(6000u, 2u, true);
    CHECK(result.count == 0u);
    result = feed(1000u, 10u, true);
    CHECK(result.count == 1u);
    CHECK(result.first == RADIO_ONSET_MEDIUM);
}

static void rearms_only_below_one_and_a_quarter(void)
{
    RadioActivityStatus status;
    FeedResult result;

    init_default();
    (void)feed(1000u, 200u, true);
    result = feed(3000u, 50u, true);
    CHECK(result.count == 1u);
    CHECK(RadioActivity_GetStatus(&status));
    CHECK(!status.armed);
    /* Below the small threshold but above 1.25x: still not re-armed. */
    do {
        result = feed(3000u, 1u, true);
        CHECK(result.count == 0u);
        CHECK(RadioActivity_GetStatus(&status));
    } while ((status.ratio_q8 >= 384u) && (status.measured_blocks < 1000u));
    CHECK(status.ratio_q8 >= 320u);
    CHECK(!status.armed);
    /* Still loud: no repeat while the slow average catches up (the ratio
     * reaches 1.25x after about 113 blocks, 1.2 s). */
    result = feed(3000u, 150u, true);
    CHECK(result.count == 0u);
    CHECK(RadioActivity_GetStatus(&status));
    CHECK(status.ratio_q8 < 320u);
    CHECK(status.armed);
    /* Re-armed: a further rise fires again. */
    (void)feed(3000u, 300u, true);
    result = feed(9000u, 50u, true);
    CHECK(result.count == 1u);
}

static void unmeasured_blocks_hold_averages_and_drop_pending(void)
{
    RadioActivityStatus before;
    RadioActivityStatus after;
    FeedResult result;

    /* Receiver mute while tuning: zeros are not fed into the averages. */
    init_default();
    (void)feed(1000u, 200u, true);
    CHECK(RadioActivity_GetStatus(&before));
    result = feed(0u, 11u, false);
    CHECK(result.count == 0u);
    CHECK(RadioActivity_GetStatus(&after));
    CHECK(after.fast_q8 == before.fast_q8);
    CHECK(after.slow_q8 == before.slow_q8);
    CHECK(after.unmeasured_blocks == before.unmeasured_blocks + 11u);
    result = feed(1000u, 50u, true);
    CHECK(result.count == 0u);

    /* An onset still being measured when the radio stops never fires. */
    init_default();
    (void)feed(1000u, 200u, true);
    result = feed(6000u, 1u, true);
    CHECK(result.count == 0u);
    CHECK(RadioActivity_GetStatus(&after));
    CHECK(after.pending > 0u);
    result = feed(6000u, 5u, false);
    CHECK(result.count == 0u);
    CHECK(RadioActivity_GetStatus(&after));
    CHECK(after.pending == 0u);
}

static void reset_reseeds_without_an_onset(void)
{
    RadioActivityStatus status;
    FeedResult result;

    init_default();
    (void)feed(100u, 200u, true);
    RadioActivity_Reset();
    CHECK(RadioActivity_GetStatus(&status));
    CHECK(!status.seeded);
    result = feed(20000u, 100u, true);
    CHECK(result.count == 0u);
}

static void floor_keeps_near_silence_quiet(void)
{
    RadioActivityConfig config;
    FeedResult result;

    init_default();
    (void)feed(0u, 200u, true);
    result = feed(20u, 50u, true);
    CHECK(result.count == 0u);

    /* The same input with the floor at its minimum does fire, which is what
     * the floor exists to prevent. */
    RadioActivity_DefaultConfig(&config);
    config.level_floor = 1u;
    CHECK(RadioActivity_Init(&config));
    (void)feed(0u, 200u, true);
    result = feed(20u, 50u, true);
    CHECK(result.count == 1u);
}

int main(void)
{
    mean_abs_covers_full_scale();
    config_is_validated();
    steady_sound_never_fires_however_loud();
    onset_size_is_the_highest_ratio_crossed();
    short_burst_reports_its_peak();
    rearms_only_below_one_and_a_quarter();
    unmeasured_blocks_hold_averages_and_drop_pending();
    reset_reseeds_without_an_onset();
    floor_keeps_near_silence_quiet();
    if (failures != 0u) {
        printf("%u failure(s)\n", failures);
        return 1;
    }
    printf("radio_activity_test: pass\n");
    return 0;
}
