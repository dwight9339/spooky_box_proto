/* Demo-only tempo matrix for the Slicer (CM7/App/tempo_matrix.c,
 * full_spooky_proto-p04.15, decision 0020 item 13). */
#include "tempo_matrix.h"

#include <stdio.h>
#include <string.h>

static unsigned failures;

#define CHECK(condition) do {                                               \
    if (!(condition)) {                                                     \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);       \
        ++failures;                                                         \
    }                                                                       \
} while (0)

static int same(MatrixFeedbackRgb a, MatrixFeedbackRgb b)
{
    return a.red == b.red && a.green == b.green && a.blue == b.blue;
}

static int distance(MatrixFeedbackRgb a, MatrixFeedbackRgb b)
{
    const int red = (int)a.red - (int)b.red;
    const int green = (int)a.green - (int)b.green;
    const int blue = (int)a.blue - (int)b.blue;

    return (red < 0 ? -red : red) + (green < 0 ? -green : green) + (blue < 0 ? -blue : blue);
}

/* The six corners of the hue circle, and a smooth turn through them that ends
 * where it began: one cycle per bar. */
static void test_hue_circle(void)
{
    const MatrixFeedbackRgb red = {TEMPO_MATRIX_LEVEL, 0u, 0u};
    MatrixFeedbackRgb previous = TempoMatrix_Hue(0u);
    int largest = 0;

    CHECK(same(TempoMatrix_Hue(0u), red));
    CHECK(TempoMatrix_Hue(10923u).red == TEMPO_MATRIX_LEVEL &&
          TempoMatrix_Hue(10923u).green >= TEMPO_MATRIX_LEVEL - 1u);          /* yellow */
    CHECK(TempoMatrix_Hue(21846u).green == TEMPO_MATRIX_LEVEL &&
          TempoMatrix_Hue(21846u).red <= 1u && TempoMatrix_Hue(21846u).blue == 0u); /* green */
    CHECK(TempoMatrix_Hue(32768u).green == TEMPO_MATRIX_LEVEL &&
          TempoMatrix_Hue(32768u).blue == TEMPO_MATRIX_LEVEL);                 /* cyan */
    CHECK(TempoMatrix_Hue(43691u).blue == TEMPO_MATRIX_LEVEL &&
          TempoMatrix_Hue(43691u).red <= 1u && TempoMatrix_Hue(43691u).green == 0u); /* blue */
    CHECK(TempoMatrix_Hue(54614u).blue == TEMPO_MATRIX_LEVEL &&
          TempoMatrix_Hue(54614u).red >= TEMPO_MATRIX_LEVEL - 1u);           /* magenta */
    for (uint32_t phase = 0u; phase <= 65535u; phase += 64u) {
        const MatrixFeedbackRgb colour = TempoMatrix_Hue((uint16_t)phase);
        const int step = distance(colour, previous);

        largest = (step > largest) ? step : largest;
        CHECK(colour.red <= TEMPO_MATRIX_LEVEL && colour.green <= TEMPO_MATRIX_LEVEL &&
              colour.blue <= TEMPO_MATRIX_LEVEL);
        previous = colour;
    }
    CHECK(largest <= 2);
    CHECK(distance(TempoMatrix_Hue(65535u), red) <= 1);
}

static void test_compose(void)
{
    MatrixFeedbackFrame frame;
    MatrixFeedbackConfig config;
    TempoMatrixInput input;

    memset(&input, 0, sizeof(input));
    memset(&frame, 0xA5, sizeof(frame));
    TempoMatrix_Compose(&input, &frame); /* no clip: dark */
    for (unsigned y = 0u; y < MATRIX_FEEDBACK_SIZE; ++y) {
        for (unsigned x = 0u; x < MATRIX_FEEDBACK_SIZE; ++x) {
            CHECK(frame.pixels[y][x].red == 0u && frame.pixels[y][x].green == 0u &&
                  frame.pixels[y][x].blue == 0u);
        }
    }
    input.clip_ready = true;
    input.bar_phase = 32768u;
    TempoMatrix_Compose(&input, &frame); /* one solid colour */
    for (unsigned y = 0u; y < MATRIX_FEEDBACK_SIZE; ++y) {
        for (unsigned x = 0u; x < MATRIX_FEEDBACK_SIZE; ++x) {
            CHECK(same(frame.pixels[y][x], TempoMatrix_Hue(32768u)));
        }
    }
    input.recording = true;
    TempoMatrix_Compose(&input, &frame);
    MatrixFeedback_DefaultConfig(&config);
    CHECK(same(frame.pixels[0][4], config.recording_colour));
    CHECK(same(frame.pixels[4][8], config.recording_colour));
    CHECK(same(frame.pixels[4][4], TempoMatrix_Hue(32768u)));
    TempoMatrix_Compose(NULL, &frame);
    CHECK(frame.pixels[4][4].red == 0u && frame.pixels[4][4].green == 0u);
}

int main(void)
{
    test_hue_circle();
    test_compose();
    if (failures != 0u) {
        printf("%u failure(s)\n", failures);
        return 1;
    }
    printf("tempo_matrix_test: all passed\n");
    return 0;
}
