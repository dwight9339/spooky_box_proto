#ifndef SPOOKY_GRAIN_MATRIX_H
#define SPOOKY_GRAIN_MATRIX_H

/*
 * Demo-only matrix presentation of grain activity in Instrument (decision 0011
 * item 16, full_spooky_proto-p04.7). The matrix shows grains, not EMF, so the
 * Field-to-Instrument sensor policy is not exercised (Principle II).
 *
 * - The nine columns are the clip, start to end. Each sounding grain lights the
 *   column it is reading, at a row taken from its voice, as bright as its
 *   envelope. The position parameter is a dim column underneath.
 * - With no clip loaded the matrix is dark: there is nothing to show.
 * - While a session records, the outer ring is the recording colour, as in
 *   Field (session state keeps its meaning in every view).
 *
 * Colours are intended appearance; drive values are tuned on the bench.
 * Portable C with no HAL calls.
 */

#include <stdbool.h>
#include <stdint.h>

#include "matrix_feedback.h"

typedef struct
{
  bool clip_ready;
  bool recording;
  uint16_t position_permille;        /* the position parameter */
  const uint16_t *grain_permille;    /* clip position each grain is reading */
  const uint8_t *grain_envelope;     /* 0..255 */
  uint32_t grains;
} GrainMatrixInput;

/* The grain colour at full envelope and the position marker. The recording
 * ring takes MatrixFeedback's recording colour. */
extern const MatrixFeedbackRgb grain_matrix_grain_colour;
extern const MatrixFeedbackRgb grain_matrix_marker_colour;

/* The column (0..8) of a clip position in permille. */
uint8_t GrainMatrix_Column(uint16_t permille);
void GrainMatrix_Compose(const GrainMatrixInput *input, MatrixFeedbackFrame *frame);

#endif /* SPOOKY_GRAIN_MATRIX_H */
