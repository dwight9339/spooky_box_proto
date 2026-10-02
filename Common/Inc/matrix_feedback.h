#ifndef SPOOKY_MATRIX_FEEDBACK_H
#define SPOOKY_MATRIX_FEEDBACK_H

#include <stdbool.h>
#include <stdint.h>

/* 9x9 matrix presentation of EMF, radio onsets and recording status
 * (decision 0013). The application publishes semantic facts (EMF bucket or
 * unknown, onset sizes, recording state and faults); this module decides the
 * pixels. It is hardware-neutral and bounded: Advance does constant work and
 * Compose writes one 81-pixel frame. The caller owns the matrix transfer.
 * Colours are intended appearance; drive values are tuned on the bench. */

#define MATRIX_FEEDBACK_SIZE 9U
#define MATRIX_FEEDBACK_CENTRE 4U
#define MATRIX_FEEDBACK_EMF_BUCKETS 5U
#define MATRIX_FEEDBACK_ONSET_SIZES 3U

typedef struct
{
  uint8_t red;
  uint8_t green;
  uint8_t blue;
} MatrixFeedbackRgb;

typedef struct
{
  MatrixFeedbackRgb pixels[MATRIX_FEEDBACK_SIZE][MATRIX_FEEDBACK_SIZE]; /* [y][x] */
} MatrixFeedbackFrame;

typedef struct
{
  MatrixFeedbackRgb emf_colour[MATRIX_FEEDBACK_EMF_BUCKETS];
  uint16_t emf_step_ms[MATRIX_FEEDBACK_EMF_BUCKETS];
  MatrixFeedbackRgb unknown_colour;   /* dim grey, at the bucket 0 period */
  MatrixFeedbackRgb recording_colour; /* status border while recording */
  MatrixFeedbackRgb fault_colour;     /* status border after a fault */
  uint8_t trail_percent;              /* trail brightness, 35 */
  uint8_t kick_px[MATRIX_FEEDBACK_ONSET_SIZES]; /* small, medium, large */
  uint16_t kick_decay_ms;             /* one pixel per 80 ms */
  uint16_t fault_blink_ms;            /* 250 ms on, 250 ms off */
  uint8_t fault_blinks;               /* 3 */
} MatrixFeedbackConfig;

typedef enum
{
  MATRIX_BORDER_NONE = 0,      /* animation reaches the outer ring */
  MATRIX_BORDER_RECORDING,     /* solid recording colour */
  MATRIX_BORDER_RECORDING_KICK,/* dark while a kick is in progress */
  MATRIX_BORDER_FAULT_ON,
  MATRIX_BORDER_FAULT_OFF
} MatrixBorder;

typedef struct
{
  uint8_t step;
  uint8_t loop_steps;
  bool trail;
  bool emf_known;     /* the colour shown is a measured bucket */
  uint8_t emf_bucket; /* latched at step 0; meaningful when emf_known */
  uint8_t kick_px;
  int8_t kick_direction; /* +1 shifts even rows right, odd rows left */
  MatrixBorder border;
  uint32_t dropped_steps; /* steps skipped to keep tempo under load */
} MatrixFeedbackStatus;

void MatrixFeedback_DefaultConfig(MatrixFeedbackConfig *config);
/* Rejects zero periods, decay or blink times, a trail above 100% and kick
 * sizes that are zero, decrease or would move a row off the matrix. */
bool MatrixFeedback_Init(const MatrixFeedbackConfig *config, uint32_t now_ms);
/* Trail on: 7-step loop with a 1-pixel trail; off: 6 steps. Restarts the
 * loop at step 0 when the length changes. */
void MatrixFeedback_SetTrail(bool enabled);
/* Unknown takes effect at once; a known bucket is latched at step 0. */
void MatrixFeedback_SetEmf(bool known, uint8_t bucket);
/* size 1..3 = small, medium, large. A kick smaller than the one in progress
 * is ignored; an accepted kick reverses the previous direction. */
void MatrixFeedback_OnOnset(uint8_t size, uint32_t now_ms);
/* Follows published session state: true on recording started, false on a
 * normal stop. A new recording ends a fault blink. */
void MatrixFeedback_SetRecording(bool recording);
/* Recording faulted or aborted: blinks the fault border, then turns it off. */
void MatrixFeedback_OnRecordingFault(uint32_t now_ms);
/* Advances steps, kick decay and the fault blink. Under load it drops steps
 * to keep each bucket's tempo. Returns true when the frame may have changed
 * since the last call. */
bool MatrixFeedback_Advance(uint32_t now_ms);
bool MatrixFeedback_Compose(MatrixFeedbackFrame *frame);
bool MatrixFeedback_GetStatus(MatrixFeedbackStatus *status);

#endif /* SPOOKY_MATRIX_FEEDBACK_H */
