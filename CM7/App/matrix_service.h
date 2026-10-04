#ifndef SPOOKY_MATRIX_SERVICE_H
#define SPOOKY_MATRIX_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "matrix_writer.h"

/*
 * Decision 0013 on the matrix (full_spooky_proto-54w.8). Portable: it runs the
 * matrix_feedback renderer on the semantic facts the caller passes each pass
 * and hands at most one register run per pass to the caller, which owns the
 * bus. Off until enabled; enabling assumes the matrix contents are unknown, so
 * the first frame is written in full.
 */

typedef struct
{
  bool emf_known;      /* a current EMF measurement (emf_level VALID) */
  uint8_t emf_bucket;  /* meaningful when emf_known */
  uint8_t onset;       /* largest radio onset since the last pass, 0 for none */
} MatrixServiceInput;

typedef struct
{
  bool enabled;
  bool trail;
  uint32_t frames;           /* frames composed and handed to the writer */
  uint32_t runs_written;
  uint32_t runs_failed;
  uint32_t frames_superseded;/* replaced before fully written */
  uint32_t dropped_steps;    /* renderer steps skipped to keep tempo */
  uint32_t run_us_max;       /* longest run the caller reported */
  uint8_t pending_runs;
} MatrixServiceStatus;

bool MatrixService_Init(uint32_t now_ms);
/* Enabling restarts the animation and rewrites the whole matrix. */
void MatrixService_SetEnabled(bool enabled, uint32_t now_ms);
bool MatrixService_IsEnabled(void);
/* The matrix contents are unknown again (it was powered off or reset). */
void MatrixService_Invalidate(void);
void MatrixService_SetTrail(bool enabled);
/* Published session events (decision 0013 items 11 to 13). */
void MatrixService_OnRecording(bool recording);
void MatrixService_OnRecordingFault(uint32_t now_ms);
/* One pass. Returns true with *run filled when the caller should write it now,
 * then report with MatrixService_RunDone before the next pass. */
bool MatrixService_Service(uint32_t now_ms, const MatrixServiceInput *input,
                           MatrixWriterRun *run);
#if defined(SPOOKY_DEMO)
/* Demo only (p04.7): one pass that writes a frame composed elsewhere instead of
 * the feedback renderer's (the grain view in Instrument). frame is NULL when no
 * new frame is due. The renderer composes afresh when Service runs again. */
bool MatrixService_ServiceFrame(const MatrixFeedbackFrame *frame, MatrixWriterRun *run);
#endif
void MatrixService_RunDone(bool ok, uint32_t elapsed_us);
void MatrixService_GetStatus(MatrixServiceStatus *status);

#endif /* SPOOKY_MATRIX_SERVICE_H */
