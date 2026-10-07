#include "tempo_matrix.h"

#include <stddef.h>
#include <string.h>

MatrixFeedbackRgb TempoMatrix_Hue(uint16_t bar_phase)
{
  /* Six sectors of the hue circle; in each, one channel ramps. */
  const uint32_t scaled = (uint32_t)bar_phase * 6U;
  const uint32_t sector = scaled >> 16;
  const uint8_t rise = (uint8_t)(((scaled & 0xFFFFU) * TEMPO_MATRIX_LEVEL) >> 16);
  const uint8_t fall = (uint8_t)(TEMPO_MATRIX_LEVEL - rise);
  MatrixFeedbackRgb colour = {0U, 0U, 0U};

  switch (sector)
  {
    case 0U:
      colour.red = TEMPO_MATRIX_LEVEL;
      colour.green = rise;
      break;
    case 1U:
      colour.red = fall;
      colour.green = TEMPO_MATRIX_LEVEL;
      break;
    case 2U:
      colour.green = TEMPO_MATRIX_LEVEL;
      colour.blue = rise;
      break;
    case 3U:
      colour.green = fall;
      colour.blue = TEMPO_MATRIX_LEVEL;
      break;
    case 4U:
      colour.red = rise;
      colour.blue = TEMPO_MATRIX_LEVEL;
      break;
    default:
      colour.red = TEMPO_MATRIX_LEVEL;
      colour.blue = fall;
      break;
  }
  return colour;
}

void TempoMatrix_Compose(const TempoMatrixInput *input, MatrixFeedbackFrame *frame)
{
  uint32_t row;
  uint32_t column;

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
    const MatrixFeedbackRgb colour = TempoMatrix_Hue(input->bar_phase);

    for (row = 0U; row < MATRIX_FEEDBACK_SIZE; ++row)
    {
      for (column = 0U; column < MATRIX_FEEDBACK_SIZE; ++column)
      {
        frame->pixels[row][column] = colour;
      }
    }
  }
  if (input->recording)
  {
    MatrixFeedbackConfig config;

    MatrixFeedback_DefaultConfig(&config);
    for (row = 0U; row < MATRIX_FEEDBACK_SIZE; ++row)
    {
      frame->pixels[0][row] = config.recording_colour;
      frame->pixels[MATRIX_FEEDBACK_SIZE - 1U][row] = config.recording_colour;
      frame->pixels[row][0] = config.recording_colour;
      frame->pixels[row][MATRIX_FEEDBACK_SIZE - 1U] = config.recording_colour;
    }
  }
}
