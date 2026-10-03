#ifndef SPOOKY_MATRIX_WRITER_H
#define SPOOKY_MATRIX_WRITER_H

#include <stdbool.h>
#include <stdint.h>

#include "matrix_feedback.h"

/*
 * Bounded transfer of decision 0013 frames to the 9x9 window of the Adafruit
 * IS31FL3741 13x9 RGB matrix (full_spooky_proto-54w.8). Portable: it knows the
 * chip's PWM register layout and the board's wiring, never the bus.
 *
 * Each logical row is two register runs: columns 0 to 7 (physical 2 to 9) are
 * 24 contiguous bytes, column 8 (physical 10) is 3 bytes elsewhere. The writer
 * remembers what the matrix shows, compares each new target frame with it and
 * hands out one run per call, only for runs whose pixels differ. The caller
 * performs the run as one I2C write after selecting its page, and reports the
 * outcome; a failed run stays pending.
 */

#define MATRIX_WRITER_MAX_BYTES 24U
#define MATRIX_WRITER_RUNS (MATRIX_FEEDBACK_SIZE * 2U)

typedef struct
{
  uint8_t page;                            /* PWM page 0 or 1 */
  uint8_t reg;                             /* first register in that page */
  uint8_t length;                          /* bytes to write */
  uint8_t bytes[MATRIX_WRITER_MAX_BYTES];
} MatrixWriterRun;

typedef struct
{
  uint8_t pending_runs;    /* runs that differ from the matrix */
  uint32_t runs_written;
  uint32_t runs_failed;
  uint32_t targets;        /* frames handed to SetTarget */
  uint32_t targets_superseded; /* frames replaced before fully written */
} MatrixWriterStatus;

/* The matrix contents are unknown (after power-up or a reset): every run is
 * pending for the next target. */
void MatrixWriter_Init(void);
/* Takes the frame to show. Runs that already match what the matrix shows are
 * not written; a frame not yet fully written is superseded. */
void MatrixWriter_SetTarget(const MatrixFeedbackFrame *frame);
/* The next pending run, row by row; false when the matrix shows the target. */
bool MatrixWriter_Next(MatrixWriterRun *run);
/* Outcome of the run Next returned. */
void MatrixWriter_Done(bool ok);
void MatrixWriter_GetStatus(MatrixWriterStatus *status);

/* One pixel's PWM location and byte order, for single-pixel writers. */
bool MatrixWriter_PixelRun(uint8_t x, uint8_t y, MatrixFeedbackRgb colour,
                           MatrixWriterRun *run);

#endif /* SPOOKY_MATRIX_WRITER_H */
