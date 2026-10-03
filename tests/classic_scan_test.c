#include "classic_scan.h"

#include <stdio.h>
#include <string.h>

static unsigned failures;

#define CHECK(condition) do {                                               \
    if (!(condition)) {                                                     \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);       \
        ++failures;                                                         \
    }                                                                       \
} while (0)

enum { FM = 0, AM = 1, SW = 2, LW = 3 };

/* The receiver's band table (radio_control_service.c). */
static const ClassicTerritory bands[CLASSIC_SCAN_BAND_COUNT] = {
    {87500u, 108000u, 100u},
    {520u, 1710u, 10u},
    {2300u, 23000u, 5u},
    {153u, 279u, 9u},
};

static const char *const band_names[CLASSIC_SCAN_BAND_COUNT] = {"FM", "AM", "SW", "LW"};

/* A receiver that lands each tune after tune_ms. With measure set, the radio
 * activity measurement is valid whenever no tune is in flight. */
typedef struct {
    ClassicScanInput in;
    bool measure;
    uint32_t now;
    uint32_t tune_ms;
    uint32_t tune_end;
    uint32_t pending_khz;
    uint32_t service_every_ms;
    unsigned jumps;
    unsigned jumps_while_in_flight;
    uint32_t last_jump_ms;
} Sim;

static void init_default(void)
{
    ClassicScanConfig config;

    ClassicScan_DefaultConfig(&config);
    CHECK(ClassicScan_Init(&config, bands));
}

static void sim_start(Sim *sim, uint8_t band, uint16_t channel)
{
    memset(sim, 0, sizeof(*sim));
    sim->in.band = band;
    sim->in.frequency_khz = ClassicScan_ChannelKhz(&bands[band], channel);
    sim->in.tuning_valid = true;
    sim->in.active = true;
    sim->in.can_tune = true;
    sim->service_every_ms = 1u;
}

static uint16_t sim_channel(const Sim *sim)
{
    return ClassicScan_NearestChannel(&bands[sim->in.band], sim->in.frequency_khz);
}

/* One millisecond: the receiver finishes a due tune, then the foreground
 * loop runs one service pass. Returns true when the engine jumped. */
static bool sim_tick(Sim *sim, ClassicScanJump *jump)
{
    ClassicScanJump local;
    ClassicScanJump *out = (jump != NULL) ? jump : &local;
    bool jumped = false;

    if (sim->in.tune_in_flight && ((int32_t)(sim->now - sim->tune_end) >= 0)) {
        sim->in.frequency_khz = sim->pending_khz;
        sim->in.tune_in_flight = false;
    }
    sim->in.activity_valid = sim->measure && !sim->in.tune_in_flight;
    if ((sim->now % sim->service_every_ms) == 0u &&
        ClassicScan_Service(sim->now, &sim->in, out)) {
        jumped = true;
        if (sim->in.tune_in_flight) {
            ++sim->jumps_while_in_flight;
        }
        ++sim->jumps;
        sim->last_jump_ms = sim->now;
        if (out->tune) {
            if (sim->tune_ms == 0u) {
                sim->in.frequency_khz = out->frequency_khz;
            } else {
                sim->in.tune_in_flight = true;
                sim->pending_khz = out->frequency_khz;
                sim->tune_end = sim->now + sim->tune_ms;
            }
        }
    }
    sim->in.onset = 0u; /* an onset is reported to one pass */
    ++sim->now;
    return jumped;
}

static bool next_jump(Sim *sim, ClassicScanJump *jump, uint32_t limit_ms)
{
    const uint32_t start = sim->now;

    while ((sim->now - start) <= limit_ms) {
        if (sim_tick(sim, jump)) {
            return true;
        }
    }
    return false;
}

static unsigned run_for(Sim *sim, uint32_t duration_ms)
{
    const unsigned before = sim->jumps;
    const uint32_t end = sim->now + duration_ms;

    while (sim->now != end) {
        (void)sim_tick(sim, NULL);
    }
    return sim->jumps - before;
}

static ClassicScanStatus status_of(const Sim *sim)
{
    ClassicScanStatus status;

    memset(&status, 0, sizeof(status));
    CHECK(ClassicScan_GetStatus(sim->in.band, sim->in.frequency_khz, &status));
    return status;
}

static void set_distance(uint8_t band, uint16_t channels)
{
    ClassicScanStatus status;
    unsigned guard = 0u;

    (void)ClassicScan_StepDistance(band, -255);
    CHECK(ClassicScan_GetStatus(band, bands[band].minimum_khz, &status));
    while ((status.distance_channels < channels) && (guard++ < 64u)) {
        CHECK(ClassicScan_StepDistance(band, 1));
        CHECK(ClassicScan_GetStatus(band, bands[band].minimum_khz, &status));
    }
    CHECK(status.distance_channels == channels);
}

static void set_rate(uint16_t per_min)
{
    ClassicScanStatus status;
    unsigned guard = 0u;

    (void)ClassicScan_StepRate(-255);
    CHECK(ClassicScan_GetStatus(FM, bands[FM].minimum_khz, &status));
    while ((status.rate_setting_per_min < per_min) && (guard++ < 32u)) {
        CHECK(ClassicScan_StepRate(1));
        CHECK(ClassicScan_GetStatus(FM, bands[FM].minimum_khz, &status));
    }
    CHECK(status.rate_setting_per_min == per_min);
}

static void set_edge(ClassicEdge edge)
{
    (void)ClassicScan_StepEdge(-255);
    (void)ClassicScan_StepEdge((int32_t)edge);
}

static void set_hold(uint16_t seconds)
{
    ClassicScanStatus status;

    (void)ClassicScan_StepHoldTime(-255);
    CHECK(ClassicScan_GetStatus(FM, bands[FM].minimum_khz, &status));
    while (status.hold_seconds < seconds) {
        CHECK(ClassicScan_StepHoldTime(1));
        CHECK(ClassicScan_GetStatus(FM, bands[FM].minimum_khz, &status));
    }
    CHECK(status.hold_seconds == seconds);
}

/* Runs duration_ms with an onset of `size` at once and then every every_ms;
 * every_ms 0 reports none. Returns the jumps made. */
static unsigned run_onsets(Sim *sim, uint32_t duration_ms, uint32_t every_ms,
                           uint8_t size)
{
    const unsigned before = sim->jumps;
    uint32_t elapsed;

    for (elapsed = 0u; elapsed < duration_ms; ++elapsed) {
        if ((every_ms != 0u) && ((elapsed % every_ms) == 0u)) {
            sim->in.onset = size;
        }
        (void)sim_tick(sim, NULL);
    }
    return sim->jumps - before;
}

/* One tick with an onset of `size`. Returns true when the engine jumped. */
static bool onset_tick(Sim *sim, uint8_t size, ClassicScanJump *jump)
{
    sim->in.onset = size;
    return sim_tick(sim, jump);
}

static bool on_raster(uint8_t band, uint32_t khz)
{
    return (khz >= bands[band].minimum_khz) && (khz <= bands[band].maximum_khz) &&
           (((khz - bands[band].minimum_khz) % bands[band].step_khz) == 0u);
}

/* --- Configuration and arithmetic (decision 0016 items 1, 4, 5, 9, 10, 22) --- */

static void channel_arithmetic_follows_the_band_table(void)
{
    static const uint16_t counts[CLASSIC_SCAN_BAND_COUNT] = {206u, 120u, 4141u, 15u};
    uint8_t band;

    for (band = 0u; band < CLASSIC_SCAN_BAND_COUNT; ++band) {
        const uint16_t count = ClassicScan_ChannelCount(&bands[band]);

        CHECK(count == counts[band]);
        CHECK(ClassicScan_ChannelKhz(&bands[band], 0u) == bands[band].minimum_khz);
        CHECK(ClassicScan_ChannelKhz(&bands[band], (uint16_t)(count - 1u)) ==
              bands[band].maximum_khz);
        CHECK(ClassicScan_NearestChannel(&bands[band], bands[band].minimum_khz) == 0u);
        CHECK(ClassicScan_NearestChannel(&bands[band], bands[band].maximum_khz) ==
              (uint16_t)(count - 1u));
        /* Out of band clamps to the nearest edge. */
        CHECK(ClassicScan_NearestChannel(&bands[band], 0u) == 0u);
        CHECK(ClassicScan_NearestChannel(&bands[band], 0xFFFFFFFFu) == (uint16_t)(count - 1u));
    }
    /* Between channels: the nearest one, half a step rounding up. */
    CHECK(ClassicScan_NearestChannel(&bands[FM], 99100u) == 116u);
    CHECK(ClassicScan_NearestChannel(&bands[FM], 99140u) == 116u);
    CHECK(ClassicScan_NearestChannel(&bands[FM], 99150u) == 117u);
    CHECK(ClassicScan_NearestChannel(&bands[LW], 157u) == 0u);
    CHECK(ClassicScan_NearestChannel(&bands[LW], 158u) == 1u);
}

static void startup_matches_decision_0016(void)
{
    ClassicScanConfig config;
    ClassicScanStatus status;
    Sim sim;

    ClassicScan_DefaultConfig(&config);
    CHECK(config.rate_count == 15u);
    CHECK(config.rate_per_min[0] == 6u);
    CHECK(config.rate_per_min[14] == 400u);
    CHECK(config.rate_per_min[config.default_rate_index] == 120u);
    CHECK(config.band_max_rate_per_min[FM] == 400u);
    CHECK(config.band_max_rate_per_min[AM] == 180u); /* decision 0018 */
    CHECK(config.band_max_rate_per_min[SW] == 240u);
    CHECK(config.band_max_rate_per_min[LW] == 180u);
    CHECK(config.distance_count == 21u);
    CHECK(config.distance_channels[20] == 2000u);
    CHECK(config.start_running && config.start_up);
    CHECK(config.start_edge == CLASSIC_EDGE_WRAP);
    /* Hold time 0 (off) to 30 s, trigger medium, release 1.5 s (items 16, 18, 19). */
    CHECK(config.hold_count == 10u);
    CHECK(config.hold_seconds[0] == 0u && config.hold_seconds[1] == 1u);
    CHECK(config.hold_seconds[5] == 8u && config.hold_seconds[9] == 30u);
    CHECK(config.default_hold_index == 0u);
    CHECK(config.hold_trigger_size == 2u);
    CHECK(config.hold_release_ms == 1500u);
    CHECK(ClassicScan_Init(&config, bands));

    /* FM at the radio's default frequency, 99.1 MHz. */
    sim_start(&sim, FM, ClassicScan_NearestChannel(&bands[FM], 99100u));
    status = status_of(&sim);
    CHECK(status.run_state == CLASSIC_RUN_RUNNING);
    CHECK(status.direction_up);
    CHECK(status.edge == CLASSIC_EDGE_WRAP);
    CHECK(status.rate_setting_per_min == 120u && status.rate_per_min == 120u);
    CHECK(!status.rate_limited);
    CHECK(status.distance_channels == 1u && status.distance_khz == 100u);
    CHECK(status.channel_index == 116u && status.channel_count == 206u);
    CHECK(status.hold_seconds == 0u);
    CHECK(ClassicScan_GetStatus(AM, 1000u, &status) && status.distance_khz == 10u);
    CHECK(ClassicScan_GetStatus(SW, 6000u, &status) && status.distance_channels == 20u &&
          status.distance_khz == 100u);
    CHECK(ClassicScan_GetStatus(LW, 198u, &status) && status.distance_khz == 9u);
}

static void config_is_validated(void)
{
    ClassicScanConfig config;
    ClassicTerritory territories[CLASSIC_SCAN_BAND_COUNT];

    memcpy(territories, bands, sizeof(territories));
    ClassicScan_DefaultConfig(&config);
    CHECK(!ClassicScan_Init(NULL, bands));
    CHECK(!ClassicScan_Init(&config, NULL));

    ClassicScan_DefaultConfig(&config);
    config.rate_count = 0u;
    CHECK(!ClassicScan_Init(&config, bands));
    ClassicScan_DefaultConfig(&config);
    config.default_rate_index = config.rate_count;
    CHECK(!ClassicScan_Init(&config, bands));
    ClassicScan_DefaultConfig(&config);
    config.rate_per_min[3] = config.rate_per_min[2];
    CHECK(!ClassicScan_Init(&config, bands));
    ClassicScan_DefaultConfig(&config);
    config.rate_per_min[0] = 0u;
    CHECK(!ClassicScan_Init(&config, bands));
    ClassicScan_DefaultConfig(&config);
    config.band_max_rate_per_min[LW] = 0u;
    CHECK(!ClassicScan_Init(&config, bands));
    ClassicScan_DefaultConfig(&config);
    config.distance_channels[5] = 1u;
    CHECK(!ClassicScan_Init(&config, bands));
    ClassicScan_DefaultConfig(&config);
    config.start_edge = CLASSIC_EDGE_COUNT;
    CHECK(!ClassicScan_Init(&config, bands));
    /* A default distance must be a value of its band's sequence. */
    ClassicScan_DefaultConfig(&config);
    config.default_distance[FM] = 6u;
    CHECK(!ClassicScan_Init(&config, bands));
    ClassicScan_DefaultConfig(&config);
    config.default_distance[LW] = 10u; /* above LW's cap of 7 */
    CHECK(!ClassicScan_Init(&config, bands));
    ClassicScan_DefaultConfig(&config);
    config.default_distance[LW] = 7u; /* the cap itself is in the sequence */
    CHECK(ClassicScan_Init(&config, bands));
    /* Territories: whole steps, at least two channels. */
    ClassicScan_DefaultConfig(&config);
    territories[AM].maximum_khz = 1715u;
    CHECK(!ClassicScan_Init(&config, territories));
    territories[AM] = bands[AM];
    territories[LW].maximum_khz = territories[LW].minimum_khz;
    CHECK(!ClassicScan_Init(&config, territories));
    territories[LW] = bands[LW];
    territories[SW].step_khz = 0u;
    CHECK(!ClassicScan_Init(&config, territories));
    territories[SW] = bands[SW];
    CHECK(ClassicScan_Init(&config, territories));
    /* Hold table: 0 first, then increasing; trigger 1 to 3; a release time. */
    ClassicScan_DefaultConfig(&config);
    config.hold_seconds[0] = 1u;
    CHECK(!ClassicScan_Init(&config, bands));
    ClassicScan_DefaultConfig(&config);
    config.hold_seconds[4] = config.hold_seconds[3];
    CHECK(!ClassicScan_Init(&config, bands));
    ClassicScan_DefaultConfig(&config);
    config.hold_count = 1u;
    CHECK(!ClassicScan_Init(&config, bands));
    ClassicScan_DefaultConfig(&config);
    config.hold_count = CLASSIC_SCAN_HOLD_STEPS_MAX + 1u;
    CHECK(!ClassicScan_Init(&config, bands));
    ClassicScan_DefaultConfig(&config);
    config.default_hold_index = config.hold_count;
    CHECK(!ClassicScan_Init(&config, bands));
    ClassicScan_DefaultConfig(&config);
    config.hold_trigger_size = 0u;
    CHECK(!ClassicScan_Init(&config, bands));
    ClassicScan_DefaultConfig(&config);
    config.hold_trigger_size = 4u;
    CHECK(!ClassicScan_Init(&config, bands));
    ClassicScan_DefaultConfig(&config);
    config.hold_release_ms = 0u;
    CHECK(!ClassicScan_Init(&config, bands));
    ClassicScan_DefaultConfig(&config);
    config.hold_trigger_size = 3u;
    CHECK(ClassicScan_Init(&config, bands));
}

static void controls_stop_at_both_ends(void)
{
    ClassicScanStatus status;

    init_default();
    CHECK(ClassicScan_StepRate(-3));
    CHECK(ClassicScan_GetStatus(FM, 99100u, &status) && status.rate_setting_per_min == 60u);
    CHECK(ClassicScan_StepRate(-100));
    CHECK(!ClassicScan_StepRate(-1));
    CHECK(ClassicScan_GetStatus(FM, 99100u, &status) && status.rate_setting_per_min == 6u);
    CHECK(ClassicScan_StepRate(1000));
    CHECK(!ClassicScan_StepRate(1));
    CHECK(ClassicScan_GetStatus(FM, 99100u, &status) && status.rate_setting_per_min == 400u);
    CHECK(ClassicScan_StepRate(-2147483647 - 1));
    CHECK(ClassicScan_GetStatus(FM, 99100u, &status) && status.rate_setting_per_min == 6u);

    CHECK(!ClassicScan_StepEdge(-1));
    CHECK(ClassicScan_StepEdge(1));
    CHECK(ClassicScan_GetStatus(FM, 99100u, &status) && status.edge == CLASSIC_EDGE_BOUNCE);
    CHECK(ClassicScan_StepEdge(5));
    CHECK(!ClassicScan_StepEdge(1));
    CHECK(ClassicScan_GetStatus(FM, 99100u, &status) && status.edge == CLASSIC_EDGE_STOP);
    CHECK(!ClassicScan_StepDistance(CLASSIC_SCAN_BAND_COUNT, 1));
}

static void distance_sequence_is_cut_at_half_the_band(void)
{
    static const uint16_t caps[CLASSIC_SCAN_BAND_COUNT] = {103u, 60u, 2070u, 7u};
    static const uint16_t fm[] = {1u, 2u, 3u, 4u, 5u, 7u, 10u, 15u, 20u, 30u, 50u, 70u, 100u, 103u};
    static const uint16_t lw[] = {1u, 2u, 3u, 4u, 5u, 7u};
    ClassicScanStatus status;
    uint8_t band;
    size_t index;

    init_default();
    for (band = 0u; band < CLASSIC_SCAN_BAND_COUNT; ++band) {
        CHECK(ClassicScan_StepDistance(band, 1000));
        CHECK(!ClassicScan_StepDistance(band, 1));
        CHECK(ClassicScan_GetStatus(band, bands[band].minimum_khz, &status));
        CHECK(status.distance_channels == caps[band]);
        CHECK(status.distance_khz == (uint32_t)caps[band] * bands[band].step_khz);
        /* From the cap, one detent down is the last table value below it. */
        CHECK(ClassicScan_StepDistance(band, -1));
        CHECK(ClassicScan_GetStatus(band, bands[band].minimum_khz, &status));
        CHECK(status.distance_channels < caps[band]);
    }
    (void)ClassicScan_StepDistance(FM, -1000);
    for (index = 0u; index < (sizeof(fm) / sizeof(fm[0])); ++index) {
        CHECK(ClassicScan_GetStatus(FM, 99100u, &status) && status.distance_channels == fm[index]);
        (void)ClassicScan_StepDistance(FM, 1);
    }
    (void)ClassicScan_StepDistance(LW, -1000);
    for (index = 0u; index < (sizeof(lw) / sizeof(lw[0])); ++index) {
        CHECK(ClassicScan_GetStatus(LW, 198u, &status) && status.distance_channels == lw[index]);
        (void)ClassicScan_StepDistance(LW, 1);
    }
    /* SW reaches 2000 and then its cap of 2070. */
    (void)ClassicScan_StepDistance(SW, 1000);
    CHECK(ClassicScan_StepDistance(SW, -1));
    CHECK(ClassicScan_GetStatus(SW, 6000u, &status) && status.distance_channels == 2000u);
}

/* --- Landings (SC-002, items 12 to 15) --- */

static void one_jump(Sim *sim, ClassicScanJump *jump)
{
    CHECK(next_jump(sim, jump, 20000u));
}

/* Every band, every edge mode, both directions, distance 1: one sweep lands
 * on every channel exactly once, on the raster and inside the band. */
static void a_sweep_lands_on_every_channel_once(void)
{
    static uint8_t visits[4141];
    uint8_t band;
    int edge;
    int up;

    for (band = 0u; band < CLASSIC_SCAN_BAND_COUNT; ++band) {
        const uint16_t count = ClassicScan_ChannelCount(&bands[band]);
        const uint16_t last = (uint16_t)(count - 1u);

        for (edge = 0; edge < (int)CLASSIC_EDGE_COUNT; ++edge) {
            for (up = 0; up <= 1; ++up) {
                ClassicScanJump jump;
                Sim sim;
                unsigned landings;
                unsigned bad = 0u;
                unsigned index;

                init_default();
                set_rate(400u);
                set_edge((ClassicEdge)edge);
                set_distance(band, 1u); /* SW starts at 20 */
                if (!up) {
                    ClassicScan_ToggleDirection();
                }
                memset(visits, 0, sizeof(visits));
                if (edge == (int)CLASSIC_EDGE_STOP) {
                    /* Paused on the far edge: resuming starts the sweep. */
                    sim_start(&sim, band, up ? last : 0u);
                    ClassicScan_ToggleRun();
                    CHECK(status_of(&sim).run_state == CLASSIC_RUN_SWEEP_COMPLETE);
                    ClassicScan_ToggleRun();
                    landings = count;
                } else {
                    /* Wrap: N jumps return to the start. Bounce: one pass
                     * from edge to edge; the start edge ended the last pass. */
                    sim_start(&sim, band, up ? 0u : last);
                    landings = count;
                    if (edge == (int)CLASSIC_EDGE_BOUNCE) {
                        visits[up ? 0u : last] = 1u;
                        landings = last;
                    }
                }
                for (index = 0u; index < landings; ++index) {
                    one_jump(&sim, &jump);
                    if (!jump.tune || !on_raster(band, jump.frequency_khz) ||
                        jump.direction_changed || jump.sweep_complete ||
                        (jump.channel_index >= count)) {
                        ++bad;
                    } else {
                        ++visits[jump.channel_index];
                    }
                }
                for (index = 0u; index < count; ++index) {
                    if (visits[index] != 1u) {
                        ++bad;
                    }
                }
                if (bad != 0u) {
                    printf("FAIL sweep %s edge %d %s: %u bad\n",
                           band_names[band], edge, up ? "up" : "down", bad);
                    ++failures;
                }
                /* What follows the sweep. */
                one_jump(&sim, &jump);
                if (edge == (int)CLASSIC_EDGE_WRAP) {
                    CHECK(jump.channel_index == (up ? 1u : (uint16_t)(last - 1u)));
                } else if (edge == (int)CLASSIC_EDGE_BOUNCE) {
                    CHECK(jump.direction_changed);
                    CHECK(jump.channel_index == (up ? (uint16_t)(last - 1u) : 1u));
                    CHECK(status_of(&sim).direction_up == !up);
                } else {
                    /* Already on the edge: complete without retuning. */
                    CHECK(jump.sweep_complete && !jump.tune);
                    CHECK(status_of(&sim).run_state == CLASSIC_RUN_SWEEP_COMPLETE);
                    CHECK(run_for(&sim, 60000u) == 0u);
                }
            }
        }
    }
}

/* One scheduled jump from every start channel (sampled on SW), for every
 * distance in every mode and direction: the landing follows the decision
 * 0016 formula and stays on the band's raster. */
static void every_jump_follows_the_edge_formulas(void)
{
    uint8_t band;
    int edge;
    int up;

    for (band = 0u; band < CLASSIC_SCAN_BAND_COUNT; ++band) {
        const int32_t count = (int32_t)ClassicScan_ChannelCount(&bands[band]);
        const int32_t last = count - 1;
        const int32_t stride = (band == SW) ? 37 : 1;

        for (edge = 0; edge < (int)CLASSIC_EDGE_COUNT; ++edge) {
            for (up = 0; up <= 1; ++up) {
                unsigned bad = 0u;
                uint16_t previous_distance = 0u;
                int32_t position;

                for (position = 0; position < 64; ++position) {
                    ClassicScanStatus status;
                    int32_t from;
                    int32_t d;

                    init_default();
                    (void)ClassicScan_StepDistance(band, -1000);
                    (void)ClassicScan_StepDistance(band, position);
                    CHECK(ClassicScan_GetStatus(band, bands[band].minimum_khz, &status));
                    if (status.distance_channels == previous_distance) {
                        break; /* past the cap */
                    }
                    previous_distance = status.distance_channels;
                    d = (int32_t)status.distance_channels;
                    for (from = 0; from < count; from += stride) {
                        const int32_t target = up ? from + d : from - d;
                        int32_t expected = target;
                        bool reverse = false;
                        bool complete = false;
                        ClassicScanInput input;
                        ClassicScanJump jump;

                        if (edge == (int)CLASSIC_EDGE_WRAP) {
                            expected = (target + count) % count;
                        } else if (edge == (int)CLASSIC_EDGE_BOUNCE) {
                            if (target > last) {
                                expected = (2 * last) - target;
                                reverse = true;
                            } else if (target < 0) {
                                expected = -target;
                                reverse = true;
                            }
                        } else if ((target > last) || (target < 0)) {
                            expected = (target > last) ? last : 0;
                            complete = true;
                        }

                        /* Fresh engine: running, the first jump due one
                         * period after the first pass (no resume rule). */
                        init_default();
                        set_edge((ClassicEdge)edge);
                        if (!up) {
                            ClassicScan_ToggleDirection();
                        }
                        (void)ClassicScan_StepDistance(band, -1000);
                        (void)ClassicScan_StepDistance(band, position);
                        memset(&input, 0, sizeof(input));
                        input.band = band;
                        input.frequency_khz = ClassicScan_ChannelKhz(&bands[band], (uint16_t)from);
                        input.tuning_valid = true;
                        input.active = true;
                        input.can_tune = true;
                        if (ClassicScan_Service(0u, &input, &jump) ||
                            !ClassicScan_Service(500u, &input, &jump) ||
                            ((int32_t)jump.channel_index != expected) ||
                            (jump.direction_changed != reverse) ||
                            (jump.sweep_complete != complete) ||
                            (jump.tune != (expected != from)) ||
                            !on_raster(band, jump.frequency_khz) ||
                            (jump.frequency_khz !=
                             ClassicScan_ChannelKhz(&bands[band], (uint16_t)expected))) {
                            ++bad;
                        }
                    }
                }
                if (bad != 0u) {
                    printf("FAIL formulas %s edge %d %s: %u bad\n",
                           band_names[band], edge, up ? "up" : "down", bad);
                    ++failures;
                }
            }
        }
    }
}

static void wrap_and_bounce_carry_the_remainder(void)
{
    ClassicScanJump jump;
    Sim sim;

    /* Wrap: 200 + 10 on FM's 206 channels lands on 4. */
    init_default();
    set_distance(FM, 10u);
    sim_start(&sim, FM, 200u);
    one_jump(&sim, &jump);
    CHECK(jump.channel_index == 4u && !jump.direction_changed);
    CHECK(sim.jumps == 1u && sim.last_jump_ms == 500u);

    /* Bounce: 203 + 5 reflects off 205 to 202, and the direction is down. */
    init_default();
    set_edge(CLASSIC_EDGE_BOUNCE);
    set_distance(FM, 5u);
    sim_start(&sim, FM, 203u);
    one_jump(&sim, &jump);
    CHECK(jump.channel_index == 202u && jump.direction_changed);
    CHECK(!status_of(&sim).direction_up);
    one_jump(&sim, &jump);
    CHECK(jump.channel_index == 197u && !jump.direction_changed);
    /* Going down: 2 - 5 reflects off 0 to 3, and the direction is up. */
    sim.in.frequency_khz = ClassicScan_ChannelKhz(&bands[FM], 2u);
    one_jump(&sim, &jump);
    CHECK(jump.channel_index == 3u && jump.direction_changed);
    CHECK(status_of(&sim).direction_up);
}

/* User Story 2 scenarios 3 to 6 and the edge cases. */
static void stop_ends_the_sweep_on_the_edge(void)
{
    ClassicScanJump jump;
    Sim sim;
    int edge;

    init_default();
    set_edge(CLASSIC_EDGE_STOP);
    set_distance(FM, 5u);
    sim_start(&sim, FM, 203u);
    one_jump(&sim, &jump);
    CHECK(jump.channel_index == 205u && jump.tune && jump.sweep_complete);
    CHECK(status_of(&sim).run_state == CLASSIC_RUN_SWEEP_COMPLETE);
    CHECK(run_for(&sim, 10000u) == 0u);

    /* Resuming with direction up starts a new sweep from the lower edge, at once. */
    ClassicScan_ToggleRun();
    CHECK(sim_tick(&sim, &jump));
    CHECK(jump.channel_index == 0u && jump.tune && !jump.sweep_complete);
    CHECK(status_of(&sim).run_state == CLASSIC_RUN_RUNNING);
    one_jump(&sim, &jump);
    CHECK(jump.channel_index == 5u);

    /* Reversing at a completed sweep sweeps down from the upper edge. */
    sim.in.frequency_khz = ClassicScan_ChannelKhz(&bands[FM], 203u);
    one_jump(&sim, &jump);
    CHECK(jump.sweep_complete && jump.channel_index == 205u);
    ClassicScan_ToggleDirection();
    CHECK(status_of(&sim).run_state == CLASSIC_RUN_PAUSED);
    ClassicScan_ToggleRun();
    CHECK(sim_tick(&sim, &jump));
    CHECK(jump.channel_index == 200u && !jump.sweep_complete);

    /* Changing the edge behavior at a completed sweep: the resume still starts
     * a new sweep, and the new mode applies to the jumps after it. */
    for (edge = 0; edge < (int)CLASSIC_EDGE_COUNT; ++edge) {
        init_default();
        set_edge(CLASSIC_EDGE_STOP);
        set_distance(FM, 5u);
        sim_start(&sim, FM, 203u);
        one_jump(&sim, &jump);
        CHECK(jump.sweep_complete);
        set_edge((ClassicEdge)edge);
        ClassicScan_ToggleRun();
        CHECK(sim_tick(&sim, &jump) && jump.channel_index == 0u);
        sim.in.frequency_khz = ClassicScan_ChannelKhz(&bands[FM], 203u);
        one_jump(&sim, &jump);
        /* 203 + 5: wrap lands on 2, bounce reflects to 202, stop ends on 205. */
        CHECK(jump.channel_index == ((edge == (int)CLASSIC_EDGE_WRAP) ? 2u :
                                     (edge == (int)CLASSIC_EDGE_BOUNCE) ? 202u : 205u));
        CHECK(jump.direction_changed == (edge == (int)CLASSIC_EDGE_BOUNCE));
        CHECK(jump.sweep_complete == (edge == (int)CLASSIC_EDGE_STOP));
    }
}

static void a_resume_mid_band_continues_in_every_mode(void)
{
    ClassicScanJump jump;
    Sim sim;
    int edge;

    for (edge = 0; edge < (int)CLASSIC_EDGE_COUNT; ++edge) {
        init_default();
        set_edge((ClassicEdge)edge);
        set_distance(FM, 3u);
        sim_start(&sim, FM, 100u);
        one_jump(&sim, &jump);
        CHECK(jump.channel_index == 103u);
        (void)run_for(&sim, 100u);
        ClassicScan_ToggleRun();
        CHECK(status_of(&sim).run_state == CLASSIC_RUN_PAUSED);
        CHECK(run_for(&sim, 5000u) == 0u);
        ClassicScan_ToggleRun();
        CHECK(sim_tick(&sim, &jump));
        CHECK(jump.channel_index == 106u);

        /* A manual pause on an edge pointing out resumes like a completed
         * sweep, in every mode (edge cases; item 15). */
        ClassicScan_ToggleRun();
        sim.in.frequency_khz = bands[FM].maximum_khz;
        CHECK(status_of(&sim).run_state == CLASSIC_RUN_SWEEP_COMPLETE);
        ClassicScan_ToggleRun();
        CHECK(sim_tick(&sim, &jump));
        CHECK(jump.channel_index == 0u && !jump.direction_changed && !jump.sweep_complete);
    }
}

/* --- Schedule (SC-003, SC-005, items 6 to 8, 21) --- */

/* Five simulated minutes at every rate on every band: the k-th jump comes
 * exactly at k periods, so the count is exact and nothing drifts. */
static void every_rate_keeps_its_count_over_five_minutes(void)
{
    ClassicScanConfig config;
    uint8_t band;
    uint8_t rate_index;

    ClassicScan_DefaultConfig(&config);
    for (band = 0u; band < CLASSIC_SCAN_BAND_COUNT; ++band) {
        for (rate_index = 0u; rate_index < config.rate_count; ++rate_index) {
            ClassicScanStatus status;
            Sim sim;
            unsigned bad = 0u;
            unsigned expected;

            init_default();
            set_rate(config.rate_per_min[rate_index]);
            sim_start(&sim, band, 0u);
            sim.tune_ms = 100u; /* under every band's shortest period */
            status = status_of(&sim);
            expected = 5u * status.rate_per_min;
            while (sim.now <= 300000u) {
                if (sim_tick(&sim, NULL) &&
                    (sim.last_jump_ms !=
                     (uint32_t)(((uint64_t)sim.jumps * 60000u) / status.rate_per_min))) {
                    ++bad;
                }
            }
            if (bad != 0u || sim.jumps != expected || sim.jumps_while_in_flight != 0u) {
                printf("FAIL rate %s %u/min: %u jumps, expected %u, %u off time\n",
                       band_names[band], status.rate_per_min, sim.jumps, expected, bad);
                ++failures;
            }
        }
    }
}

/* Serviced only every 7 ms: jumps are at most 6 ms late, keep their grid,
 * and the count stays within 1%. */
static void a_coarse_loop_does_not_drift(void)
{
    Sim sim;
    unsigned late = 0u;

    init_default();
    set_rate(180u); /* 333.33 ms */
    sim_start(&sim, FM, 0u);
    sim.tune_ms = 30u;
    sim.service_every_ms = 7u;
    while (sim.now <= 300000u) {
        if (sim_tick(&sim, NULL)) {
            const uint32_t ideal = (uint32_t)(((uint64_t)sim.jumps * 60000u) / 180u);

            if ((sim.last_jump_ms < ideal) || (sim.last_jump_ms - ideal) > 6u) {
                ++late;
            }
        }
    }
    CHECK(late == 0u);
    CHECK((sim.jumps * 100u) >= (900u * 99u) && (sim.jumps * 100u) <= (900u * 101u));
}

/* Tunes that end after the next jump was due: that jump goes when the tune
 * ends, the schedule restarts from it, no burst, no skipped landing. */
static void late_tunes_cause_no_burst_and_no_skip(void)
{
    ClassicScanJump jump;
    Sim sim;
    uint16_t expected_channel = 1u;
    uint32_t previous_ms = 0u;
    unsigned index;
    unsigned bad = 0u;

    init_default(); /* 120 per minute, 500 ms */
    sim_start(&sim, FM, 0u);
    for (index = 0u; index < 300u; ++index) {
        bool was_late;

        /* Every fourth tune ends 200 ms after the next jump was due. */
        sim.tune_ms = ((index % 4u) == 3u) ? 700u : 40u;
        was_late = (index > 0u) && (((index - 1u) % 4u) == 3u);
        one_jump(&sim, &jump);
        if (jump.channel_index != expected_channel || sim.jumps_while_in_flight != 0u) {
            ++bad;
        }
        if (index > 0u) {
            const uint32_t gap = sim.last_jump_ms - previous_ms;

            /* After a late tune, the jump follows the landing in the same pass. */
            if (was_late ? (gap != 700u) : (gap != 500u)) {
                ++bad;
            }
        }
        previous_ms = sim.last_jump_ms;
        expected_channel = (uint16_t)((expected_channel + 1u) % 206u);
    }
    CHECK(bad == 0u);
}

/* The foreground loop stalls for more than a period: one jump at once, then
 * the schedule restarts from it rather than catching up. */
static void a_stalled_loop_does_not_catch_up(void)
{
    ClassicScanJump jump;
    Sim sim;

    init_default();
    sim_start(&sim, FM, 0u);
    one_jump(&sim, &jump);
    CHECK(sim.last_jump_ms == 500u);
    sim.now = 2100u; /* nothing serviced for 1.6 s */
    CHECK(sim_tick(&sim, &jump) && sim.last_jump_ms == 2100u);
    CHECK(run_for(&sim, 499u) == 0u);
    CHECK(sim_tick(&sim, &jump) && sim.last_jump_ms == 2600u);
}

static void a_pause_lands_before_the_next_jump(void)
{
    uint32_t offset;

    for (offset = 0u; offset < 500u; offset += 1u) {
        ClassicScanJump jump;
        Sim sim;

        init_default();
        sim_start(&sim, FM, 0u);
        one_jump(&sim, &jump);
        (void)run_for(&sim, offset);
        /* Including a pause in the very millisecond the next jump is due. */
        ClassicScan_ToggleRun();
        if (run_for(&sim, 5000u) != 0u) {
            printf("FAIL pause %u ms after a jump still jumped\n", (unsigned)offset);
            ++failures;
        }
        CHECK(sim_channel(&sim) == 1u);
    }
}

static void a_resume_jumps_at_once_but_waits_for_a_tune(void)
{
    ClassicScanJump jump;
    Sim sim;

    init_default();
    sim_start(&sim, FM, 10u);
    ClassicScan_ToggleRun();
    (void)run_for(&sim, 3000u);
    ClassicScan_ToggleRun();
    CHECK(sim_tick(&sim, &jump) && jump.channel_index == 11u);
    CHECK(sim.last_jump_ms == 3000u);
    CHECK(run_for(&sim, 499u) == 0u);
    CHECK(sim_tick(&sim, &jump) && sim.last_jump_ms == 3500u);

    /* A tune in flight (Manual's, or a slow one) delays the resume jump. */
    ClassicScan_ToggleRun();
    sim.in.tune_in_flight = true;
    sim.pending_khz = ClassicScan_ChannelKhz(&bands[FM], 50u);
    sim.tune_end = sim.now + 120u;
    ClassicScan_ToggleRun();
    CHECK(next_jump(&sim, &jump, 1000u));
    CHECK(jump.channel_index == 51u && sim.jumps_while_in_flight == 0u);
}

static void the_band_limit_caps_the_rate_but_keeps_the_setting(void)
{
    ClassicScanStatus status;
    ClassicScanJump jump;
    Sim sim;
    uint32_t previous;

    init_default();
    set_rate(400u);
    sim_start(&sim, AM, 0u);
    status = status_of(&sim);
    CHECK(status.rate_setting_per_min == 400u && status.rate_per_min == 180u);
    CHECK(status.rate_limited);
    one_jump(&sim, &jump);
    previous = sim.last_jump_ms;
    one_jump(&sim, &jump);
    CHECK(sim.last_jump_ms - previous == 333u);

    /* Same setting on FM runs at 400 and is not limited. */
    CHECK(ClassicScan_GetStatus(FM, 99100u, &status));
    CHECK(status.rate_per_min == 400u && !status.rate_limited);
    CHECK(ClassicScan_GetStatus(SW, 6000u, &status));
    CHECK(status.rate_per_min == 240u && status.rate_limited);
    sim.in.band = FM;
    sim.in.frequency_khz = 99100u;
    one_jump(&sim, &jump); /* due on AM's grid */
    previous = sim.last_jump_ms;
    one_jump(&sim, &jump);
    CHECK(sim.last_jump_ms - previous == 150u);
}

static void a_rate_change_applies_from_the_next_due_time(void)
{
    ClassicScanJump jump;
    Sim sim;
    uint32_t first;

    init_default(); /* 120 */
    sim_start(&sim, FM, 0u);
    one_jump(&sim, &jump);
    first = sim.last_jump_ms;
    (void)run_for(&sim, 100u);
    CHECK(ClassicScan_StepRate(-3)); /* 60 */
    one_jump(&sim, &jump);
    CHECK(sim.last_jump_ms - first == 500u);
    one_jump(&sim, &jump);
    CHECK(sim.last_jump_ms - first == 1500u);
}

static void direction_applies_to_the_next_jump(void)
{
    ClassicScanJump jump;
    Sim sim;

    init_default();
    sim_start(&sim, FM, 100u);
    one_jump(&sim, &jump);
    CHECK(jump.channel_index == 101u);
    ClassicScan_ToggleDirection();
    CHECK(!status_of(&sim).direction_up);
    one_jump(&sim, &jump);
    CHECK(jump.channel_index == 100u && !jump.direction_changed);
    CHECK(sim.last_jump_ms == 1000u);
}

static void startup_waits_for_the_first_tune_then_one_period(void)
{
    ClassicScanJump jump;
    Sim sim;

    init_default();
    sim_start(&sim, FM, 116u);
    sim.in.tuning_valid = false; /* the radio is still starting */
    CHECK(run_for(&sim, 2000u) == 0u);
    sim.in.tuning_valid = true;
    CHECK(next_jump(&sim, &jump, 1000u));
    CHECK(sim.last_jump_ms == 2500u && jump.channel_index == 117u);
}

static void no_jump_or_retry_while_tuning_is_not_possible(void)
{
    ClassicScanJump jump;
    Sim sim;

    init_default();
    sim_start(&sim, FM, 0u);
    one_jump(&sim, &jump);
    sim.in.can_tune = false; /* faulted, stopped or guarded by the session */
    CHECK(run_for(&sim, 10000u) == 0u);
    CHECK(status_of(&sim).run_state == CLASSIC_RUN_RUNNING);
    sim.in.can_tune = true;
    {
        const uint32_t back = sim.now;

        one_jump(&sim, &jump);
        CHECK(jump.channel_index == 2u && sim.last_jump_ms == back + 500u);
        CHECK(sim.jumps == 2u);
    }
}

/* --- Navigation and shared tuning (SC-004, FR-003, FR-009, FR-021 to FR-024) --- */

static int same_parameters(const ClassicScanStatus *a, const ClassicScanStatus *b)
{
    return (a->run_state == b->run_state) && (a->direction_up == b->direction_up) &&
           (a->edge == b->edge) && (a->rate_setting_per_min == b->rate_setting_per_min) &&
           (a->distance_channels == b->distance_channels);
}

static void each_band_remembers_its_distance(void)
{
    ClassicScanStatus status;
    ClassicScanJump jump;
    Sim sim;

    init_default();
    set_distance(FM, 10u);
    CHECK(ClassicScan_GetStatus(AM, 1000u, &status) && status.distance_channels == 1u);
    CHECK(ClassicScan_GetStatus(SW, 6000u, &status) && status.distance_channels == 20u);
    CHECK(ClassicScan_GetStatus(LW, 198u, &status) && status.distance_channels == 1u);
    set_distance(SW, 100u);

    /* The band menu selects AM at its last frequency: AM's distance applies. */
    sim_start(&sim, FM, 50u);
    one_jump(&sim, &jump);
    CHECK(jump.channel_index == 60u);
    sim.in.band = AM;
    sim.in.frequency_khz = 1000u;
    one_jump(&sim, &jump);
    CHECK(jump.frequency_khz == 1010u);
    CHECK(status_of(&sim).run_state == CLASSIC_RUN_RUNNING);
    sim.in.band = SW;
    sim.in.frequency_khz = 6000u;
    one_jump(&sim, &jump);
    CHECK(jump.frequency_khz == 6500u);
    sim.in.band = FM;
    sim.in.frequency_khz = ClassicScan_ChannelKhz(&bands[FM], 60u);
    one_jump(&sim, &jump);
    CHECK(jump.channel_index == 70u);
}

/* Manual, the engine menu and engine switches: Classic stops moving while
 * inactive and comes back unchanged, continuing from the shared tuning. */
static void navigation_round_trips_keep_classic(void)
{
    int paused;
    int retune;

    for (paused = 0; paused <= 1; ++paused) {
        for (retune = 0; retune <= 2; ++retune) {
            ClassicScanStatus before;
            ClassicScanStatus after;
            ClassicScanJump jump;
            Sim sim;

            init_default();
            set_rate(240u);
            set_edge(CLASSIC_EDGE_BOUNCE);
            set_distance(FM, 5u);
            set_distance(AM, 3u);
            ClassicScan_ToggleDirection();
            sim_start(&sim, FM, 100u);
            one_jump(&sim, &jump);
            CHECK(jump.channel_index == 95u);
            if (paused) {
                ClassicScan_ToggleRun();
            }
            before = status_of(&sim);

            /* Enter Manual: Classic stops moving at once. */
            sim.in.active = false;
            CHECK(run_for(&sim, 30000u) == 0u);
            if (retune == 1) {
                sim.in.frequency_khz = ClassicScan_ChannelKhz(&bands[FM], 40u);
            } else if (retune == 2) {
                sim.in.band = AM;
                sim.in.frequency_khz = 700u;
            }
            /* Leave Manual. */
            sim.in.active = true;
            after = status_of(&sim);
            if (retune == 2) {
                CHECK(after.distance_channels == 3u);
                after.distance_channels = before.distance_channels;
            }
            CHECK(same_parameters(&before, &after));
            if (paused) {
                CHECK(run_for(&sim, 30000u) == 0u);
                continue;
            }
            /* Continues from the shared tuning, one period after the return. */
            {
                const uint32_t back = sim.now;

                one_jump(&sim, &jump);
                /* AM limits 240 per minute to 180. */
                CHECK(sim.last_jump_ms == back + ((retune == 2) ? 333u : 250u));
            }
            if (retune == 0) {
                CHECK(jump.channel_index == 90u);
            } else if (retune == 1) {
                CHECK(jump.channel_index == 35u);
            } else {
                CHECK(jump.frequency_khz == 670u);
            }
        }
    }
}

/* Menus and the utility root leave Classic active: it keeps its rhythm. */
static void menus_do_not_interrupt_the_scan(void)
{
    Sim sim;

    init_default();
    sim_start(&sim, FM, 0u);
    CHECK(run_for(&sim, 10001u) == 20u);
}

/* Decision 0016 item 3: a completed sweep left at an edge returns paused when
 * the user tuned away from it. */
static void a_completed_sweep_tuned_away_returns_paused(void)
{
    ClassicScanJump jump;
    Sim sim;

    init_default();
    set_edge(CLASSIC_EDGE_STOP);
    sim_start(&sim, FM, 204u);
    one_jump(&sim, &jump);
    one_jump(&sim, &jump);
    CHECK(jump.sweep_complete);
    CHECK(status_of(&sim).run_state == CLASSIC_RUN_SWEEP_COMPLETE);
    sim.in.active = false;
    sim.in.frequency_khz = ClassicScan_ChannelKhz(&bands[FM], 150u);
    sim.in.active = true;
    CHECK(status_of(&sim).run_state == CLASSIC_RUN_PAUSED);
    CHECK(run_for(&sim, 5000u) == 0u);
    ClassicScan_ToggleRun();
    CHECK(sim_tick(&sim, &jump) && jump.channel_index == 151u);
}

/* --- Activity hold (decision 0016 items 16 to 20) ----------------------- */

/* A scan on FM channel 10 at 120 per minute with 60 ms tunes and a valid
 * measurement outside them. Returns once the first jump has landed, at
 * t = 601 on channel 11 with the next jump due at 1000. */
static void hold_start(Sim *sim, uint16_t hold_seconds)
{
    ClassicScanJump jump;

    init_default();
    set_hold(hold_seconds);
    sim_start(sim, FM, 10u);
    sim->measure = true;
    sim->tune_ms = 60u;
    CHECK(next_jump(sim, &jump, 1000u) && sim->last_jump_ms == 500u);
    (void)run_for(sim, 100u);
    CHECK(sim->now == 601u && sim_channel(sim) == 11u);
}

static ClassicRunState run_state_of(const Sim *sim)
{
    return status_of(sim).run_state;
}

static void the_hold_time_steps_and_stops_at_both_ends(void)
{
    ClassicScanStatus status;

    init_default();
    CHECK(!ClassicScan_StepHoldTime(-1));
    CHECK(ClassicScan_StepHoldTime(1));
    CHECK(ClassicScan_GetStatus(FM, 99100u, &status) && status.hold_seconds == 1u);
    CHECK(ClassicScan_StepHoldTime(4));
    CHECK(ClassicScan_GetStatus(FM, 99100u, &status) && status.hold_seconds == 8u);
    CHECK(ClassicScan_StepHoldTime(255));
    CHECK(ClassicScan_GetStatus(FM, 99100u, &status) && status.hold_seconds == 30u);
    CHECK(!ClassicScan_StepHoldTime(1));
    CHECK(ClassicScan_StepHoldTime(-1000));
    CHECK(ClassicScan_GetStatus(FM, 99100u, &status) && status.hold_seconds == 0u);
}

static void a_zero_hold_time_keeps_the_rhythm(void)
{
    Sim sim;

    hold_start(&sim, 0u);
    /* Large onsets every 50 ms: jumps stay every 500 ms (spec US5 scenario 4). */
    CHECK(run_onsets(&sim, 10000u, 50u, 3u) == 20u);
    CHECK(sim.last_jump_ms == 10500u);
    CHECK(run_state_of(&sim) == CLASSIC_RUN_RUNNING);
}

static void a_hold_starts_extends_and_releases(void)
{
    ClassicScanJump jump;
    Sim sim;

    hold_start(&sim, 5u);
    /* A small onset is below the trigger size. */
    CHECK(!onset_tick(&sim, 1u, NULL));
    CHECK(run_state_of(&sim) == CLASSIC_RUN_RUNNING);
    /* A medium one starts the hold at t = 602 (item 18). */
    CHECK(!onset_tick(&sim, 2u, NULL));
    CHECK(run_state_of(&sim) == CLASSIC_RUN_HOLDING);
    /* Onsets of any size no more than 1.5 s apart keep it past the jump due
     * at 1000: at 603, 1603 and 2603 (item 19). */
    CHECK(run_onsets(&sim, 3000u, 1000u, 1u) == 0u);
    CHECK(run_state_of(&sim) == CLASSIC_RUN_HOLDING);
    CHECK(sim_channel(&sim) == 11u);
    /* Released more than 1.5 s after the last onset; the jump goes at once
     * (spec US5 scenario 2). */
    CHECK(next_jump(&sim, &jump, 3000u));
    CHECK(sim.last_jump_ms == 4104u && jump.channel_index == 12u);
    CHECK(run_state_of(&sim) == CLASSIC_RUN_RUNNING);
    /* The schedule starts again from that jump. */
    CHECK(next_jump(&sim, &jump, 1000u) && sim.last_jump_ms == 4604u);
}

static void an_onset_one_release_time_later_still_extends(void)
{
    Sim sim;

    hold_start(&sim, 10u);
    CHECK(!onset_tick(&sim, 2u, NULL)); /* t = 601 */
    CHECK(run_for(&sim, 1499u) == 0u);  /* to 2100 */
    CHECK(!onset_tick(&sim, 1u, NULL)); /* 2101: exactly 1500 ms later */
    CHECK(run_state_of(&sim) == CLASSIC_RUN_HOLDING);
    CHECK(run_for(&sim, 1500u) == 0u);  /* to 3601 */
    CHECK(run_state_of(&sim) == CLASSIC_RUN_HOLDING);
    CHECK(sim_tick(&sim, NULL) && sim.last_jump_ms == 3602u);
}

static void the_hold_time_caps_a_hold(void)
{
    ClassicScanJump jump;
    Sim sim;

    hold_start(&sim, 2u);
    CHECK(!onset_tick(&sim, 2u, NULL)); /* hold from t = 601 */
    /* Activity continues: onsets every 200 ms until the hold time (item 19,
     * spec US5 scenario 3). */
    CHECK(run_onsets(&sim, 1999u, 200u, 3u) == 0u);
    CHECK(run_state_of(&sim) == CLASSIC_RUN_HOLDING);
    CHECK(onset_tick(&sim, 3u, &jump));
    CHECK(sim.last_jump_ms == 2601u && jump.channel_index == 12u);
    /* The next landing may hold again. */
    CHECK(run_for(&sim, 100u) == 0u);
    CHECK(sim_channel(&sim) == 12u);
    CHECK(!onset_tick(&sim, 2u, NULL));
    CHECK(run_state_of(&sim) == CLASSIC_RUN_HOLDING);
}

static void each_landing_holds_at_most_once(void)
{
    ClassicScanJump jump;
    Sim sim;

    init_default();
    set_hold(30u);
    set_rate(6u); /* one jump every 10 s */
    sim_start(&sim, FM, 10u);
    sim.measure = true;
    sim.tune_ms = 60u;
    CHECK(next_jump(&sim, &jump, 11000u) && sim.last_jump_ms == 10000u);
    (void)run_for(&sim, 100u);
    CHECK(!onset_tick(&sim, 2u, NULL)); /* hold from t = 10101 */
    CHECK(run_state_of(&sim) == CLASSIC_RUN_HOLDING);
    /* Released at 11602, long before the jump due at 20000: a hold never
     * shortens a dwell, so the jump keeps its due time. */
    CHECK(run_for(&sim, 1501u) == 0u);
    CHECK(run_state_of(&sim) == CLASSIC_RUN_RUNNING);
    /* More onsets on the same landing do not hold again (item 20). */
    CHECK(!onset_tick(&sim, 3u, NULL));
    CHECK(run_state_of(&sim) == CLASSIC_RUN_RUNNING);
    CHECK(run_onsets(&sim, 5000u, 400u, 3u) == 0u);
    CHECK(run_state_of(&sim) == CLASSIC_RUN_RUNNING);
    CHECK(next_jump(&sim, &jump, 10000u) && sim.last_jump_ms == 20000u);
}

static void a_failed_tune_stays_on_the_landing(void)
{
    ClassicScanJump jump;
    Sim sim;

    hold_start(&sim, 1u);
    CHECK(!onset_tick(&sim, 2u, NULL)); /* hold from t = 601, capped at 1601 */
    CHECK(next_jump(&sim, &jump, 2000u) && sim.last_jump_ms == 1601u);
    /* The tune fails: the frequency stays on channel 11, the same landing. */
    sim.in.tune_in_flight = false;
    (void)run_for(&sim, 100u);
    CHECK(sim_channel(&sim) == 11u);
    CHECK(!onset_tick(&sim, 3u, NULL));
    CHECK(run_state_of(&sim) == CLASSIC_RUN_RUNNING);
}

static void no_hold_without_a_measurement(void)
{
    ClassicScanJump jump;
    Sim sim;

    hold_start(&sim, 5u);
    /* No valid measurement: no hold (item 17, FR-019). */
    sim.measure = false;
    CHECK(!onset_tick(&sim, 3u, NULL));
    CHECK(run_state_of(&sim) == CLASSIC_RUN_RUNNING);
    /* Receiver output during a tune is not activity, even if reported. */
    sim.measure = true;
    CHECK(next_jump(&sim, &jump, 1000u) && sim.last_jump_ms == 1000u);
    CHECK(sim.in.tune_in_flight);
    sim.in.onset = 3u;
    sim.in.activity_valid = true;
    CHECK(!ClassicScan_Service(sim.now, &sim.in, &jump));
    sim.in.onset = 0u;
    CHECK(run_state_of(&sim) == CLASSIC_RUN_RUNNING);
    /* A hold in progress ends when the measurement becomes unavailable; the
     * jump that fell due during the hold goes at once. */
    (void)run_for(&sim, 100u);
    CHECK(!onset_tick(&sim, 2u, NULL)); /* hold from t = 1101 */
    CHECK(run_onsets(&sim, 600u, 300u, 1u) == 0u);
    CHECK(run_state_of(&sim) == CLASSIC_RUN_HOLDING);
    sim.measure = false;
    CHECK(sim_tick(&sim, &jump) && sim.last_jump_ms == 1702u);
    CHECK(run_state_of(&sim) == CLASSIC_RUN_RUNNING);
    CHECK(next_jump(&sim, &jump, 1000u) && sim.last_jump_ms == 2202u);
}

static void a_hold_ends_when_classic_cannot_move(void)
{
    Sim sim;

    hold_start(&sim, 10u);
    CHECK(!onset_tick(&sim, 2u, NULL));
    CHECK(run_state_of(&sim) == CLASSIC_RUN_HOLDING);
    /* Manual takes over, then returns: the hold is over, and the next jump
     * waits one period (item 21). */
    sim.in.active = false;
    CHECK(run_onsets(&sim, 2000u, 100u, 3u) == 0u);
    CHECK(run_state_of(&sim) == CLASSIC_RUN_RUNNING);
    sim.in.active = true;
    (void)sim_tick(&sim, NULL);
    CHECK(run_state_of(&sim) == CLASSIC_RUN_RUNNING);
    /* The same landing already had its hold. */
    CHECK(run_onsets(&sim, 499u, 100u, 3u) == 0u);
    CHECK(run_state_of(&sim) == CLASSIC_RUN_RUNNING);
    CHECK(sim_tick(&sim, NULL) && sim.last_jump_ms == 3102u);

    /* The radio cannot tune. */
    hold_start(&sim, 10u);
    CHECK(!onset_tick(&sim, 2u, NULL));
    sim.in.can_tune = false;
    (void)sim_tick(&sim, NULL);
    CHECK(run_state_of(&sim) == CLASSIC_RUN_RUNNING);
}

static void a_tune_elsewhere_ends_the_hold(void)
{
    Sim sim;

    hold_start(&sim, 10u);
    CHECK(!onset_tick(&sim, 2u, NULL));
    CHECK(run_state_of(&sim) == CLASSIC_RUN_HOLDING);
    /* The CLI tuned while Classic held: a new landing, no hold yet. */
    sim.in.frequency_khz = ClassicScan_ChannelKhz(&bands[FM], 50u);
    (void)sim_tick(&sim, NULL);
    CHECK(run_state_of(&sim) == CLASSIC_RUN_RUNNING);
    /* That landing may hold. */
    CHECK(!onset_tick(&sim, 2u, NULL));
    CHECK(run_state_of(&sim) == CLASSIC_RUN_HOLDING);
}

static void pausing_ends_a_hold_and_resuming_jumps(void)
{
    ClassicScanJump jump;
    Sim sim;

    hold_start(&sim, 10u);
    CHECK(!onset_tick(&sim, 2u, NULL));
    CHECK(run_onsets(&sim, 2000u, 500u, 1u) == 0u);
    CHECK(run_state_of(&sim) == CLASSIC_RUN_HOLDING);
    /* Pausing during a hold pauses (item 20, spec US5 scenario 5). */
    ClassicScan_ToggleRun();
    CHECK(run_state_of(&sim) == CLASSIC_RUN_PAUSED);
    CHECK(run_onsets(&sim, 5000u, 300u, 3u) == 0u);
    CHECK(run_state_of(&sim) == CLASSIC_RUN_PAUSED);
    /* Resuming makes the next jump at once, even with an onset on that pass. */
    ClassicScan_ToggleRun();
    CHECK(onset_tick(&sim, 3u, &jump) && jump.channel_index == 12u);
    CHECK(run_state_of(&sim) == CLASSIC_RUN_RUNNING);

    /* A pause and resume between two passes also end the hold. */
    hold_start(&sim, 10u);
    CHECK(!onset_tick(&sim, 2u, NULL));
    ClassicScan_ToggleRun();
    ClassicScan_ToggleRun();
    CHECK(onset_tick(&sim, 1u, &jump) && jump.channel_index == 12u);

    /* On a landing that has not held, an onset on the resume pass does not
     * hold either: the resume jumps (item 8). */
    hold_start(&sim, 10u);
    ClassicScan_ToggleRun();
    (void)run_for(&sim, 100u);
    ClassicScan_ToggleRun();
    CHECK(onset_tick(&sim, 3u, &jump) && jump.channel_index == 12u);
}

static void a_hold_time_change_applies_at_once(void)
{
    Sim sim;

    hold_start(&sim, 10u);
    CHECK(!onset_tick(&sim, 2u, NULL)); /* t = 601 */
    CHECK(run_state_of(&sim) == CLASSIC_RUN_HOLDING);
    /* Turning the hold off ends the hold; the jump keeps its due time. */
    CHECK(ClassicScan_StepHoldTime(-255));
    CHECK(!sim_tick(&sim, NULL));
    CHECK(run_state_of(&sim) == CLASSIC_RUN_RUNNING);
    CHECK(run_onsets(&sim, 397u, 50u, 3u) == 0u);
    CHECK(sim_tick(&sim, NULL) && sim.last_jump_ms == 1000u);
}

static void bad_arguments_are_refused(void)
{
    ClassicScanConfig config;
    ClassicScanInput input;
    ClassicScanJump jump;
    ClassicScanStatus status;

    init_default();
    memset(&input, 0, sizeof(input));
    CHECK(!ClassicScan_Service(0u, NULL, &jump));
    CHECK(!ClassicScan_Service(0u, &input, NULL));
    input.band = CLASSIC_SCAN_BAND_COUNT;
    input.tuning_valid = input.active = input.can_tune = true;
    CHECK(!ClassicScan_Service(1000u, &input, &jump));
    CHECK(!ClassicScan_GetStatus(CLASSIC_SCAN_BAND_COUNT, 0u, &status));
    CHECK(!ClassicScan_GetStatus(FM, 0u, NULL));
    ClassicScan_DefaultConfig(NULL);
    ClassicScan_DefaultConfig(&config);
    CHECK(ClassicScan_ChannelCount(NULL) == 0u);
}

int main(void)
{
    channel_arithmetic_follows_the_band_table();
    startup_matches_decision_0016();
    config_is_validated();
    controls_stop_at_both_ends();
    distance_sequence_is_cut_at_half_the_band();
    a_sweep_lands_on_every_channel_once();
    every_jump_follows_the_edge_formulas();
    wrap_and_bounce_carry_the_remainder();
    stop_ends_the_sweep_on_the_edge();
    a_resume_mid_band_continues_in_every_mode();
    every_rate_keeps_its_count_over_five_minutes();
    a_coarse_loop_does_not_drift();
    late_tunes_cause_no_burst_and_no_skip();
    a_stalled_loop_does_not_catch_up();
    a_pause_lands_before_the_next_jump();
    a_resume_jumps_at_once_but_waits_for_a_tune();
    the_band_limit_caps_the_rate_but_keeps_the_setting();
    a_rate_change_applies_from_the_next_due_time();
    direction_applies_to_the_next_jump();
    startup_waits_for_the_first_tune_then_one_period();
    no_jump_or_retry_while_tuning_is_not_possible();
    each_band_remembers_its_distance();
    navigation_round_trips_keep_classic();
    menus_do_not_interrupt_the_scan();
    a_completed_sweep_tuned_away_returns_paused();
    the_hold_time_steps_and_stops_at_both_ends();
    a_zero_hold_time_keeps_the_rhythm();
    a_hold_starts_extends_and_releases();
    an_onset_one_release_time_later_still_extends();
    the_hold_time_caps_a_hold();
    each_landing_holds_at_most_once();
    a_failed_tune_stays_on_the_landing();
    no_hold_without_a_measurement();
    a_hold_ends_when_classic_cannot_move();
    a_tune_elsewhere_ends_the_hold();
    pausing_ends_a_hold_and_resuming_jumps();
    a_hold_time_change_applies_at_once();
    bad_arguments_are_refused();

    if (failures != 0u) {
        printf("%u failure(s)\n", failures);
        return 1;
    }
    printf("classic_scan_test: all checks passed\n");
    return 0;
}
