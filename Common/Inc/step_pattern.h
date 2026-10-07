#ifndef SPOOKY_STEP_PATTERN_H
#define SPOOKY_STEP_PATTERN_H

/*
 * One engine's step pattern (decision 0021 items 8 to 10). Each step has an on
 * flag and one value whose meaning the engine defines (a clip position in
 * permille for Granular). Division and length are pattern settings; the playhead
 * follows the shared transport (0027 item 3). While a sequence drives a
 * parameter, the live knob offsets it (0020 item 7, 0021 item 9).
 *
 * The values and flags are written one at a time, so the radio interrupt may read
 * a step while the foreground edits another. Portable C with no HAL calls.
 */

#include <stdbool.h>
#include <stdint.h>

#include "transport.h"

#define STEP_PATTERN_MAX_STEPS 16U

typedef struct
{
  volatile uint16_t value;
  volatile bool on;
} PatternStep;

typedef struct
{
  PatternStep steps[STEP_PATTERN_MAX_STEPS];
  volatile uint8_t length;          /* 1..STEP_PATTERN_MAX_STEPS */
  volatile uint8_t steps_per_beat;  /* 4 (1/16), 2 (1/8) or 1 (1/4) */
} StepPattern;

/* 16 steps at 1/16, all on, values rising evenly from 0: step n is
 * n * max_value / 16 (0027 item 5 for Granular, with max_value 1000). */
void StepPattern_InitSweep(StepPattern *pattern, uint16_t max_value);
/* The step the transport is on: whole steps since the start, modulo the length. */
uint32_t StepPattern_Playhead(const StepPattern *pattern, const Transport *transport);
/* value + (knob - centre), held within 0..max_value. */
uint16_t StepPattern_Offset(uint16_t value, uint16_t knob, uint16_t centre, uint16_t max_value);
/* Next or previous division in 1/16, 1/8, 1/4 order, held at the ends. */
uint8_t StepPattern_NextDivision(uint8_t steps_per_beat, int32_t detents);

#endif /* SPOOKY_STEP_PATTERN_H */
