/* Demo-only matrix grain view (grain_matrix.c, full_spooky_proto-p04.7). */
#include "grain_matrix.h"

#include <stdio.h>
#include <string.h>

static unsigned failures;

#define CHECK(condition) do {                                               \
    if (!(condition)) {                                                     \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);       \
        ++failures;                                                         \
    }                                                                       \
} while (0)

static MatrixFeedbackFrame frame;

static bool dark(MatrixFeedbackRgb pixel)
{
    return (pixel.red == 0u) && (pixel.green == 0u) && (pixel.blue == 0u);
}

static bool same(MatrixFeedbackRgb a, MatrixFeedbackRgb b)
{
    return (a.red == b.red) && (a.green == b.green) && (a.blue == b.blue);
}

static unsigned lit(void)
{
    unsigned count = 0u;

    for (unsigned y = 0u; y < MATRIX_FEEDBACK_SIZE; ++y) {
        for (unsigned x = 0u; x < MATRIX_FEEDBACK_SIZE; ++x) {
            count += dark(frame.pixels[y][x]) ? 0u : 1u;
        }
    }
    return count;
}

static void test_columns(void)
{
    CHECK(GrainMatrix_Column(0u) == 0u);
    CHECK(GrainMatrix_Column(111u) == 0u);
    CHECK(GrainMatrix_Column(112u) == 1u);
    CHECK(GrainMatrix_Column(500u) == 4u);
    CHECK(GrainMatrix_Column(999u) == 8u);
    CHECK(GrainMatrix_Column(1000u) == 8u);
    CHECK(GrainMatrix_Column(65535u) == 8u);
}

/* No clip: nothing to show, so the matrix is dark (never EMF). */
static void test_no_clip_is_dark(void)
{
    static const uint16_t positions[] = {500u};
    static const uint8_t envelopes[] = {255u};
    GrainMatrixInput input;

    memset(&input, 0, sizeof(input));
    input.position_permille = 500u;
    input.grain_permille = positions;
    input.grain_envelope = envelopes;
    input.grains = 1u;
    memset(&frame, 0x55, sizeof(frame));
    GrainMatrix_Compose(&input, &frame);
    CHECK(lit() == 0u);
    GrainMatrix_Compose(NULL, &frame);
    CHECK(lit() == 0u);
}

/* Grains light their column at their voice's row, as bright as their envelope,
 * over the dim position marker. */
static void test_grains_and_marker(void)
{
    static const uint16_t positions[] = {0u, 999u, 500u, 500u};
    static const uint8_t envelopes[] = {255u, 128u, 255u, 0u};
    GrainMatrixInput input;

    memset(&input, 0, sizeof(input));
    input.clip_ready = true;
    input.position_permille = 250u; /* column 2 */
    GrainMatrix_Compose(&input, &frame);
    CHECK(lit() == MATRIX_FEEDBACK_SIZE);
    for (unsigned y = 0u; y < MATRIX_FEEDBACK_SIZE; ++y) {
        CHECK(same(frame.pixels[y][2], grain_matrix_marker_colour));
    }
    input.grain_permille = positions;
    input.grain_envelope = envelopes;
    input.grains = 4u;
    GrainMatrix_Compose(&input, &frame);
    CHECK(same(frame.pixels[0][0], grain_matrix_grain_colour));
    CHECK(frame.pixels[1][8].blue == 128u && frame.pixels[1][8].red == 75u);
    CHECK(same(frame.pixels[2][4], grain_matrix_grain_colour));
    CHECK(dark(frame.pixels[3][4]));             /* envelope 0 is silent */
    CHECK(same(frame.pixels[0][2], grain_matrix_marker_colour));
    CHECK(lit() == MATRIX_FEEDBACK_SIZE + 3u);
    /* A grain on the marker column outshines it. */
    input.position_permille = 0u;
    GrainMatrix_Compose(&input, &frame);
    CHECK(same(frame.pixels[0][0], grain_matrix_grain_colour));
    CHECK(same(frame.pixels[1][0], grain_matrix_marker_colour));
}

/* More grains than rows wrap onto the rows again. */
static void test_rows_wrap(void)
{
    uint16_t positions[16];
    uint8_t envelopes[16];
    GrainMatrixInput input;

    for (unsigned grain = 0u; grain < 16u; ++grain) {
        positions[grain] = (uint16_t)(grain * 60u);
        envelopes[grain] = 255u;
    }
    memset(&input, 0, sizeof(input));
    input.clip_ready = true;
    input.grain_permille = positions;
    input.grain_envelope = envelopes;
    input.grains = 16u;
    input.position_permille = 1000u;
    GrainMatrix_Compose(&input, &frame);
    CHECK(same(frame.pixels[9u % 9u][GrainMatrix_Column(540u)], grain_matrix_grain_colour));
    CHECK(same(frame.pixels[15u % 9u][GrainMatrix_Column(900u)], grain_matrix_grain_colour));
}

/* A recording keeps its meaning here: the outer ring is the recording colour. */
static void test_recording_ring(void)
{
    MatrixFeedbackConfig config;
    GrainMatrixInput input;

    MatrixFeedback_DefaultConfig(&config);
    memset(&input, 0, sizeof(input));
    input.recording = true;
    GrainMatrix_Compose(&input, &frame);
    CHECK(lit() == 32u);
    CHECK(same(frame.pixels[0][0], config.recording_colour));
    CHECK(same(frame.pixels[8][4], config.recording_colour));
    CHECK(same(frame.pixels[4][0], config.recording_colour));
    CHECK(dark(frame.pixels[4][4]));
}

int main(void)
{
    test_columns();
    test_no_clip_is_dark();
    test_grains_and_marker();
    test_rows_wrap();
    test_recording_ring();
    if (failures != 0u) {
        printf("%u failure(s)\n", failures);
        return 1;
    }
    printf("grain_matrix_test passed\n");
    return 0;
}
