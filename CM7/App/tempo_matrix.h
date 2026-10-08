#ifndef SPOOKY_TEMPO_MATRIX_H
#define SPOOKY_TEMPO_MATRIX_H

/*
 * Demo-only matrix view while the Slicer is the Instrument engine (decision
 * 0020 item 13, full_spooky_proto-p04.15). The matrix shows the tempo, not EMF,
 * so the Field-to-Instrument sensor policy is not exercised (Principle II).
 *
 * - One solid colour turns through the spectrum, one full cycle per 4/4 bar of
 *   the transport (0026 item 1). The phase comes from the transport's beat
 *   position, so the colour freezes while the transport is stopped.
 * - With no clip loaded the matrix is dark.
 * - While a session records, the outer ring is the recording colour, as in
 *   Field (grain_matrix.c does the same).
 *
 * Colours are intended appearance; drive values are tuned on the bench.
 * Portable C with no HAL calls.
 */

#include <stdbool.h>
#include <stdint.h>

#include "matrix_feedback.h"

/* The brightest channel of the hue colour. */
#define TEMPO_MATRIX_LEVEL 160U

typedef struct
{
  bool clip_ready;
  bool recording;
  uint16_t bar_phase; /* position in the bar, 0..65535 for one bar */
} TempoMatrixInput;

/* The colour at a bar phase: red at 0, then yellow, green, cyan, blue and
 * magenta, back to red at the bar line. */
MatrixFeedbackRgb TempoMatrix_Hue(uint16_t bar_phase);
void TempoMatrix_Compose(const TempoMatrixInput *input, MatrixFeedbackFrame *frame);

#endif /* SPOOKY_TEMPO_MATRIX_H */
