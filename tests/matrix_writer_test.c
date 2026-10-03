/*
 * Host tests for the matrix frame writer (full_spooky_proto-54w.8): the
 * IS31FL3741 register layout of the 9x9 window and the row-run diffing. Host
 * results only; how the matrix looks needs the bench.
 */

#include "matrix_writer.h"

#include <stdio.h>
#include <string.h>

static unsigned failures;

#define CHECK(condition) do {                                               \
    if (!(condition)) {                                                     \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);       \
        ++failures;                                                         \
    }                                                                       \
} while (0)

/* The per-pixel arithmetic of UiMatrixSetPixel as proven on the bench by
 * UI MATRIX ANIMATE before the writer existed, kept as the reference. Its
 * (x, y) are panel coordinates: the matrix is mounted rotated 180 degrees,
 * so logical (x, y) is panel (8 - x, 8 - y). */
static void reference_pixel(uint8_t x, uint8_t y, MatrixFeedbackRgb c,
                            uint8_t *page, uint8_t *reg, uint8_t bytes[3])
{
    static const uint8_t row_map[9] = {8u, 5u, 4u, 3u, 2u, 1u, 0u, 7u, 6u};
    const uint8_t px = (uint8_t)(x + 2u);
    const uint8_t my = row_map[y];
    const uint16_t offset = (uint16_t)(px + ((px < 10u) ? (my * 10u) : (80u + my * 3u))) * 3u;

    if ((px & 1u) != 0u) {
        bytes[0] = c.green; bytes[1] = c.red; bytes[2] = c.blue;
    } else {
        bytes[0] = c.blue; bytes[1] = c.green; bytes[2] = c.red;
    }
    *page = (offset < 180u) ? 0u : 1u;
    *reg = (uint8_t)((offset < 180u) ? offset : offset - 180u);
}

static MatrixFeedbackRgb colour_of(uint8_t x, uint8_t y)
{
    MatrixFeedbackRgb c;

    c.red = (uint8_t)(10u + x);
    c.green = (uint8_t)(100u + y);
    c.blue = (uint8_t)(200u + x + y);
    return c;
}

static void fill(MatrixFeedbackFrame *frame)
{
    uint8_t x;
    uint8_t y;

    for (y = 0u; y < 9u; ++y) {
        for (x = 0u; x < 9u; ++x) {
            frame->pixels[y][x] = colour_of(x, y);
        }
    }
}

/* Writes every pending run to a model of the PWM registers. */
static unsigned drain(uint8_t pwm[2][256])
{
    MatrixWriterRun run;
    unsigned runs = 0u;

    while (MatrixWriter_Next(&run) && (runs < 100u)) {
        CHECK(run.page <= 1u);
        CHECK((unsigned)run.reg + run.length <= (run.page == 0u ? 180u : 171u));
        memcpy(&pwm[run.page][run.reg], run.bytes, run.length);
        MatrixWriter_Done(true);
        ++runs;
    }
    return runs;
}

static void each_pixel_matches_the_proven_mapping(void)
{
    uint8_t x;
    uint8_t y;

    for (y = 0u; y < 9u; ++y) {
        for (x = 0u; x < 9u; ++x) {
            MatrixWriterRun run;
            uint8_t page;
            uint8_t reg;
            uint8_t bytes[3];

            reference_pixel((uint8_t)(8u - x), (uint8_t)(8u - y), colour_of(x, y),
                            &page, &reg, bytes);
            CHECK(MatrixWriter_PixelRun(x, y, colour_of(x, y), &run));
            CHECK(run.page == page && run.reg == reg && run.length == 3u);
            CHECK(memcmp(run.bytes, bytes, 3u) == 0);
        }
    }
    {
        MatrixWriterRun run;

        CHECK(!MatrixWriter_PixelRun(9u, 0u, colour_of(0u, 0u), &run));
        CHECK(!MatrixWriter_PixelRun(0u, 9u, colour_of(0u, 0u), &run));
        CHECK(!MatrixWriter_PixelRun(0u, 0u, colour_of(0u, 0u), NULL));
    }
}

/* A full frame written as row runs leaves the registers exactly as 81 single
 * pixel writes would. */
static void runs_write_the_same_registers_as_pixels(void)
{
    static uint8_t by_runs[2][256];
    static uint8_t by_pixels[2][256];
    MatrixFeedbackFrame frame;
    MatrixWriterStatus status;
    uint8_t x;
    uint8_t y;

    memset(by_runs, 0, sizeof(by_runs));
    memset(by_pixels, 0, sizeof(by_pixels));
    fill(&frame);
    for (y = 0u; y < 9u; ++y) {
        for (x = 0u; x < 9u; ++x) {
            uint8_t page;
            uint8_t reg;
            uint8_t bytes[3];

            reference_pixel((uint8_t)(8u - x), (uint8_t)(8u - y), frame.pixels[y][x],
                            &page, &reg, bytes);
            memcpy(&by_pixels[page][reg], bytes, 3u);
        }
    }
    MatrixWriter_Init();
    MatrixWriter_SetTarget(&frame);
    MatrixWriter_GetStatus(&status);
    CHECK(status.pending_runs == MATRIX_WRITER_RUNS);
    CHECK(drain(by_runs) == MATRIX_WRITER_RUNS);
    CHECK(memcmp(by_runs, by_pixels, sizeof(by_runs)) == 0);
    MatrixWriter_GetStatus(&status);
    CHECK(status.pending_runs == 0u && status.runs_written == MATRIX_WRITER_RUNS);
}

static void only_changed_runs_are_written(void)
{
    static uint8_t pwm[2][256];
    MatrixFeedbackFrame frame;
    MatrixWriterRun run;

    fill(&frame);
    MatrixWriter_Init();
    MatrixWriter_SetTarget(&frame);
    (void)drain(pwm);
    /* The same frame again: nothing to write. */
    MatrixWriter_SetTarget(&frame);
    CHECK(!MatrixWriter_Next(&run));
    /* One pixel in columns 0 to 7 of row 3: its 24-byte run only. */
    frame.pixels[3][5].red ^= 0xFFu;
    MatrixWriter_SetTarget(&frame);
    CHECK(MatrixWriter_Next(&run) && run.length == 24u);
    MatrixWriter_Done(true);
    CHECK(!MatrixWriter_Next(&run));
    /* Logical column 0 of row 7 is panel column 8 of row 1: its 3-byte run. */
    frame.pixels[7][0].blue ^= 0xFFu;
    MatrixWriter_SetTarget(&frame);
    CHECK(MatrixWriter_Next(&run) && run.length == 3u && run.page == 1u);
    MatrixWriter_Done(true);
    CHECK(drain(pwm) == 0u);
}

static void a_failed_run_stays_pending(void)
{
    static uint8_t pwm[2][256];
    MatrixFeedbackFrame frame;
    MatrixWriterRun first;
    MatrixWriterRun again;
    MatrixWriterStatus status;

    fill(&frame);
    MatrixWriter_Init();
    MatrixWriter_SetTarget(&frame);
    CHECK(MatrixWriter_Next(&first));
    MatrixWriter_Done(false);
    CHECK(MatrixWriter_Next(&again));
    CHECK(again.page == first.page && again.reg == first.reg);
    MatrixWriter_GetStatus(&status);
    CHECK(status.runs_failed == 1u && status.pending_runs == MATRIX_WRITER_RUNS);
    MatrixWriter_Done(true);
    CHECK(drain(pwm) == MATRIX_WRITER_RUNS - 1u);
    /* A Done with nothing handed out changes nothing. */
    MatrixWriter_Done(false);
    MatrixWriter_GetStatus(&status);
    CHECK(status.runs_failed == 1u);
}

/* A new target before the old one is written replaces it: runs that now
 * match the matrix are dropped, and the old frame's runs are never written. */
static void a_new_target_supersedes_the_old(void)
{
    static uint8_t pwm[2][256];
    MatrixFeedbackFrame shown;
    MatrixFeedbackFrame next;
    MatrixWriterRun run;
    MatrixWriterStatus status;

    fill(&shown);
    MatrixWriter_Init();
    MatrixWriter_SetTarget(&shown);
    (void)drain(pwm);
    next = shown;
    next.pixels[0][1].red ^= 0xFFu;
    next.pixels[8][1].red ^= 0xFFu;
    MatrixWriter_SetTarget(&next);
    CHECK(MatrixWriter_Next(&run));
    /* Logical row 8 is panel row 0, written first; logical row 0 pending. */
    MatrixWriter_Done(true);
    MatrixWriter_SetTarget(&shown);    /* row 0 back to what the matrix shows */
    MatrixWriter_GetStatus(&status);
    CHECK(status.targets_superseded == 1u);
    CHECK(status.pending_runs == 1u);  /* only row 8, to restore it */
    CHECK(MatrixWriter_Next(&run) && run.length == 24u);
    MatrixWriter_SetTarget(NULL);
}

int main(void)
{
    each_pixel_matches_the_proven_mapping();
    runs_write_the_same_registers_as_pixels();
    only_changed_runs_are_written();
    a_failed_run_stays_pending();
    a_new_target_supersedes_the_old();

    if (failures != 0u) {
        printf("%u failure(s)\n", failures);
        return 1;
    }
    printf("matrix_writer_test: all checks passed\n");
    return 0;
}
