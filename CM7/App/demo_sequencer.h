#ifndef SPOOKY_DEMO_SEQUENCER_H
#define SPOOKY_DEMO_SEQUENCER_H

/*
 * Demo-only Instrument views and the step view (full_spooky_proto-p04.14;
 * decisions 0020 items 4 to 8, 0021 item 7, 0026 item 8). Provisional: these
 * bindings do not settle C-021 to C-023, C-083, C-084 or D-012.
 *
 * - Main engine page: an Encoder 0 click runs or stops the transport (0021
 *   item 7); a long Encoder 2 press opens the view menu (C-021, in the menu form
 *   of decision 0009 items 16 to 18). Turns and the Encoder 3 click stay with
 *   the engine's pages (demo_instrument.c).
 * - View menu: ENGINE (the main page) and SEQUENCER. Encoder 0 scrolls, an
 *   Encoder 0 click selects, an Encoder 1 click closes without a change, and the
 *   menu closes after DEMO_SEQ_MENU_TIMEOUT_MS without input.
 * - Step view, steps: Encoder 0 browses (C-072); its click edits the step
 *   (C-073); its long press moves to the settings row (C-077).
 * - Step edit: Encoder 0 changes the value at once (C-074); an Encoder 1 click
 *   turns the step on or off (C-075); an Encoder 0 click returns (C-076).
 * - Settings row: Tempo and Division. Encoder 0 browses (C-078); its click edits
 *   (C-079); its long press returns to the steps (0026 item 8). An edit is heard
 *   at once (C-080); an Encoder 0 click keeps it (C-082) and an Encoder 1 click
 *   restores the value from before the edit (C-081).
 * - Anywhere in the step view, an Encoder 2 click runs or stops the transport
 *   (0021 item 7), and an Encoder 3 click (C-083) or a long Encoder 2 press opens
 *   the view menu; a setting edit in progress is kept. Other gestures are
 *   swallowed; the Encoder 3 hold stays Shift.
 *
 * Portable C with no HAL calls: the caller applies the effects.
 */

#include <stdbool.h>
#include <stdint.h>

#include "sm/gesture.h"
#include "step_pattern.h"

#define DEMO_SEQ_MENU_TIMEOUT_MS 5000U /* decision 0009 menu inactivity */
#define DEMO_SEQ_VALUE_STEP 10U        /* a step value moves 1 % of the clip a detent */
#define DEMO_SEQ_VALUE_MAX 1000U
#define DEMO_SEQ_TEMPO_STEP_X100 100U  /* 1 BPM a detent (0026 item 8) */

typedef enum
{
  DEMO_SEQ_VIEW_ENGINE = 0, /* the main engine page */
  DEMO_SEQ_VIEW_MENU,
  DEMO_SEQ_VIEW_STEPS
} DemoSeqView;

typedef enum
{
  DEMO_SEQ_FOCUS_STEPS = 0,
  DEMO_SEQ_FOCUS_STEP_EDIT,
  DEMO_SEQ_FOCUS_SETTINGS,
  DEMO_SEQ_FOCUS_SETTING_EDIT
} DemoSeqFocus;

typedef enum
{
  DEMO_SEQ_SETTING_TEMPO = 0,
  DEMO_SEQ_SETTING_DIVISION,
  DEMO_SEQ_SETTING_COUNT
} DemoSeqSetting;

typedef enum
{
  DEMO_SEQ_ITEM_ENGINE = 0,
  DEMO_SEQ_ITEM_SEQUENCER,
  DEMO_SEQ_ITEM_COUNT
} DemoSeqItem;

typedef struct
{
  uint8_t view;           /* DemoSeqView */
  uint8_t focus;          /* DemoSeqFocus, in the step view */
  uint8_t step;           /* the selected step */
  uint8_t setting;        /* DemoSeqSetting */
  uint8_t item;           /* DemoSeqItem highlighted in the menu */
  uint8_t menu_return;    /* the view a closed menu returns to */
  uint32_t edit_backup;   /* the setting's value before its edit */
  uint32_t last_input_ms; /* for the menu timeout */
} DemoSequencer;

/* What the caller works on and applies. */
typedef struct
{
  StepPattern *pattern;   /* edited in place */
  uint32_t tempo_x100;    /* in: the tempo now; out: the tempo to set */
  bool toggle_run;        /* out: run or stop the transport */
  bool tempo_changed;     /* out */
} DemoSeqTarget;

void DemoSequencer_Init(DemoSequencer *seq);
/* Back to the main engine page, as on every entry to Instrument (0027 item 4). */
void DemoSequencer_Reset(DemoSequencer *seq);
/* True if the gesture was taken. On the main page only the Encoder 0 click and
 * the long Encoder 2 press are taken; everything else is left for the pages. */
bool DemoSequencer_OnGesture(DemoSequencer *seq, Gesture gesture, DemoSeqTarget *target,
                             uint32_t now_ms);
/* Closes the menu after DEMO_SEQ_MENU_TIMEOUT_MS without input. True if it closed. */
bool DemoSequencer_Tick(DemoSequencer *seq, uint32_t now_ms);
const char *DemoSequencer_ItemName(uint8_t item);
const char *DemoSequencer_SettingName(uint8_t setting);
/* "1/16", "1/8" or "1/4". */
const char *DemoSequencer_DivisionName(uint8_t steps_per_beat);

#endif /* SPOOKY_DEMO_SEQUENCER_H */
