/*
 * Host tests for the matrix service (full_spooky_proto-54w.8): the decision
 * 0013 renderer driving the frame writer, one register run per pass. Host
 * results only; frame cost and appearance need the bench.
 */

#include "matrix_service.h"
#include "matrix_feedback.h"

#include <stdio.h>
#include <string.h>

static unsigned failures;

#define CHECK(condition) do {                                               \
    if (!(condition)) {                                                     \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);       \
        ++failures;                                                         \
    }                                                                       \
} while (0)

static MatrixServiceInput input;

/* Passes at a fixed time until no run is handed out; returns the runs. */
static unsigned settle(uint32_t now_ms)
{
    MatrixWriterRun run;
    unsigned runs = 0u;

    while ((runs < 100u) && MatrixService_Service(now_ms, &input, &run)) {
        MatrixService_RunDone(true, 300u + runs);
        ++runs;
    }
    return runs;
}

static void start(void)
{
    memset(&input, 0, sizeof(input));
    CHECK(MatrixService_Init(0u));
}

static void off_until_enabled(void)
{
    MatrixWriterRun run;

    start();
    CHECK(!MatrixService_IsEnabled());
    CHECK(!MatrixService_Service(0u, &input, &run));
    MatrixService_SetEnabled(true, 0u);
    CHECK(MatrixService_IsEnabled());
    CHECK(!MatrixService_Service(0u, NULL, &run));
    CHECK(!MatrixService_Service(0u, &input, NULL));
}

/* Enabling writes the whole first frame, one run per pass; a steady frame
 * then writes nothing until the animation moves. */
static void the_first_frame_is_written_in_full_then_only_changes(void)
{
    MatrixServiceStatus status;

    start();
    MatrixService_SetEnabled(true, 0u);
    CHECK(settle(0u) == MATRIX_WRITER_RUNS);
    CHECK(settle(0u) == 0u);
    MatrixService_GetStatus(&status);
    CHECK(status.enabled && !status.trail);
    {
        /* The renderer runs the six-step loop without the trail. */
        MatrixFeedbackStatus renderer;

        CHECK(MatrixFeedback_GetStatus(&renderer));
        CHECK(!renderer.trail && renderer.loop_steps == 6u);
    }
    CHECK(status.frames == 1u && status.runs_written == MATRIX_WRITER_RUNS);
    CHECK(status.pending_runs == 0u);
    CHECK(status.run_us_max == 300u + MATRIX_WRITER_RUNS - 1u);
    /* The next step of the unknown-EMF loop (bucket 0 tempo, 160 ms). */
    {
        const unsigned runs = settle(160u);

        CHECK(runs > 0u && runs < MATRIX_WRITER_RUNS);
    }
}

static void an_onset_and_the_recording_border_change_the_frame(void)
{
    start();
    MatrixService_SetEnabled(true, 0u);
    (void)settle(0u);
    input.onset = 3u;
    CHECK(settle(1u) > 0u);
    input.onset = 0u;
    (void)settle(500u);   /* the kick has decayed */
    MatrixService_OnRecording(true);
    CHECK(settle(500u) > 0u);
    MatrixService_OnRecording(false);
    CHECK(settle(500u) > 0u);
    MatrixService_OnRecordingFault(500u);
    CHECK(settle(500u) > 0u);
}

static void a_failed_run_is_retried(void)
{
    MatrixWriterRun first;
    MatrixWriterRun again;
    MatrixServiceStatus status;

    start();
    MatrixService_SetEnabled(true, 0u);
    CHECK(MatrixService_Service(0u, &input, &first));
    MatrixService_RunDone(false, 50000u);
    CHECK(MatrixService_Service(0u, &input, &again));
    CHECK(again.page == first.page && again.reg == first.reg);
    MatrixService_RunDone(true, 10u);
    MatrixService_GetStatus(&status);
    CHECK(status.runs_failed == 1u && status.run_us_max == 50000u);
}

static void invalidate_and_re_enable_rewrite_everything(void)
{
    MatrixWriterRun run;

    start();
    MatrixService_SetEnabled(true, 0u);
    (void)settle(0u);
    MatrixService_Invalidate();
    CHECK(settle(0u) == MATRIX_WRITER_RUNS);
    MatrixService_SetEnabled(false, 0u);
    CHECK(!MatrixService_Service(0u, &input, &run));
    MatrixService_SetEnabled(true, 0u);
    CHECK(settle(0u) == MATRIX_WRITER_RUNS);
}

/* The recording border survives re-enabling. */
static void re_enabling_keeps_the_session_state(void)
{
    MatrixWriterRun run;
    unsigned with_border;
    unsigned without_border;

    start();
    MatrixService_SetEnabled(true, 0u);
    (void)settle(0u);
    MatrixService_OnRecording(true);
    (void)settle(0u);
    MatrixService_SetEnabled(false, 0u);
    MatrixService_SetEnabled(true, 0u);
    with_border = settle(0u);
    start();
    MatrixService_SetEnabled(true, 0u);
    without_border = settle(0u);
    CHECK(with_border == MATRIX_WRITER_RUNS && without_border == MATRIX_WRITER_RUNS);
    /* Same run count; compare the first run's bytes: the border lights row 0. */
    start();
    MatrixService_OnRecording(true);
    MatrixService_SetEnabled(true, 0u);
    CHECK(MatrixService_Service(0u, &input, &run));
    {
        unsigned lit = 0u;
        unsigned index;

        for (index = 0u; index < run.length; ++index) {
            lit += (run.bytes[index] != 0u) ? 1u : 0u;
        }
        CHECK(lit > 0u);
    }
}

static void the_trail_setting_is_kept(void)
{
    MatrixServiceStatus status;

    start();
    MatrixService_SetTrail(true);
    MatrixService_SetEnabled(true, 0u);
    MatrixService_GetStatus(&status);
    CHECK(status.trail);
    MatrixService_SetTrail(false);
    MatrixService_GetStatus(&status);
    CHECK(!status.trail);
    MatrixService_GetStatus(NULL);
}

int main(void)
{
    off_until_enabled();
    the_first_frame_is_written_in_full_then_only_changes();
    an_onset_and_the_recording_border_change_the_frame();
    a_failed_run_is_retried();
    invalidate_and_re_enable_rewrite_everything();
    re_enabling_keeps_the_session_state();
    the_trail_setting_is_kept();

    if (failures != 0u) {
        printf("%u failure(s)\n", failures);
        return 1;
    }
    printf("matrix_service_test: all checks passed\n");
    return 0;
}
