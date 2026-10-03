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

static MatrixFeedbackConfig config;
static MatrixFeedbackFrame frame;

static const MatrixFeedbackRgb black = {0u, 0u, 0u};
/* A visible stand-in for the unknown colour, so the trail and geometry
 * checks can tell it apart; the default drive value is checked separately. */
static const MatrixFeedbackRgb grey = {55u, 58u, 62u};
static const MatrixFeedbackRgb cyan = {0u, 175u, 230u};
static const MatrixFeedbackRgb green = {0u, 255u, 0u};
static const MatrixFeedbackRgb red = {255u, 30u, 20u};
static const MatrixFeedbackRgb yellow = {120u, 240u, 10u};

static bool same(MatrixFeedbackRgb a, MatrixFeedbackRgb b)
{
    return (a.red == b.red) && (a.green == b.green) && (a.blue == b.blue);
}

static MatrixFeedbackRgb pct(MatrixFeedbackRgb c, unsigned percent)
{
    MatrixFeedbackRgb s;

    s.red = (uint8_t)((c.red * percent) / 100u);
    s.green = (uint8_t)((c.green * percent) / 100u);
    s.blue = (uint8_t)((c.blue * percent) / 100u);
    return s;
}

static unsigned count(MatrixFeedbackRgb colour)
{
    unsigned x;
    unsigned y;
    unsigned n = 0u;

    for (y = 0u; y < MATRIX_FEEDBACK_SIZE; ++y) {
        for (x = 0u; x < MATRIX_FEEDBACK_SIZE; ++x) {
            n += same(frame.pixels[y][x], colour) ? 1u : 0u;
        }
    }
    return n;
}

static bool ring_is(MatrixFeedbackRgb colour)
{
    unsigned i;

    for (i = 0u; i < MATRIX_FEEDBACK_SIZE; ++i) {
        if (!same(frame.pixels[0][i], colour) ||
            !same(frame.pixels[MATRIX_FEEDBACK_SIZE - 1u][i], colour) ||
            !same(frame.pixels[i][0], colour) ||
            !same(frame.pixels[i][MATRIX_FEEDBACK_SIZE - 1u], colour)) {
            return false;
        }
    }
    return true;
}

static MatrixFeedbackStatus status_now(void)
{
    MatrixFeedbackStatus status;

    CHECK(MatrixFeedback_GetStatus(&status));
    return status;
}

static void start(uint32_t now)
{
    MatrixFeedback_DefaultConfig(&config);
    config.unknown_colour = grey;
    CHECK(MatrixFeedback_Init(&config, now));
}

/* Advances to step `target`, one period of the latched bucket at a time. */
static uint32_t step_to(uint32_t now, uint8_t target)
{
    unsigned guard = 0u;

    while ((status_now().step != target) && (guard++ < 20u)) {
        const MatrixFeedbackStatus s = status_now();
        now += config.emf_step_ms[s.emf_known ? s.emf_bucket : 0u];
        (void)MatrixFeedback_Advance(now);
    }
    CHECK(status_now().step == target);
    return now;
}

static void config_is_validated(void)
{
    static const MatrixFeedbackRgb bench_grey = {1u, 2u, 2u};

    MatrixFeedback_DefaultConfig(&config);
    CHECK(same(config.unknown_colour, bench_grey));
    CHECK(!MatrixFeedback_Init(NULL, 0u));
    MatrixFeedback_DefaultConfig(&config);
    config.emf_step_ms[3] = 0u;
    CHECK(!MatrixFeedback_Init(&config, 0u));
    MatrixFeedback_DefaultConfig(&config);
    config.trail_percent = 101u;
    CHECK(!MatrixFeedback_Init(&config, 0u));
    MatrixFeedback_DefaultConfig(&config);
    config.kick_px[1] = 0u;
    CHECK(!MatrixFeedback_Init(&config, 0u));
    MatrixFeedback_DefaultConfig(&config);
    config.kick_px[2] = 1u;
    CHECK(!MatrixFeedback_Init(&config, 0u));
    MatrixFeedback_DefaultConfig(&config);
    config.kick_px[2] = MATRIX_FEEDBACK_SIZE;
    CHECK(!MatrixFeedback_Init(&config, 0u));
    MatrixFeedback_DefaultConfig(&config);
    config.kick_decay_ms = 0u;
    CHECK(!MatrixFeedback_Init(&config, 0u));
}

static void loop_geometry_with_and_without_trail(void)
{
    uint32_t now = 0u;
    MatrixFeedbackStatus status;

    start(now);
    MatrixFeedback_SetEmf(true, 0u);
    status = status_now();
    CHECK(status.trail);
    CHECK(status.loop_steps == 7u);
    CHECK(!status.emf_known); /* not latched until step 0 */

    /* Step 0: the centre only. */
    CHECK(MatrixFeedback_Compose(&frame));
    CHECK(count(black) == 80u);
    CHECK(same(frame.pixels[4][4], grey));

    /* Step 2: radii 2 and 1 full (16 + 8), radius 0 at 35%. */
    now = step_to(now, 2u);
    CHECK(MatrixFeedback_Compose(&frame));
    CHECK(count(grey) == 24u);
    CHECK(same(frame.pixels[4][4], pct(grey, 35u)));
    CHECK(same(frame.pixels[2][2], grey));
    CHECK(same(frame.pixels[1][1], black));

    /* Step 6: the stroke has left the matrix; only the trail at radius 4. */
    now = step_to(now, 6u);
    CHECK(MatrixFeedback_Compose(&frame));
    CHECK(count(pct(grey, 35u)) == 32u);
    CHECK(count(black) == 49u);

    /* Wrap to step 0 latches the bucket set earlier. */
    now = step_to(now, 0u);
    CHECK(status_now().emf_known);
    CHECK(MatrixFeedback_Compose(&frame));
    CHECK(same(frame.pixels[4][4], cyan));

    /* Trail off: six steps, restarted at step 0. Step 4 lights radii 4
     * and 3; step 5 only the edge ring at radius 4. */
    now = step_to(now, 3u);
    MatrixFeedback_SetTrail(false);
    status = status_now();
    CHECK(status.step == 0u);
    CHECK(status.loop_steps == 6u);
    now = step_to(now, 4u);
    CHECK(MatrixFeedback_Compose(&frame));
    CHECK(count(cyan) == 32u + 24u);
    now = step_to(now, 5u);
    CHECK(MatrixFeedback_Compose(&frame));
    CHECK(count(cyan) == 32u);
    CHECK(count(black) == 49u);
    now = step_to(now, 0u);
    CHECK(status_now().step == 0u);
}

static void emf_colour_latches_at_step_zero_and_unknown_is_immediate(void)
{
    uint32_t now = 0u;

    start(now);
    MatrixFeedback_SetEmf(true, 1u);
    now = step_to(now, 1u);
    now = step_to(now, 0u);
    CHECK(MatrixFeedback_Compose(&frame));
    CHECK(same(frame.pixels[4][4], green));

    /* A new bucket mid-sweep waits for the next loop. */
    now = step_to(now, 2u);
    MatrixFeedback_SetEmf(true, 3u);
    CHECK(MatrixFeedback_Compose(&frame));
    CHECK(count(green) == 24u);
    CHECK(count(red) == 0u);
    now = step_to(now, 0u);
    CHECK(status_now().emf_bucket == 3u);

    /* Bucket 3 runs at 70 ms per step. */
    CHECK(!MatrixFeedback_Advance(now + 69u));
    CHECK(MatrixFeedback_Advance(now + 70u));
    CHECK(status_now().step == 1u);
    now += 70u;

    /* Losing the measurement greys the outline at once... */
    MatrixFeedback_SetEmf(false, 0u);
    CHECK(MatrixFeedback_Advance(now));
    CHECK(MatrixFeedback_Compose(&frame));
    CHECK(count(red) == 0u);
    CHECK(count(grey) == 9u);
    /* ...and a measurement returning mid-sweep stays grey until step 0. */
    MatrixFeedback_SetEmf(true, 1u);
    CHECK(!status_now().emf_known);
    now = step_to(now, 2u);
    CHECK(MatrixFeedback_Compose(&frame));
    CHECK(count(grey) == 24u);
    now = step_to(now, 0u);
    CHECK(status_now().emf_known);
    CHECK(status_now().emf_bucket == 1u);
}

static void late_service_drops_steps_to_keep_tempo(void)
{
    uint32_t now = 0u;
    MatrixFeedbackStatus status;

    start(now);
    (void)MatrixFeedback_Advance(now);
    /* Three periods late: three steps advanced, two of them dropped. */
    now += 3u * 160u;
    CHECK(MatrixFeedback_Advance(now));
    status = status_now();
    CHECK(status.step == 3u);
    CHECK(status.dropped_steps == 2u);
    /* Far behind: bounded work, then resynchronised. */
    now += 100000u;
    CHECK(MatrixFeedback_Advance(now));
    CHECK(!MatrixFeedback_Advance(now + 159u));
    CHECK(MatrixFeedback_Advance(now + 160u));
}

static void kicks_shift_rows_clip_decay_and_alternate(void)
{
    uint32_t now = 0u;
    MatrixFeedbackStatus status;
    unsigned x;
    unsigned lit;

    start(now);
    MatrixFeedback_SetTrail(false);
    now = step_to(now, 1u); /* 3x3 block at the centre */

    MatrixFeedback_OnOnset(2u, now);
    status = status_now();
    CHECK(status.kick_px == 2u);
    CHECK(status.kick_direction == 1);
    CHECK(MatrixFeedback_Compose(&frame));
    /* Even row 4 moves right by 2 (x 5..7), odd row 3 moves left (x 1..3). */
    CHECK(same(frame.pixels[4][4], black));
    CHECK(same(frame.pixels[4][5], grey) && same(frame.pixels[4][7], grey));
    CHECK(same(frame.pixels[3][1], grey) && same(frame.pixels[3][3], grey));
    CHECK(same(frame.pixels[3][4], black));

    /* A smaller onset is ignored; an equal one restarts the other way. */
    MatrixFeedback_OnOnset(1u, now + 10u);
    CHECK(status_now().kick_px == 2u);
    CHECK(status_now().kick_direction == 1);
    MatrixFeedback_OnOnset(2u, now + 20u);
    CHECK(status_now().kick_direction == -1);
    now += 20u;
    CHECK(MatrixFeedback_Advance(now));

    /* One pixel per 80 ms. */
    CHECK(!MatrixFeedback_Advance(now + 79u));
    CHECK(MatrixFeedback_Advance(now + 80u));
    CHECK(status_now().kick_px == 1u);
    (void)MatrixFeedback_Advance(now + 160u);
    CHECK(status_now().kick_px == 0u);
    now += 160u;

    /* Clipping at the edge: step 4 lights radii 4 and 3; a large kick moves
     * row 4 right by 3, so its x 7 and 8 fall off and x 0..2 go dark. */
    now = step_to(now, 4u);
    MatrixFeedback_OnOnset(3u, now);
    CHECK(status_now().kick_direction == 1);
    CHECK(MatrixFeedback_Compose(&frame));
    lit = 0u;
    for (x = 0u; x < MATRIX_FEEDBACK_SIZE; ++x) {
        lit += same(frame.pixels[4][x], grey) ? 1u : 0u;
    }
    CHECK(lit == 2u);
    CHECK(same(frame.pixels[4][3], grey) && same(frame.pixels[4][4], grey));
    CHECK(same(frame.pixels[4][0], black));
    /* Odd row 5 moves left: its x 0, 1 (radius 4, 3) fall off. */
    CHECK(same(frame.pixels[5][4], grey) && same(frame.pixels[5][5], grey));
    CHECK(same(frame.pixels[5][8], black));
    CHECK(count(grey) < 56u);

    /* Out-of-range sizes are ignored. */
    MatrixFeedback_OnOnset(0u, now);
    MatrixFeedback_OnOnset(4u, now);
    CHECK(status_now().kick_px == 3u);
}

static void recording_border_blinks_off_during_kicks(void)
{
    uint32_t now = 1000u;

    start(now);
    MatrixFeedback_SetEmf(true, 0u);
    CHECK(MatrixFeedback_Compose(&frame));
    CHECK(status_now().border == MATRIX_BORDER_NONE);

    MatrixFeedback_SetRecording(true);
    CHECK(MatrixFeedback_Advance(now));
    CHECK(status_now().border == MATRIX_BORDER_RECORDING);
    CHECK(MatrixFeedback_Compose(&frame));
    CHECK(ring_is(yellow));

    /* Drawn over the animation: the edge ring at step 5 stays yellow. */
    now = step_to(now, 5u);
    CHECK(MatrixFeedback_Compose(&frame));
    CHECK(ring_is(yellow));

    MatrixFeedback_OnOnset(1u, now);
    CHECK(status_now().border == MATRIX_BORDER_RECORDING_KICK);
    CHECK(MatrixFeedback_Compose(&frame));
    CHECK(ring_is(black));
    (void)MatrixFeedback_Advance(now + 80u);
    CHECK(status_now().border == MATRIX_BORDER_RECORDING);

    /* A normal stop removes it. */
    MatrixFeedback_SetRecording(false);
    CHECK(status_now().border == MATRIX_BORDER_NONE);
}

static void fault_border_blinks_three_times_then_clears(void)
{
    uint32_t now = 5000u;
    const uint32_t fault = 5000u;

    start(now);
    MatrixFeedback_SetRecording(true);
    MatrixFeedback_OnRecordingFault(fault);
    CHECK(MatrixFeedback_Advance(fault));
    CHECK(status_now().border == MATRIX_BORDER_FAULT_ON);
    CHECK(MatrixFeedback_Compose(&frame));
    CHECK(ring_is(red));

    /* Onsets do not touch the fault border. */
    MatrixFeedback_OnOnset(3u, fault + 10u);
    CHECK(MatrixFeedback_Compose(&frame));
    CHECK(ring_is(red));

    (void)MatrixFeedback_Advance(fault + 249u);
    CHECK(status_now().border == MATRIX_BORDER_FAULT_ON);
    (void)MatrixFeedback_Advance(fault + 250u);
    CHECK(status_now().border == MATRIX_BORDER_FAULT_OFF);
    CHECK(MatrixFeedback_Compose(&frame));
    CHECK(ring_is(black));
    (void)MatrixFeedback_Advance(fault + 500u);
    CHECK(status_now().border == MATRIX_BORDER_FAULT_ON);
    (void)MatrixFeedback_Advance(fault + 750u);
    CHECK(status_now().border == MATRIX_BORDER_FAULT_OFF);
    (void)MatrixFeedback_Advance(fault + 1000u);
    CHECK(status_now().border == MATRIX_BORDER_FAULT_ON);
    (void)MatrixFeedback_Advance(fault + 1249u);
    CHECK(status_now().border == MATRIX_BORDER_FAULT_ON);
    CHECK(MatrixFeedback_Advance(fault + 1250u));
    CHECK(status_now().border == MATRIX_BORDER_NONE);

    /* A stop after the fault does not bring the border back; a new
     * recording does, and ends any blink still running. */
    MatrixFeedback_SetRecording(false);
    CHECK(status_now().border == MATRIX_BORDER_NONE);
    MatrixFeedback_OnRecordingFault(fault + 2000u);
    MatrixFeedback_SetRecording(true);
    CHECK(status_now().border == MATRIX_BORDER_RECORDING);
}

static void advance_reports_only_real_changes(void)
{
    uint32_t now = 0u;

    start(now);
    CHECK(MatrixFeedback_Advance(now)); /* first frame after init */
    CHECK(!MatrixFeedback_Advance(now + 1u));
    MatrixFeedback_SetEmf(true, 3u); /* latched later: no visible change */
    CHECK(!MatrixFeedback_Advance(now + 2u));
    MatrixFeedback_SetRecording(true);
    CHECK(MatrixFeedback_Advance(now + 3u));
    MatrixFeedback_SetRecording(true);
    CHECK(!MatrixFeedback_Advance(now + 4u));
}

int main(void)
{
    config_is_validated();
    loop_geometry_with_and_without_trail();
    emf_colour_latches_at_step_zero_and_unknown_is_immediate();
    late_service_drops_steps_to_keep_tempo();
    kicks_shift_rows_clip_decay_and_alternate();
    recording_border_blinks_off_during_kicks();
    fault_border_blinks_three_times_then_clears();
    advance_reports_only_real_changes();
    if (failures != 0u) {
        printf("%u failure(s)\n", failures);
        return 1;
    }
    printf("matrix_feedback_test: pass\n");
    return 0;
}
