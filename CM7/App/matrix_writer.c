#include "matrix_writer.h"

#include <stddef.h>
#include <string.h>

/* Board wiring (proven by UI MATRIX ANIMATE): the 9x9 window starts at
 * physical column 2, logical rows map to these physical rows, and the PWM
 * registers hold three bytes per LED, 180 in page 0 and the rest in page 1. */
#define MATRIX_COLUMN_OFFSET 2U
/* The matrix is mounted rotated 180 degrees from that mapping (UI MATRIX
 * ORIENT, 2026-10-03): logical (x, y) is panel (8 - x, 8 - y). Everything
 * below the public functions works in panel coordinates. */
#define MATRIX_LAST (MATRIX_FEEDBACK_SIZE - 1U)
#define MATRIX_PAGE0_BYTES 180U
#define MATRIX_RUN_A_PIXELS 8U   /* logical columns 0 to 7 */

static const uint8_t row_map[MATRIX_FEEDBACK_SIZE] = {8U, 5U, 4U, 3U, 2U, 1U, 0U, 7U, 6U};

/* Panel coordinates: [panel y][panel x]. */
static MatrixFeedbackRgb shown[MATRIX_FEEDBACK_SIZE][MATRIX_FEEDBACK_SIZE];
static MatrixFeedbackRgb target[MATRIX_FEEDBACK_SIZE][MATRIX_FEEDBACK_SIZE];
static bool run_known[MATRIX_WRITER_RUNS];
static uint32_t pending;          /* bit r: run r differs from the matrix */
static int8_t in_flight;          /* run handed out by Next, or -1 */
static MatrixWriterStatus writer_status;

/* Byte offset of a physical LED in the PWM register space. */
static uint16_t LedOffset(uint8_t physical_x, uint8_t physical_y)
{
  return (uint16_t)((uint16_t)physical_x +
    ((physical_x < 10U) ? ((uint16_t)physical_y * 10U)
                        : (80U + ((uint16_t)physical_y * 3U)))) * 3U;
}

/* The board is BGR on even physical columns and GRB on odd ones. */
static void PackPixel(uint8_t physical_x, MatrixFeedbackRgb colour, uint8_t *bytes)
{
  if ((physical_x & 1U) != 0U)
  {
    bytes[0] = colour.green;
    bytes[1] = colour.red;
    bytes[2] = colour.blue;
  }
  else
  {
    bytes[0] = colour.blue;
    bytes[1] = colour.green;
    bytes[2] = colour.red;
  }
}

static void SetLocation(uint16_t offset, MatrixWriterRun *run)
{
  run->page = (offset < MATRIX_PAGE0_BYTES) ? 0U : 1U;
  run->reg = (uint8_t)((offset < MATRIX_PAGE0_BYTES) ? offset
                                                     : (offset - MATRIX_PAGE0_BYTES));
}

/* Run r covers row r / 2: columns 0 to 7 when r is even, column 8 when odd. */
static uint8_t RunFirstColumn(uint8_t r)
{
  return ((r & 1U) == 0U) ? 0U : MATRIX_RUN_A_PIXELS;
}

static uint8_t RunColumns(uint8_t r)
{
  return ((r & 1U) == 0U) ? MATRIX_RUN_A_PIXELS : 1U;
}

static bool SameColour(MatrixFeedbackRgb a, MatrixFeedbackRgb b)
{
  return (a.red == b.red) && (a.green == b.green) && (a.blue == b.blue);
}

void MatrixWriter_Init(void)
{
  (void)memset(shown, 0, sizeof(shown));
  (void)memset(target, 0, sizeof(target));
  (void)memset(run_known, 0, sizeof(run_known));
  (void)memset(&writer_status, 0, sizeof(writer_status));
  pending = 0U;
  in_flight = -1;
}

void MatrixWriter_SetTarget(const MatrixFeedbackFrame *frame)
{
  uint8_t r;
  uint8_t py;
  uint8_t px;

  if (frame == NULL)
  {
    return;
  }
  ++writer_status.targets;
  if (pending != 0U)
  {
    ++writer_status.targets_superseded;
  }
  for (py = 0U; py < MATRIX_FEEDBACK_SIZE; ++py)
  {
    for (px = 0U; px < MATRIX_FEEDBACK_SIZE; ++px)
    {
      target[py][px] = frame->pixels[MATRIX_LAST - py][MATRIX_LAST - px];
    }
  }
  pending = 0U;
  for (r = 0U; r < MATRIX_WRITER_RUNS; ++r)
  {
    const uint8_t y = (uint8_t)(r / 2U);
    const uint8_t first = RunFirstColumn(r);
    bool differs = !run_known[r];
    uint8_t x;

    for (x = first; !differs && (x < (uint8_t)(first + RunColumns(r))); ++x)
    {
      differs = !SameColour(shown[y][x], target[y][x]);
    }
    if (differs)
    {
      pending |= (1UL << r);
    }
  }
  in_flight = -1;
}

bool MatrixWriter_Next(MatrixWriterRun *run)
{
  uint8_t r;
  uint8_t y;
  uint8_t first;
  uint8_t count;
  uint8_t index;

  if ((run == NULL) || (pending == 0U))
  {
    return false;
  }
  for (r = 0U; (pending & (1UL << r)) == 0U; ++r)
  {
  }
  y = (uint8_t)(r / 2U);
  first = RunFirstColumn(r);
  count = RunColumns(r);
  SetLocation(LedOffset((uint8_t)(first + MATRIX_COLUMN_OFFSET), row_map[y]), run);
  run->length = (uint8_t)(count * 3U);
  for (index = 0U; index < count; ++index)
  {
    const uint8_t x = (uint8_t)(first + index);

    PackPixel((uint8_t)(x + MATRIX_COLUMN_OFFSET), target[y][x], &run->bytes[index * 3U]);
  }
  in_flight = (int8_t)r;
  return true;
}

void MatrixWriter_Done(bool ok)
{
  uint8_t r;
  uint8_t y;
  uint8_t x;

  if (in_flight < 0)
  {
    return;
  }
  r = (uint8_t)in_flight;
  in_flight = -1;
  if (!ok)
  {
    ++writer_status.runs_failed;
    return;
  }
  ++writer_status.runs_written;
  y = (uint8_t)(r / 2U);
  for (x = RunFirstColumn(r); x < (uint8_t)(RunFirstColumn(r) + RunColumns(r)); ++x)
  {
    shown[y][x] = target[y][x];
  }
  run_known[r] = true;
  pending &= ~(1UL << r);
}

void MatrixWriter_GetStatus(MatrixWriterStatus *status)
{
  uint32_t bits = pending;

  if (status == NULL)
  {
    return;
  }
  *status = writer_status;
  status->pending_runs = 0U;
  while (bits != 0U)
  {
    status->pending_runs = (uint8_t)(status->pending_runs + (bits & 1U));
    bits >>= 1U;
  }
}

bool MatrixWriter_PixelRun(uint8_t x, uint8_t y, MatrixFeedbackRgb colour,
                           MatrixWriterRun *run)
{
  uint8_t physical_x;

  if ((run == NULL) || (x >= MATRIX_FEEDBACK_SIZE) || (y >= MATRIX_FEEDBACK_SIZE))
  {
    return false;
  }
  physical_x = (uint8_t)((MATRIX_LAST - x) + MATRIX_COLUMN_OFFSET);
  SetLocation(LedOffset(physical_x, row_map[MATRIX_LAST - y]), run);
  run->length = 3U;
  PackPixel(physical_x, colour, run->bytes);
  return true;
}
