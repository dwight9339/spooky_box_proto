/*
 * Host tests for the Session shadow wiring (full_spooky_proto-8lw.14): reports made
 * the way the recorder makes them pass through the real M7 event queue and
 * dispatcher into the Session machine, and the disagreement check fires exactly
 * when the machine and the recorder differ. The recorder and the diagnostics
 * history are stubs. Host results only; not hardware evidence.
 */

#include <stdio.h>
#include <string.h>

#include "app_dispatch.h"
#include "app_events.h"
#include "diagnostics.h"
#include "radio_recorder.h"
#include "session_port.h"
#include "session_shadow.h"

/* --- Stubs --------------------------------------------------------------------- */

static uint32_t now_ms;
static bool recorder_active;
static unsigned mismatch_records;
static uint32_t last_mismatch_state;
static uint32_t last_mismatch_recorder;

uint32_t HAL_GetTick(void) { return now_ms; }
bool RadioRecorder_IsActive(void) { return recorder_active; }

void Diagnostics_Record(DiagEventType type, uint32_t arg0, uint32_t arg1)
{
    if (type == DIAG_SESSION_MISMATCH) {
        ++mismatch_records;
        last_mismatch_state = arg0;
        last_mismatch_recorder = arg1;
    }
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
    recorder_active = false;
    mismatch_records = 0;
    AppDispatch_Init();
    AppDispatch_Service(); /* drain anything left by an earlier test */
    mismatch_records = 0;
}

/* One foreground loop pass: producers ran, then the dispatcher. */
static void pass(void)
{
    now_ms += 5u;
    AppDispatch_Service();
}

static void recorder_starts(void)
{
    recorder_active = true;
    SessionShadow_ReportStart(60u, SESSION_SHADOW_STARTED, false);
}

/* --- Scenarios ----------------------------------------------------------------- */

static void a_recording_that_reaches_its_target_agrees_throughout(void)
{
    reset();
    recorder_starts();
    pass();
    CHECK(Session_GetState() == SES_STATE_RECORDING);
    for (int i = 0; i < 20; ++i) {
        SessionShadow_ReportBlockWritten(false, false);
        pass();
    }
    CHECK(Session_GetState() == SES_STATE_RECORDING);
    recorder_active = false;
    SessionShadow_ReportBlockWritten(true, true);
    pass();
    CHECK(Session_GetState() == SES_STATE_IDLE);
    CHECK(mismatch_records == 0);
}

static void a_stopped_recording_agrees_through_finalizing(void)
{
    reset();
    recorder_starts();
    pass();
    SessionShadow_ReportStop(); /* RECORD STOP: the recorder stays active */
    pass();
    CHECK(Session_GetState() == SES_STATE_FINALIZING);
    SessionShadow_ReportStop(); /* repeated stop */
    pass();
    CHECK(Session_GetState() == SES_STATE_FINALIZING);
    recorder_active = false;
    SessionShadow_ReportBlockWritten(false, true); /* stop, target not reached */
    pass();
    CHECK(Session_GetState() == SES_STATE_IDLE);
    CHECK(mismatch_records == 0);
}

static void a_stop_and_the_finishing_block_in_one_pass_stay_in_order(void)
{
    reset();
    recorder_starts();
    pass();
    SessionShadow_ReportStop();
    recorder_active = false;
    SessionShadow_ReportBlockWritten(false, true);
    pass();
    CHECK(Session_GetState() == SES_STATE_IDLE);
    CHECK(mismatch_records == 0);
}

static void every_start_failure_leaves_both_idle(void)
{
    static const SessionShadowStart outcomes[] = {
        SESSION_SHADOW_REJECTED, SESSION_SHADOW_OPEN_FAILED, SESSION_SHADOW_CAPTURE_FAILED};
    for (unsigned i = 0; i < sizeof(outcomes) / sizeof(outcomes[0]); ++i) {
        for (int finalized = 0; finalized < 2; ++finalized) {
            reset();
            SessionShadow_ReportStart(60u, outcomes[i], finalized != 0);
            pass();
            CHECK(Session_GetState() == SES_STATE_IDLE);
            CHECK(mismatch_records == 0);
        }
    }
}

static void a_start_while_active_is_rejected_without_disagreement(void)
{
    reset();
    recorder_starts();
    pass();
    SessionShadow_ReportStart(30u, SESSION_SHADOW_REJECTED, false);
    pass();
    CHECK(Session_GetState() == SES_STATE_RECORDING);
    CHECK(mismatch_records == 0);
}

static void a_capture_fault_ends_the_session_in_both(void)
{
    for (int stopping = 0; stopping < 2; ++stopping) {
        reset();
        recorder_starts();
        pass();
        if (stopping) {
            SessionShadow_ReportStop();
            pass();
        }
        recorder_active = false;
        SessionShadow_ReportCaptureFault(true);
        pass();
        CHECK(Session_GetState() == SES_STATE_IDLE);
        CHECK(mismatch_records == 0);
    }
}

static void an_unreported_recorder_change_is_recorded_once(void)
{
    /* For example sleep ending a recording (full_spooky_proto-8lw.11). */
    reset();
    recorder_starts();
    pass();
    recorder_active = false; /* the recorder stopped without a report */
    pass();
    CHECK(mismatch_records == 1);
    CHECK(last_mismatch_state == SES_STATE_RECORDING && last_mismatch_recorder == 0);
    for (int i = 0; i < 10; ++i) {
        pass();
    }
    CHECK(mismatch_records == 1 && SessionShadow_Mismatches() >= 1);
    /* The disagreement ends when the machine catches up, and a new one is
     * recorded again. */
    SessionShadow_ReportCaptureFault(true);
    pass();
    CHECK(Session_GetState() == SES_STATE_IDLE);
    recorder_active = true; /* starts without a report */
    pass();
    CHECK(mismatch_records == 2 && last_mismatch_recorder == 1);
}

static void a_rejected_start_that_the_recorder_accepted_is_caught(void)
{
    reset();
    recorder_active = true;
    SessionShadow_ReportStart(60u, SESSION_SHADOW_REJECTED, false);
    pass();
    CHECK(mismatch_records == 1);
}

static void reports_are_internal_events_on_the_queue(void)
{
    reset();
    EvqStats before;
    AppEvents_GetStats(&before);
    recorder_starts();
    SessionShadow_ReportBlockWritten(false, false);
    SessionShadow_ReportStop();
    EvqStats queued;
    AppEvents_GetStats(&queued);
    CHECK(queued.count == 3 && queued.posted == before.posted + 3);
    pass();
    EvqStats after;
    AppEvents_GetStats(&after);
    CHECK(after.count == 0 && after.dispatched == before.dispatched + 3);
    CHECK(after.rejected[EVQ_CLASS_INTERNAL] == 0);
    CHECK(Session_GetState() == SES_STATE_FINALIZING && mismatch_records == 0);
}

#define RUN(test)              \
    do {                       \
        current_test = #test;  \
        test();                \
    } while (0)

int main(void)
{
    RUN(a_recording_that_reaches_its_target_agrees_throughout);
    RUN(a_stopped_recording_agrees_through_finalizing);
    RUN(a_stop_and_the_finishing_block_in_one_pass_stay_in_order);
    RUN(every_start_failure_leaves_both_idle);
    RUN(a_start_while_active_is_rejected_without_disagreement);
    RUN(a_capture_fault_ends_the_session_in_both);
    RUN(an_unreported_recorder_change_is_recorded_once);
    RUN(a_rejected_start_that_the_recorder_accepted_is_caught);
    RUN(reports_are_internal_events_on_the_queue);
    if (failures != 0) {
        printf("session_shadow_test: %d failure(s)\n", failures);
        return 1;
    }
    puts("session_shadow_test: all tests passed");
    return 0;
}
