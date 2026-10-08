#ifndef SPOOKY_DEMO_SLICER_H
#define SPOOKY_DEMO_SLICER_H

/*
 * Demo-only Slicer page (decisions 0020 item 11 and 0022 items 4 to 9 and 14;
 * full_spooky_proto-p04.15). Provisional: these bindings do not settle C-025 or
 * 0022 item 5 (user call 2026-10-07, demo-track rule 5).
 *
 * - Main page, browse: an Encoder 0 turn selects a slice (0022 item 5) and,
 *   with the transport stopped, auditions it once (item 8). An Encoder 0 hold
 *   opens the selected slice. The Encoder 0 click stays the transport's run and
 *   stop on every engine's main page (0021 items 6 and 7), so a hold opens the
 *   slice instead of 0022 item 5's press.
 * - An Encoder 1 turn chooses the slice count (4, 8 or 16; item 4) and an
 *   Encoder 1 click applies it (item 9). If boundaries, slice parameters or the
 *   pattern were edited, the page asks first: a second Encoder 1 click discards
 *   them and applies; any other gesture cancels. A count change makes equal
 *   slices with default parameters, and the caller rebuilds the pattern with
 *   DemoSlicer_ClipPattern (user call 2026-10-08, p04.16, in place of 0022
 *   item 11's remap).
 * - Slice open: Encoders 0 to 3 turn pitch, gate, level and length (item 6).
 *   Length moves the slice's end, which is also the next slice's start, in
 *   DEMO_SLICER_LENGTH_STEP_MS steps, never below SLICER_MIN_SLICE_SAMPLES
 *   (item 7); the last slice's end is the clip end. An Encoder 0 click closes
 *   the slice; an Encoder 2 click runs or stops the transport, as in the step
 *   view. Other gestures are swallowed.
 * - The Encoder 1 hold (engine menu), the Encoder 2 hold (view menu) and the
 *   Encoder 0 click on the main page are not taken here (demo_sequencer.c).
 *
 * A new clip keeps the count, the slice parameters and the pattern and makes
 * the boundaries equal again (0022 item 14).
 *
 * Portable C with no HAL calls: the caller applies the effects.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sm/gesture.h"
#include "slicer.h"
#include "step_pattern.h"

#define DEMO_SLICER_LENGTH_STEP_MS 10U
#define DEMO_SLICER_GATE_STEP 5U
#define DEMO_SLICER_LEVEL_STEP 5U

typedef enum
{
  DEMO_SLICER_BROWSE = 0,
  DEMO_SLICER_SLICE,       /* the selected slice is open */
  DEMO_SLICER_CONFIRM      /* a count change would discard edits */
} DemoSlicerFocus;

typedef struct
{
  uint8_t focus;           /* DemoSlicerFocus */
  uint8_t selected;        /* the selected slice */
  uint8_t count;           /* the slice count in force: 4, 8 or 16 */
  uint8_t count_choice;    /* the count being chosen; equals count when none is */
  bool pattern_edited;     /* a step was edited since the pattern was last built */
  SlicerSetup setup;       /* the map and slice parameters; staged by the caller */
} DemoSlicer;

/* What the caller applies. */
typedef struct
{
  bool toggle_run;         /* run or stop the transport */
  bool setup_changed;      /* stage setup in the engine */
  bool count_changed;      /* rebuild the pattern for setup.map */
  bool audition;           /* play audition_slice once (the transport is stopped) */
  uint8_t audition_slice;
  SliceMap old_map;        /* valid with count_changed */
} DemoSlicerAction;

/* 16 slices, default parameters, no clip yet. */
void DemoSlicer_Init(DemoSlicer *slicer);
/* Back to browsing, with no count being chosen: on every landing on the main
 * page (0027 item 4). The selection is kept. */
void DemoSlicer_Reset(DemoSlicer *slicer);
/* A new clip of clip_samples: equal boundaries at the current count; the slice
 * parameters stay (0022 item 14). */
void DemoSlicer_OnClip(DemoSlicer *slicer, uint32_t clip_samples);
/* True while the page holds a state that takes gestures before the sequencer
 * shell does (a slice open or a confirmation). */
bool DemoSlicer_Modal(const DemoSlicer *slicer);
/* True if the gesture was taken. running is the transport's run state. */
bool DemoSlicer_OnGesture(DemoSlicer *slicer, Gesture gesture, bool running,
                          DemoSlicerAction *action);
/* The step view changed a Slicer step (p04.16): a count change now asks
 * before it rebuilds the pattern. */
void DemoSlicer_MarkPatternEdited(DemoSlicer *slicer);
/* Boundaries or slice parameters differ from equal slices at their defaults,
 * or the pattern was edited since it was last built. */
bool DemoSlicer_Edited(const DemoSlicer *slicer);
/* The next or previous count in 4, 8, 16 order, held at the ends. */
uint8_t DemoSlicer_NextCount(uint8_t count, int32_t detents);
/* A slice's length in milliseconds of clip. */
uint32_t DemoSlicer_LengthMs(const DemoSlicer *slicer, uint8_t slice);
/* The pattern a count change leaves: each step holds the slice under its time
 * in the clip, and only a step where a new slice begins is on, so the slices
 * play through and the pattern sounds like the clip at any count. With 16
 * equal slices it is the identity pattern (0022 item 10). This replaces 0022
 * item 11's remap by time, so a count round trip (16, 4, 16) gives the
 * identity back (user call 2026-10-07, p04.15 bench); an edited pattern is
 * rebuilt too, after the page has asked (user call 2026-10-08, p04.16).
 * Written one step at a time, as the step view edits. */
void DemoSlicer_ClipPattern(const SliceMap *map, StepPattern *pattern);

#endif /* SPOOKY_DEMO_SLICER_H */
