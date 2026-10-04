#include "grain_matrix.h"

#include <stddef.h>
#include <string.h>

/* Violet grains, apart from the EMF buckets (cyan, green, amber, red) and the
 * recording yellow. */
const MatrixFeedbackRgb grain_matrix_grain_colour = {150U, 0U, 255U};
const MatrixFeedbackRgb grain_matrix_marker_colour = {3U, 0U, 8U};

uint8_t GrainMatrix_Column(uint16_t permille)
{
  const uint32_t clamped = (permille > 1000U) ? 1000U : permille;

  return (uint8_t)((clamped * MATRIX_FEEDBACK_SIZE) / 1001U);
}

static uint8_t Scale(uint8_t value, uint8_t envelope)
{
  return (uint8_t)(((uint32_t)value * envelope + 127U) / 255U);
}

static void Brighter(MatrixFeedbackRgb *pixel, MatrixFeedbackRgb colour)
{
  if (((uint32_t)colour.red + colour.green + colour.blue) >
      ((uint32_t)pixel->red + pixel->green + pixel->blue))
  {
    *pixel = colour;
  }
}

void GrainMatrix_Compose(const GrainMatrixInput *input, MatrixFeedbackFrame *frame)
{
  uint32_t index;

  if (frame == NULL)
  {
    return;
  }
  (void)memset(frame, 0, sizeof(*frame));
  if (input == NULL)
  {
    return;
  }
  if (input->clip_ready)
  {
    const uint8_t marker = GrainMatrix_Column(input->position_permille);

    for (index = 0U; index < MATRIX_FEEDBACK_SIZE; ++index)
    {
      frame->pixels[index][marker] = grain_matrix_marker_colour;
    }
    for (index = 0U; (input->grain_permille != NULL) && (input->grain_envelope != NULL) &&
                     (index < input->grains); ++index)
    {
      MatrixFeedbackRgb colour;
      const uint8_t envelope = input->grain_envelope[index];

      colour.red = Scale(grain_matrix_grain_colour.red, envelope);
      colour.green = Scale(grain_matrix_grain_colour.green, envelope);
      colour.blue = Scale(grain_matrix_grain_colour.blue, envelope);
      Brighter(&frame->pixels[index % MATRIX_FEEDBACK_SIZE]
                             [GrainMatrix_Column(input->grain_permille[index])],
               colour);
    }
  }
  if (input->recording)
  {
    MatrixFeedbackConfig config;

    MatrixFeedback_DefaultConfig(&config);
    for (index = 0U; index < MATRIX_FEEDBACK_SIZE; ++index)
    {
      frame->pixels[0][index] = config.recording_colour;
      frame->pixels[MATRIX_FEEDBACK_SIZE - 1U][index] = config.recording_colour;
      frame->pixels[index][0] = config.recording_colour;
      frame->pixels[index][MATRIX_FEEDBACK_SIZE - 1U] = config.recording_colour;
    }
  }
}
