/*
 * Host scenario tests for the Session machine (docs/design/behavior/SessionSm.puml).
 * The generated machine and the real port run against a fake integration that
 * scripts action results and records every call. Host results only; not hardware
 * evidence.
 */

#include <stdio.h>
#include <string.h>

#include "session_port.h"

/* --- Fake integration ---------------------------------------------------------- */

#define LOG_CAPACITY 64u

typedef enum Call {
    CALL_CAN_START = 0,
    CALL_OPEN_FILE,
    CALL_START_CAPTURE,
    CALL_REQUEST_STOP,
    CALL_TARGET_REACHED,
    CALL_STOP_CAPTURE,
    CALL_FINALIZE,
    CALL_COUNT
} Call;

typedef struct Fake {
    /* Scripted results. */
    bool can_start;
    bool open_ok;
    bool capture_ok;
    bool target_reached;
    bool finalize_ok;
    /* Recorded calls. */
    unsigned calls[CALL_COUNT];
    uint32_t open_seconds;
    unsigned published_count;
    SesPublished published[LOG_CAPACITY];
    unsigned state_changes;
    SesState last_state;
    /* Order of the last finalize result against publications. */
    bool last_finalize_result;
} Fake;

static Fake fake;

bool ses_integration_can_start(uint32_t seconds)
{
    (void)seconds;
    ++fake.calls[CALL_CAN_START];
    return fake.can_start;
}

bool ses_integration_open_file(uint32_t seconds)
{
    ++fake.calls[CALL_OPEN_FILE];
    fake.open_seconds = seconds;
    return fake.open_ok;
}

bool ses_integration_start_capture(void)
{
    ++fake.calls[CALL_START_CAPTURE];
    return fake.capture_ok;
}

void ses_integration_request_stop(void) { ++fake.calls[CALL_REQUEST_STOP]; }

bool ses_integration_target_reached(void)
{
    ++fake.calls[CALL_TARGET_REACHED];
    return fake.target_reached;
}

void ses_integration_stop_capture(void) { ++fake.calls[CALL_STOP_CAPTURE]; }

bool ses_integration_finalize_file(void)
{
    ++fake.calls[CALL_FINALIZE];
    fake.last_finalize_result = fake.finalize_ok;
    return fake.finalize_ok;
}

void ses_integration_publish(SesPublished event)
{
    if (fake.published_count < LOG_CAPACITY) {
        fake.published[fake.published_count] = event;
    }
    ++fake.published_count;
}

void ses_integration_state_changed(SesState state)
{
    ++fake.state_changes;
    fake.last_state = state;
}

/* --- Helpers ------------------------------------------------------------------- */

static int failures;
static const char *current_test;

#define CHECK(cond)                                                                   \
    do {                                                                              \
        if (!(cond)) {                                                                \
            printf("FAIL %s (line %d): %s\n", current_test, __LINE__, #cond);        \
            ++failures;                                                               \
        }                                                                             \
    } while (0)

static void reset(void)
{
    memset(&fake, 0, sizeof(fake));
    fake.can_start = true;
    fake.open_ok = true;
    fake.capture_ok = true;
    fake.finalize_ok = true;
    Session_Init();
    fake.state_changes = 0; /* ignore the initial entry into Idle */
}

static bool last_published_is(SesPublished event)
{
    return fake.published_count > 0 && fake.published[fake.published_count - 1] == event;
}

static void start_recording(void)
{
    Session_OnStart(60u);
}

static void start_finalizing(void)
{
    start_recording();
    Session_OnStop();
}

/* --- Scenarios ----------------------------------------------------------------- */

static void starting_opens_the_file_starts_capture_and_records(void)
{
    reset();
    Session_OnStart(60u);
    CHECK(Session_GetState() == SES_STATE_RECORDING && Session_IsActive());
    CHECK(fake.calls[CALL_OPEN_FILE] == 1 && fake.open_seconds == 60u);
    CHECK(fake.calls[CALL_START_CAPTURE] == 1 && fake.calls[CALL_FINALIZE] == 0);
    CHECK(fake.published_count == 1 && last_published_is(SES_PUB_RECORDING_STARTED));
    CHECK(fake.state_changes == 1 && fake.last_state == SES_STATE_RECORDING);
}

static void the_start_guard_is_evaluated_once_per_start(void)
{
    reset();
    Session_OnStart(60u);
    CHECK(fake.calls[CALL_CAN_START] == 1);
    reset();
    fake.can_start = false;
    Session_OnStart(60u);
    CHECK(fake.calls[CALL_CAN_START] == 1);
}

static void a_start_that_cannot_start_is_rejected_without_touching_the_card(void)
{
    /* No card, card busy, radio not running, PDM not ready or bad duration. */
    reset();
    fake.can_start = false;
    Session_OnStart(0u);
    CHECK(Session_GetState() == SES_STATE_IDLE);
    CHECK(fake.calls[CALL_OPEN_FILE] == 0 && fake.calls[CALL_START_CAPTURE] == 0);
    CHECK(fake.published_count == 1 && last_published_is(SES_PUB_RECORDING_REJECTED));
    CHECK(fake.state_changes == 0);
}

static void a_file_that_cannot_be_opened_rejects_the_start(void)
{
    reset();
    fake.open_ok = false;
    Session_OnStart(60u);
    CHECK(Session_GetState() == SES_STATE_IDLE);
    CHECK(fake.calls[CALL_OPEN_FILE] == 1 && fake.calls[CALL_START_CAPTURE] == 0);
    CHECK(fake.calls[CALL_FINALIZE] == 0);
    CHECK(fake.published_count == 1 && last_published_is(SES_PUB_RECORDING_REJECTED));
}

static void capture_that_does_not_start_aborts_and_finalizes(void)
{
    reset();
    fake.capture_ok = false;
    Session_OnStart(60u);
    CHECK(Session_GetState() == SES_STATE_IDLE);
    CHECK(fake.calls[CALL_START_CAPTURE] == 1 && fake.calls[CALL_FINALIZE] == 1);
    CHECK(fake.published_count == 1 && last_published_is(SES_PUB_RECORDING_ABORTED));
    CHECK(fake.last_state != SES_STATE_RECORDING);
}

static void stop_while_idle_is_ignored(void)
{
    reset();
    Session_OnStop();
    CHECK(Session_GetState() == SES_STATE_IDLE);
    CHECK(fake.calls[CALL_REQUEST_STOP] == 0);
    CHECK(fake.published_count == 1 && last_published_is(SES_PUB_STOP_IGNORED));
}

static void start_while_active_is_rejected_and_changes_nothing(void)
{
    reset();
    start_recording();
    Session_OnStart(30u);
    CHECK(Session_GetState() == SES_STATE_RECORDING);
    CHECK(fake.calls[CALL_OPEN_FILE] == 1);
    CHECK(last_published_is(SES_PUB_RECORDING_REJECTED));
    start_finalizing();
    Session_OnStart(30u);
    CHECK(Session_GetState() == SES_STATE_FINALIZING);
    CHECK(last_published_is(SES_PUB_RECORDING_REJECTED));
}

static void stop_requests_finalizing_at_the_next_block(void)
{
    reset();
    start_recording();
    Session_OnStop();
    CHECK(Session_GetState() == SES_STATE_FINALIZING && Session_IsActive());
    CHECK(fake.calls[CALL_REQUEST_STOP] == 1 && fake.calls[CALL_FINALIZE] == 0);
    CHECK(last_published_is(SES_PUB_RECORDING_STOPPING));
    CHECK(fake.last_state == SES_STATE_FINALIZING);
}

static void repeated_stop_is_acknowledged_once_more_without_a_second_request(void)
{
    reset();
    start_finalizing();
    const unsigned published = fake.published_count;
    Session_OnStop();
    Session_OnStop();
    CHECK(Session_GetState() == SES_STATE_FINALIZING);
    CHECK(fake.calls[CALL_REQUEST_STOP] == 1);
    CHECK(fake.published_count == published + 2 && last_published_is(SES_PUB_RECORDING_STOPPING));
}

static void blocks_below_the_target_keep_recording(void)
{
    reset();
    start_recording();
    const unsigned published = fake.published_count;
    for (int i = 0; i < 100; ++i) {
        Session_OnBlockWritten();
    }
    CHECK(Session_GetState() == SES_STATE_RECORDING);
    CHECK(fake.published_count == published);
    CHECK(fake.calls[CALL_STOP_CAPTURE] == 0 && fake.calls[CALL_FINALIZE] == 0);
}

static void reaching_the_target_completes_the_session(void)
{
    reset();
    start_recording();
    fake.target_reached = true;
    Session_OnBlockWritten();
    CHECK(Session_GetState() == SES_STATE_IDLE);
    CHECK(fake.calls[CALL_STOP_CAPTURE] == 1 && fake.calls[CALL_FINALIZE] == 1);
    CHECK(last_published_is(SES_PUB_RECORDING_COMPLETED));
    CHECK(fake.last_state == SES_STATE_IDLE);
}

static void a_target_reached_with_a_failed_finalize_is_an_abort(void)
{
    reset();
    start_recording();
    fake.target_reached = true;
    fake.finalize_ok = false;
    Session_OnBlockWritten();
    CHECK(Session_GetState() == SES_STATE_IDLE);
    CHECK(last_published_is(SES_PUB_RECORDING_ABORTED));
}

static void the_next_block_after_a_stop_completes_the_session(void)
{
    reset();
    start_finalizing();
    fake.target_reached = false; /* a stop does not wait for the target */
    Session_OnBlockWritten();
    CHECK(Session_GetState() == SES_STATE_IDLE);
    CHECK(fake.calls[CALL_STOP_CAPTURE] == 1 && fake.calls[CALL_FINALIZE] == 1);
    CHECK(last_published_is(SES_PUB_RECORDING_COMPLETED));
}

static void a_failed_finalize_after_a_stop_is_an_abort(void)
{
    reset();
    start_finalizing();
    fake.finalize_ok = false;
    Session_OnBlockWritten();
    CHECK(Session_GetState() == SES_STATE_IDLE);
    CHECK(last_published_is(SES_PUB_RECORDING_ABORTED));
}

static void a_capture_fault_aborts_from_every_active_state(void)
{
    for (int finalizing = 0; finalizing < 2; ++finalizing) {
        for (int finalized = 0; finalized < 2; ++finalized) {
            reset();
            if (finalizing) {
                start_finalizing();
            } else {
                start_recording();
            }
            fake.finalize_ok = finalized != 0;
            Session_OnCaptureFault();
            CHECK(Session_GetState() == SES_STATE_IDLE);
            CHECK(fake.calls[CALL_STOP_CAPTURE] == 1 && fake.calls[CALL_FINALIZE] == 1);
            /* Aborted even when the partial file was finalized: never a success. */
            CHECK(last_published_is(SES_PUB_RECORDING_ABORTED));
        }
    }
}

static void idle_ignores_blocks_and_faults(void)
{
    reset();
    Session_OnBlockWritten();
    Session_OnCaptureFault();
    CHECK(Session_GetState() == SES_STATE_IDLE);
    CHECK(fake.published_count == 0 && fake.calls[CALL_FINALIZE] == 0);
}

static void a_new_session_can_start_after_each_ending(void)
{
    reset();
    for (int ending = 0; ending < 4; ++ending) {
        start_recording();
        CHECK(Session_GetState() == SES_STATE_RECORDING);
        fake.target_reached = true;
        switch (ending) {
        case 0: Session_OnBlockWritten(); break;
        case 1: Session_OnStop(); Session_OnBlockWritten(); break;
        case 2: Session_OnCaptureFault(); break;
        default: fake.finalize_ok = false; Session_OnBlockWritten(); fake.finalize_ok = true; break;
        }
        fake.target_reached = false;
        CHECK(Session_GetState() == SES_STATE_IDLE);
    }
}

/* Random sequences: a success is published only after a finalize that succeeded,
 * every started session ends in exactly one completion or abort, and the machine is
 * active exactly between a start and its ending. */
static void outcomes_are_never_shown_as_success_after_a_failure(void)
{
    uint32_t rng = 777u;
    for (int run = 0; run < 300; ++run) {
        reset();
        bool active = false;
        unsigned started = 0, ended = 0;
        for (int step = 0; step < 100; ++step) {
            rng = rng * 1664525u + 1013904223u;
            const uint32_t r = rng >> 8;
            fake.can_start = (r & 1u) != 0;
            fake.open_ok = (r & 2u) != 0;
            fake.capture_ok = (r & 4u) != 0;
            fake.target_reached = (r & 8u) != 0;
            fake.finalize_ok = (r & 16u) != 0;
            const unsigned before = fake.published_count;
            const unsigned finalizes = fake.calls[CALL_FINALIZE];
            switch ((r >> 5) % 4u) {
            case 0: Session_OnStart(1u + (r >> 7) % 3600u); break;
            case 1: Session_OnStop(); break;
            case 2: Session_OnBlockWritten(); break;
            default: Session_OnCaptureFault(); break;
            }
            for (unsigned i = before; i < fake.published_count && i < LOG_CAPACITY; ++i) {
                const SesPublished e = fake.published[i];
                if (e == SES_PUB_RECORDING_STARTED) {
                    CHECK(!active);
                    active = true;
                    ++started;
                }
                if (e == SES_PUB_RECORDING_COMPLETED) {
                    CHECK(active && fake.calls[CALL_FINALIZE] == finalizes + 1u &&
                          fake.last_finalize_result);
                    active = false;
                    ++ended;
                }
                if (e == SES_PUB_RECORDING_ABORTED && active) {
                    CHECK(fake.calls[CALL_FINALIZE] == finalizes + 1u);
                    active = false;
                    ++ended;
                }
            }
            if (fake.published_count >= LOG_CAPACITY - 8u) {
                fake.published_count = 0;
            }
            if (Session_IsActive() != active) {
                printf("FAIL %s: run %d step %d: machine active=%d, expected %d\n",
                       current_test, run, step, Session_IsActive(), active);
                ++failures;
                return;
            }
        }
        CHECK(started == ended + (active ? 1u : 0u));
    }
}

#define RUN(test)              \
    do {                       \
        current_test = #test;  \
        test();                \
    } while (0)

int main(void)
{
    RUN(starting_opens_the_file_starts_capture_and_records);
    RUN(the_start_guard_is_evaluated_once_per_start);
    RUN(a_start_that_cannot_start_is_rejected_without_touching_the_card);
    RUN(a_file_that_cannot_be_opened_rejects_the_start);
    RUN(capture_that_does_not_start_aborts_and_finalizes);
    RUN(stop_while_idle_is_ignored);
    RUN(start_while_active_is_rejected_and_changes_nothing);
    RUN(stop_requests_finalizing_at_the_next_block);
    RUN(repeated_stop_is_acknowledged_once_more_without_a_second_request);
    RUN(blocks_below_the_target_keep_recording);
    RUN(reaching_the_target_completes_the_session);
    RUN(a_target_reached_with_a_failed_finalize_is_an_abort);
    RUN(the_next_block_after_a_stop_completes_the_session);
    RUN(a_failed_finalize_after_a_stop_is_an_abort);
    RUN(a_capture_fault_aborts_from_every_active_state);
    RUN(idle_ignores_blocks_and_faults);
    RUN(a_new_session_can_start_after_each_ending);
    RUN(outcomes_are_never_shown_as_success_after_a_failure);
    if (failures != 0) {
        printf("session_test: %d failure(s)\n", failures);
        return 1;
    }
    puts("session_test: all scenarios passed");
    return 0;
}
