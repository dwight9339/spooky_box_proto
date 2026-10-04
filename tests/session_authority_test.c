/*
 * Host tests for the authoritative Session wiring (full_spooky_proto-8lw.4).
 * Commands and recorder outcomes pass through the real M7 event queue and
 * dispatcher. Recorder operations and diagnostics are fakes. Host results only.
 */

#include <stdio.h>
#include <string.h>

#include "app_dispatch.h"
#include "app_events.h"
#include "classic_adapter.h"
#include "diagnostics.h"
#include "matrix_adapter.h"
#include "radio_adapter.h"
#include "radio_recorder.h"
#include "session_control.h"
#include "sm/session_port.h"

static uint32_t now_ms;
static bool recorder_active;
static bool can_start;
static bool open_ok;
static bool capture_ok;
static bool target_reached;
static bool finalize_ok;
static unsigned can_start_calls;
static unsigned open_calls;
static unsigned capture_calls;
static unsigned request_stop_calls;
static unsigned stop_capture_calls;
static unsigned finalize_calls;
static unsigned discard_calls;
static uint32_t last_seconds;
static bool last_radio_ready;
static SesPublished published[16];
static unsigned published_count;
static unsigned mismatch_records;

uint32_t HAL_GetTick(void) { return now_ms; }
bool RadioRecorder_IsActive(void) { return recorder_active; }

bool RadioRecorder_CanStart(uint32_t seconds, bool radio_ready)
{
    ++can_start_calls;
    last_seconds = seconds;
    last_radio_ready = radio_ready;
    return can_start;
}

/* Like the recorder, a prepared file counts as active until it is discarded or
 * capture ends. */
bool RadioRecorder_OpenFile(uint32_t seconds)
{
    ++open_calls;
    last_seconds = seconds;
    if (open_ok) recorder_active = true;
    return open_ok;
}

void RadioRecorder_DiscardFile(void)
{
    ++discard_calls;
    recorder_active = false;
}

bool RadioRecorder_StartCapture(void)
{
    ++capture_calls;
    if (capture_ok) recorder_active = true;
    return capture_ok;
}

void RadioRecorder_RequestStop(void) { ++request_stop_calls; }
bool RadioRecorder_TargetReached(void) { return target_reached; }
void RadioRecorder_StopCapture(void) { ++stop_capture_calls; }

bool RadioRecorder_FinalizeFile(void)
{
    ++finalize_calls;
    recorder_active = false;
    return finalize_ok;
}

void RadioRecorder_PublishSessionEvent(SesPublished event)
{
    if (published_count < sizeof(published) / sizeof(published[0])) {
        published[published_count++] = event;
    }
}

/* The matrix border takes the same events (54w.8). */
static unsigned matrix_events;

void MatrixAdapter_OnSessionEvent(SesPublished event)
{
    (void)event;
    ++matrix_events;
}

/* The Radio machine is routed by the same dispatcher; radio_test covers it. */
static unsigned radio_events;
void RadioAdapter_Init(void) {}
void RadioAdapter_Dispatch(const EvqEvent *event) { (void)event; ++radio_events; }

/* So is the Classic engine; classic_service_test covers it. */
static unsigned classic_events;
void ClassicAdapter_Init(void) {}
void ClassicAdapter_Dispatch(const EvqEvent *event) { (void)event; ++classic_events; }

void Diagnostics_Record(DiagEventType type, uint32_t arg0, uint32_t arg1)
{
    (void)arg0;
    (void)arg1;
    if (type == DIAG_SESSION_MISMATCH) ++mismatch_records;
}

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
    can_start = true;
    open_ok = true;
    capture_ok = true;
    target_reached = false;
    finalize_ok = true;
    can_start_calls = open_calls = capture_calls = 0;
    request_stop_calls = stop_capture_calls = finalize_calls = discard_calls = 0;
    last_seconds = 0;
    last_radio_ready = false;
    published_count = 0;
    matrix_events = 0;
    mismatch_records = 0;
    AppDispatch_Init();
    AppDispatch_Service();
    mismatch_records = 0;
}

static void pass(void)
{
    now_ms += 5u;
    AppDispatch_Service();
}

/* The recorder's preparation steps end with an internal PREPARED report. */
static void prepare(bool ok)
{
    CHECK(Session_GetState() == SES_STATE_PREPARING && recorder_active);
    SessionControl_ReportPrepared(ok);
    pass();
}

static void start_recording(void)
{
    CHECK(SessionControl_RequestStart(60u, true));
    CHECK(!recorder_active);
    pass();
    prepare(true);
    CHECK(Session_GetState() == SES_STATE_RECORDING);
    CHECK(recorder_active);
}

static void an_external_start_drives_the_recorder_through_the_machine(void)
{
    reset();
    start_recording();
    CHECK(can_start_calls == 1 && open_calls == 1 && capture_calls == 1);
    CHECK(last_seconds == 60u && last_radio_ready);
    CHECK(published_count == 2);
    CHECK(published[0] == SES_PUB_RECORDING_PREPARING);
    CHECK(published[1] == SES_PUB_RECORDING_STARTED);
    CHECK(mismatch_records == 0);
}

static void an_open_ended_start_reaches_the_recorder_as_zero(void)
{
    reset();
    CHECK(SessionControl_RequestStart(0u, true));
    pass();
    prepare(true);
    CHECK(Session_GetState() == SES_STATE_RECORDING && recorder_active);
    CHECK(last_seconds == 0u && last_radio_ready);
}

static void each_start_failure_has_one_explicit_outcome(void)
{
    reset();
    can_start = false;
    CHECK(SessionControl_RequestStart(30u, true));
    pass();
    CHECK(open_calls == 0 && capture_calls == 0 && finalize_calls == 0);
    CHECK(published_count == 1 && published[0] == SES_PUB_RECORDING_REJECTED);

    reset();
    open_ok = false;
    CHECK(SessionControl_RequestStart(30u, true));
    pass();
    CHECK(open_calls == 1 && capture_calls == 0 && finalize_calls == 0);
    CHECK(published_count == 1 && published[0] == SES_PUB_RECORDING_REJECTED);

    reset();
    CHECK(SessionControl_RequestStart(30u, true));
    pass();
    prepare(false);
    CHECK(discard_calls == 1 && capture_calls == 0 && finalize_calls == 0);
    CHECK(published_count == 2 && published[1] == SES_PUB_RECORDING_REJECTED);
    CHECK(Session_GetState() == SES_STATE_IDLE && !recorder_active);

    reset();
    capture_ok = false;
    CHECK(SessionControl_RequestStart(30u, true));
    pass();
    prepare(true);
    CHECK(capture_calls == 1 && finalize_calls == 1);
    CHECK(published_count == 2 && published[1] == SES_PUB_RECORDING_ABORTED);
    CHECK(Session_GetState() == SES_STATE_IDLE && !recorder_active);
    CHECK(mismatch_records == 0);
    /* The matrix border receives every published event too. */
    CHECK(matrix_events == published_count);
}

static void stop_while_preparing_discards_the_file(void)
{
    reset();
    CHECK(SessionControl_RequestStart(0u, true));
    pass();
    CHECK(Session_GetState() == SES_STATE_PREPARING && recorder_active);
    CHECK(SessionControl_RequestStop());
    pass();
    CHECK(Session_GetState() == SES_STATE_IDLE && !recorder_active);
    CHECK(discard_calls == 1 && request_stop_calls == 0 && finalize_calls == 0);
    CHECK(published[published_count - 1] == SES_PUB_RECORDING_CANCELLED);
    CHECK(mismatch_records == 0);
}

static void stop_waits_for_the_next_matched_block_then_finalizes(void)
{
    reset();
    start_recording();
    CHECK(SessionControl_RequestStop());
    pass();
    CHECK(Session_GetState() == SES_STATE_FINALIZING);
    CHECK(request_stop_calls == 1 && recorder_active);
    CHECK(published[published_count - 1] == SES_PUB_RECORDING_STOPPING);
    SessionControl_ReportBlockWritten();
    pass();
    CHECK(Session_GetState() == SES_STATE_IDLE && !recorder_active);
    CHECK(stop_capture_calls == 1 && finalize_calls == 1);
    CHECK(published[published_count - 1] == SES_PUB_RECORDING_COMPLETED);
}

static void reaching_the_target_and_capture_faults_are_authoritative(void)
{
    reset();
    start_recording();
    target_reached = true;
    SessionControl_ReportBlockWritten();
    pass();
    CHECK(Session_GetState() == SES_STATE_IDLE && finalize_calls == 1);
    CHECK(published[published_count - 1] == SES_PUB_RECORDING_COMPLETED);

    reset();
    start_recording();
    finalize_ok = false;
    SessionControl_ReportCaptureFault();
    pass();
    CHECK(Session_GetState() == SES_STATE_IDLE && finalize_calls == 1);
    CHECK(published[published_count - 1] == SES_PUB_RECORDING_ABORTED);
}

static void storage_limit_outcomes_cross_the_authoritative_queue(void)
{
    reset();
    start_recording();
    SessionControl_ReportCardFull();
    pass();
    CHECK(Session_GetState() == SES_STATE_IDLE && finalize_calls == 1);
    CHECK(published[published_count - 1] == SES_PUB_RECORDING_CARD_FULL);

    reset();
    start_recording();
    SessionControl_ReportFileLimit();
    pass();
    CHECK(Session_GetState() == SES_STATE_IDLE && finalize_calls == 1);
    CHECK(published[published_count - 1] == SES_PUB_RECORDING_FILE_LIMIT);
}

static void commands_are_external_and_recorder_reports_are_internal(void)
{
    EvqStats before;
    EvqStats queued;
    EvqStats after;
    reset();
    AppEvents_GetStats(&before);
    CHECK(SessionControl_RequestStart(10u, true));
    SessionControl_ReportBlockWritten();
    AppEvents_GetStats(&queued);
    CHECK(queued.count == 2 && queued.posted == before.posted + 2);
    pass();
    AppEvents_GetStats(&after);
    CHECK(after.count == 0 && after.dispatched == before.dispatched + 2);
    CHECK(after.rejected[EVQ_CLASS_EXTERNAL_COMMAND] == 0);
    CHECK(after.rejected[EVQ_CLASS_INTERNAL] == 0);
}

static void an_unreported_recorder_change_is_still_a_fault(void)
{
    reset();
    start_recording();
    recorder_active = false;
    pass();
    CHECK(mismatch_records == 1);
    pass();
    CHECK(mismatch_records == 1);
}

#define RUN(test)              \
    do {                       \
        current_test = #test;  \
        test();                \
    } while (0)

int main(void)
{
    RUN(an_external_start_drives_the_recorder_through_the_machine);
    RUN(an_open_ended_start_reaches_the_recorder_as_zero);
    RUN(each_start_failure_has_one_explicit_outcome);
    RUN(stop_while_preparing_discards_the_file);
    RUN(stop_waits_for_the_next_matched_block_then_finalizes);
    RUN(reaching_the_target_and_capture_faults_are_authoritative);
    RUN(storage_limit_outcomes_cross_the_authoritative_queue);
    RUN(commands_are_external_and_recorder_reports_are_internal);
    RUN(an_unreported_recorder_change_is_still_a_fault);
    if (failures != 0) {
        printf("session_authority_test: %d failure(s)\n", failures);
        return 1;
    }
    puts("session_authority_test: all tests passed");
    return 0;
}
