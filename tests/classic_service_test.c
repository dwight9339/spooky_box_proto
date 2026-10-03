/*
 * Host tests for Classic on the M7 (full_spooky_proto-54w.33): the classic_scan
 * core run against shared radio and session state through the real command
 * policy, with the radio queue and the event log as fakes. Host results only.
 */

#include "classic_service.h"

#include <stdio.h>
#include <string.h>

#include "sm/radio_port.h"
#include "sm/session_port.h"

static unsigned failures;

#define CHECK(condition) do {                                               \
    if (!(condition)) {                                                     \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);       \
        ++failures;                                                         \
    }                                                                       \
} while (0)

enum { FM = 0, AM = 1, SW = 2, LW = 3 };

static const ClassicTerritory bands[CLASSIC_SCAN_BAND_COUNT] = {
    {87500u, 108000u, 100u},
    {520u, 1710u, 10u},
    {2300u, 23000u, 5u},
    {153u, 279u, 9u},
};

/* --- Fakes --------------------------------------------------------------------- */

static bool queue_accepts;
static unsigned requests;
static uint32_t requested_khz;
static unsigned events[CLASSIC_PUB_COUNT];
static unsigned event_total;
static ClassicState event_state;
static uint32_t event_ms;

bool classic_integration_request_tune(uint32_t frequency_khz)
{
    ++requests;
    requested_khz = frequency_khz;
    return queue_accepts;
}

void classic_integration_publish(ClassicPublished event, uint32_t now_ms,
                                 const ClassicState *state)
{
    ++events[event];
    ++event_total;
    event_state = *state;
    event_ms = now_ms;
}

static void clear_events(void)
{
    memset(events, 0, sizeof(events));
    event_total = 0u;
}

/* The radio as the adapter reads it: settled on FM at the 99.1 MHz default. */
static ClassicWorld world;
static uint32_t now_ms;

static void start(void)
{
    CHECK(ClassicService_Init(bands));
    queue_accepts = true;
    requests = 0u;
    requested_khz = 0u;
    clear_events();
    now_ms = 1000u;
    memset(&world, 0, sizeof(world));
    world.band = FM;
    world.frequency_khz = 99100u;
    world.tune_valid = true;
    world.radio_state = RAD_STATE_SETTLED;
    world.session_state = SES_STATE_IDLE;
    world.active = true;
}

static ClassicState state_now(void)
{
    ClassicState state;

    memset(&state, 0, sizeof(state));
    CHECK(ClassicService_GetState(&world, &state));
    return state;
}

/* Runs the foreground loop for duration_ms in 5 ms passes. */
static void run(uint32_t duration_ms)
{
    const uint32_t end = now_ms + duration_ms;

    while ((int32_t)(now_ms - end) < 0) {
        ClassicService_Service(now_ms, &world);
        now_ms += 5u;
    }
}

/* The Radio machine dispatches Classic's command and completes the tune. */
static void radio_completes(void)
{
    world.frequency_khz = requested_khz;
    ClassicService_OnTuneAnswered(true);
}

static void command(CtxCommand which, int32_t arg)
{
    CHECK(ClassicService_OnCommand(which, arg, now_ms, &world));
}

/* --- Tests --------------------------------------------------------------------- */

/* SC-001 on the host: Classic waits for the radio to start, then scans FM
 * upward from the default frequency with no input. */
static void boots_scanning_fm_once_the_radio_starts(void)
{
    ClassicState state;

    start();
    world.radio_state = RAD_STATE_STOPPED;
    run(3000u);
    CHECK(requests == 0u);
    /* The first pass publishes every fact. */
    CHECK(event_total == CLASSIC_PUB_COUNT);
    state = state_now();
    CHECK(state.run_state == CLASSIC_STATE_UNABLE);
    CHECK(state.unable_reason == CLASSIC_UNABLE_RADIO_STOPPED);

    clear_events();
    world.radio_state = RAD_STATE_SETTLED;
    {
        const uint32_t started = now_ms;

        while ((requests == 0u) && ((now_ms - started) < 2000u)) {
            run(5u);
        }
        CHECK(requests == 1u && requested_khz == 99200u);
        /* One period of the default 120 per minute after the radio started. */
        CHECK((now_ms - 5u) - started == 500u);
    }
    CHECK(events[CLASSIC_PUB_RUN_STATE] == 1u && event_total == 1u);
    state = state_now();
    CHECK(state.run_state == CLASSIC_STATE_RUNNING);
    CHECK(state.unable_reason == CLASSIC_UNABLE_NONE);
    CHECK(state.band == FM && state.channel_index == 116u && state.channel_count == 206u);
    CHECK(state.direction_up && state.edge == CLASSIC_EDGE_WRAP);
    CHECK(state.rate_per_min == 120u && state.distance_channels == 1u);
    CHECK(state.distance_khz == 100u);
}

/* Never two tunes in flight: Classic waits for its own unanswered command and
 * for any tune the radio is running, then continues from the landing. */
static void one_tune_in_flight_at_a_time(void)
{
    ClassicServiceStats stats;

    start();
    run(505u);
    CHECK(requests == 1u);
    ClassicService_GetStats(&stats);
    CHECK(stats.tune_outstanding);
    run(2000u); /* unanswered: the queue or the radio still holds it */
    CHECK(requests == 1u);
    world.radio_state = RAD_STATE_TUNING;
    radio_completes(); /* answered, but a CLI tune is now in flight */
    run(2000u);
    CHECK(requests == 1u);
    world.radio_state = RAD_STATE_SETTLED;
    run(5u);
    CHECK(requests == 2u && requested_khz == 99300u);
    radio_completes();
    ClassicService_GetStats(&stats);
    CHECK(!stats.tune_outstanding && stats.tunes_requested == 2u);
}

/* A CLI tune moves the shared frequency; Classic continues from it (FR-022).
 * A jump that comes due while the CLI command is still queued waits for its
 * answer, so it is computed from the frequency the CLI tuned. */
static void continues_from_wherever_the_cli_tuned(void)
{
    start();
    run(505u);
    radio_completes();
    world.frequency_khz = 104000u;
    run(500u);
    CHECK(requests == 2u && requested_khz == 104100u);
    radio_completes();

    world.radio_command_pending = true; /* CLI TUNE 90000 queued */
    run(1500u);
    CHECK(requests == 2u);
    world.radio_command_pending = false;
    world.frequency_khz = 90000u;
    run(5u);
    CHECK(requests == 3u && requested_khz == 90100u);
}

/* FR-027 and SC-007: while the policy rejects tuning in the session, Classic
 * shows why, issues nothing and does not retry; it resumes one period after. */
static void the_session_guard_makes_classic_unable_to_scan(void)
{
    static const SesState guarded[] = {
        SES_STATE_PREPARING, SES_STATE_RECORDING, SES_STATE_FINALIZING
    };
    size_t index;

    for (index = 0u; index < (sizeof(guarded) / sizeof(guarded[0])); ++index) {
        ClassicState state;
        uint32_t back;

        start();
        run(505u);
        radio_completes();
        clear_events();
        world.session_state = guarded[index];
        run(10000u);
        CHECK(requests == 1u);
        CHECK(events[CLASSIC_PUB_RUN_STATE] == 1u && event_total == 1u);
        CHECK(event_state.run_state == CLASSIC_STATE_UNABLE);
        CHECK(event_state.unable_reason == CLASSIC_UNABLE_SESSION);
        state = state_now();
        CHECK(state.run_state == CLASSIC_STATE_UNABLE);

        /* Parameters stay adjustable (FR-010) and nothing tunes. */
        command(CTX_CMD_JUMP_RATE, 1);
        command(CTX_CMD_TOGGLE_DIRECTION, 0);
        run(1000u);
        CHECK(requests == 1u);

        world.session_state = SES_STATE_IDLE;
        back = now_ms;
        while ((requests == 1u) && ((now_ms - back) < 2000u)) {
            run(5u);
        }
        CHECK(requests == 2u && requested_khz == 99100u); /* down from 99.2 */
        CHECK((now_ms - 5u) - back == 400u);              /* 150 per minute */
        CHECK(state_now().run_state == CLASSIC_STATE_RUNNING);
    }
}

/* The reason is part of the run state: a new reason is a new event even while
 * Classic stays unable to scan. */
static void a_new_reason_is_published(void)
{
    start();
    world.session_state = SES_STATE_RECORDING;
    run(5u);
    CHECK(state_now().unable_reason == CLASSIC_UNABLE_SESSION);
    clear_events();
    world.radio_state = RAD_STATE_FAULTED;
    run(5u);
    CHECK(events[CLASSIC_PUB_RUN_STATE] == 1u && event_total == 1u);
    CHECK(event_state.run_state == CLASSIC_STATE_UNABLE);
    CHECK(event_state.unable_reason == CLASSIC_UNABLE_RADIO_FAULTED);
}

static void a_radio_fault_is_shown_and_never_retried(void)
{
    ClassicState state;

    start();
    run(505u);
    /* The fault abandons the tune in flight. */
    world.radio_state = RAD_STATE_FAULTED;
    ClassicService_OnTuneAnswered(false);
    run(10000u);
    CHECK(requests == 1u);
    state = state_now();
    CHECK(state.run_state == CLASSIC_STATE_UNABLE);
    CHECK(state.unable_reason == CLASSIC_UNABLE_RADIO_FAULTED);
    /* A paused Classic stays paused; the reason is still available. */
    command(CTX_CMD_RUN_PAUSE, CLASSIC_RUN_TOGGLE);
    state = state_now();
    CHECK(state.run_state == CLASSIC_STATE_PAUSED);
    CHECK(state.unable_reason == CLASSIC_UNABLE_RADIO_FAULTED);
    {
        ClassicServiceStats stats;

        ClassicService_GetStats(&stats);
        CHECK(stats.tunes_failed == 1u);
    }
}

/* C-103 to C-106 and C-110: each command changes its fact, publishes one event
 * with the new state, and applies to the next jump. */
static void commands_publish_one_event_each(void)
{
    start();
    run(5u);
    clear_events();

    command(CTX_CMD_RUN_PAUSE, CLASSIC_RUN_ENSURE_RUNNING); /* already running */
    CHECK(event_total == 0u);
    command(CTX_CMD_RUN_PAUSE, CLASSIC_RUN_ENSURE_PAUSED);
    CHECK(events[CLASSIC_PUB_RUN_STATE] == 1u);
    CHECK(event_state.run_state == CLASSIC_STATE_PAUSED && event_ms == now_ms);
    command(CTX_CMD_RUN_PAUSE, CLASSIC_RUN_ENSURE_PAUSED);
    CHECK(events[CLASSIC_PUB_RUN_STATE] == 1u);
    run(5000u);
    CHECK(requests == 0u);

    command(CTX_CMD_TOGGLE_DIRECTION, 0);
    CHECK(events[CLASSIC_PUB_DIRECTION] == 1u && !event_state.direction_up);
    command(CTX_CMD_JUMP_RATE, -2);
    CHECK(events[CLASSIC_PUB_RATE] == 1u && event_state.rate_setting_per_min == 80u);
    command(CTX_CMD_JUMP_DISTANCE, 3);
    CHECK(events[CLASSIC_PUB_DISTANCE] == 1u && event_state.distance_channels == 4u);
    CHECK(event_state.distance_khz == 400u);
    command(CTX_CMD_EDGE_BEHAVIOR, 1);
    CHECK(events[CLASSIC_PUB_EDGE] == 1u && event_state.edge == CLASSIC_EDGE_BOUNCE);
    command(CTX_CMD_EDGE_BEHAVIOR, -5);
    CHECK(events[CLASSIC_PUB_EDGE] == 2u && event_state.edge == CLASSIC_EDGE_WRAP);
    CHECK(event_total == 6u);

    /* A resume jumps at once, down by four channels. */
    command(CTX_CMD_RUN_PAUSE, CLASSIC_RUN_TOGGLE);
    CHECK(events[CLASSIC_PUB_RUN_STATE] == 2u && event_state.run_state == CLASSIC_STATE_RUNNING);
    run(5u);
    CHECK(requests == 1u && requested_khz == 98700u);

    /* Not Classic's: the activity hold (54w.32) and other engines' commands. */
    CHECK(!ClassicService_OnCommand(CTX_CMD_HOLD_TIME, 1, now_ms, &world));
    CHECK(!ClassicService_OnCommand(CTX_CMD_TUNE, 1, now_ms, &world));
    CHECK(!ClassicService_OnCommand(CTX_CMD_RUN_PAUSE, 7, now_ms, &world));
    CHECK(!ClassicService_OnCommand(CTX_CMD_RUN_PAUSE, 0, now_ms, NULL));
}

/* FR-029: a bounce reversal is published like a direction toggle. */
static void a_bounce_reversal_is_published(void)
{
    start();
    command(CTX_CMD_EDGE_BEHAVIOR, 1);
    world.frequency_khz = 108000u;
    run(5u);
    clear_events();
    run(500u);
    CHECK(requests == 1u && requested_khz == 107900u);
    CHECK(events[CLASSIC_PUB_DIRECTION] == 1u && !event_state.direction_up);
}

/* FR-003 and FR-009: a band change brings that band's distance and rate limit,
 * published as events, and the run state is unchanged. */
static void a_band_change_brings_the_band_parameters(void)
{
    start();
    command(CTX_CMD_JUMP_RATE, 100); /* 400 per minute */
    run(5u);
    CHECK(state_now().rate_per_min == 400u && !state_now().rate_limited);
    clear_events();
    world.band = AM;
    world.frequency_khz = 1000u;
    run(5u);
    CHECK(events[CLASSIC_PUB_RATE] == 1u);
    CHECK(event_state.rate_per_min == 180u && event_state.rate_limited);
    CHECK(events[CLASSIC_PUB_DISTANCE] == 1u && event_state.distance_khz == 10u);
    CHECK(events[CLASSIC_PUB_RUN_STATE] == 0u);
    command(CTX_CMD_JUMP_DISTANCE, 2); /* AM only */
    world.band = FM;
    world.frequency_khz = 99100u;
    CHECK(state_now().distance_channels == 1u);
    world.band = AM;
    world.frequency_khz = 1000u;
    CHECK(state_now().distance_channels == 3u);
}

/* A refused queue post or a failed tune leaves the frequency where it was; the
 * next jump goes to the same channel rather than skipping it. */
static void a_lost_tune_delays_the_landing_without_skipping_it(void)
{
    ClassicServiceStats stats;

    start();
    queue_accepts = false;
    run(505u);
    CHECK(requests == 1u && requested_khz == 99200u);
    queue_accepts = true;
    run(500u);
    CHECK(requests == 2u && requested_khz == 99200u);
    ClassicService_OnTuneAnswered(false); /* failed: still on 99.1 */
    run(500u);
    CHECK(requests == 3u && requested_khz == 99200u);
    ClassicService_GetStats(&stats);
    CHECK(stats.tunes_refused == 1u && stats.tunes_failed == 1u);
    CHECK(stats.tunes_requested == 2u && stats.jumps == 3u);
    /* An answer to nothing outstanding changes nothing. */
    radio_completes();
    ClassicService_OnTuneAnswered(false);
    ClassicService_GetStats(&stats);
    CHECK(stats.tunes_failed == 1u);
}

/* While another engine or Manual has the radio, Classic does not move. */
static void inactive_classic_does_not_move(void)
{
    start();
    world.active = false;
    run(10000u);
    CHECK(requests == 0u);
    CHECK(state_now().run_state == CLASSIC_STATE_RUNNING);
    world.active = true;
    run(505u);
    CHECK(requests == 1u);
}

static void names_cover_every_value(void)
{
    CHECK(strcmp(ClassicService_RunName(CLASSIC_STATE_UNABLE), "UNABLE") == 0);
    CHECK(strcmp(ClassicService_RunName(9u), "UNKNOWN") == 0);
    CHECK(strcmp(ClassicService_ReasonName(CLASSIC_UNABLE_SESSION), "SESSION") == 0);
    CHECK(strcmp(ClassicService_EdgeName(CLASSIC_EDGE_STOP), "STOP") == 0);
    CHECK(strcmp(ClassicService_EventName(CLASSIC_PUB_EDGE), "EDGE") == 0);
    CHECK(strcmp(ClassicService_EventName(CLASSIC_PUB_COUNT), "UNKNOWN") == 0);
}

int main(void)
{
    boots_scanning_fm_once_the_radio_starts();
    one_tune_in_flight_at_a_time();
    continues_from_wherever_the_cli_tuned();
    the_session_guard_makes_classic_unable_to_scan();
    a_new_reason_is_published();
    a_radio_fault_is_shown_and_never_retried();
    commands_publish_one_event_each();
    a_bounce_reversal_is_published();
    a_band_change_brings_the_band_parameters();
    a_lost_tune_delays_the_landing_without_skipping_it();
    inactive_classic_does_not_move();
    names_cover_every_value();

    if (failures != 0u) {
        printf("%u failure(s)\n", failures);
        return 1;
    }
    printf("classic_service_test: all checks passed\n");
    return 0;
}
