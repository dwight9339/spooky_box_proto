#ifndef SPOOKY_DEMO_FIELD_H
#define SPOOKY_DEMO_FIELD_H

/*
 * Demo-only Field on the M7 without product IPC (decision 0011 item 12,
 * full_spooky_proto-p04.3; builds only with SPOOKY_DEMO). It narrows
 * full_spooky_proto-54w.5 and transfers no ownership: the M4 sleeps.
 *
 * - Switch edges and encoder detents from the UI service become InputResolution
 *   inputs on the M7 event queue (input class). Resolved gestures go to the
 *   Context machine as internal events, and both machines' thresholds and
 *   timeouts as one tick event posted only when one is due (decision 0007).
 * - Context commands go through the shared command policy (decision 0008) to
 *   Classic, the Radio machine (band menu) and the Session machine (prompt).
 * - The C-010 chord saves and loads the Instrument clip (demo_clip.c, p04.6).
 *   Instrument shows the clip as it saves, loads and plays; a refused or failed
 *   save or load sends Context a Shift+B0 gesture (the C-028 path back to Field)
 *   and shows the fault. The session prompt is refused in Instrument.
 * - The OLED (demo_view.c), the button and encoder lights (demo_lights.c) render
 *   published state; the matrix is matrix_adapter.c.
 *
 * Foreground only, like the dispatcher it is called from.
 */

#include <stdbool.h>
#include <stdint.h>

#include "event_queue.h"
#include "sm/radio_port.h"
#include "sm/session_port.h"

/* After AppDispatch_Init, UiBoardTest_Start and MatrixAdapter_Init. Starts the
 * OLED (about 200 ms) and the light PWM. */
void DemoField_Init(void);
/* One pass: posts a due tick, composes the view when due, writes at most one
 * OLED page and updates the lights. */
void DemoField_Service(void);
void DemoField_Dispatch(const EvqEvent *event);

/* From the UI service pass (ui_board_test.c): a debounced switch edge, with
 * control in InpControl order, and net encoder detents (clockwise positive). */
void DemoField_OnControl(uint8_t control, bool pressed, uint32_t now_ms);
void DemoField_OnDetents(uint8_t encoder, int32_t detents, uint32_t now_ms);

/* Published Session events and state changes (session_control.c). */
void DemoField_OnSessionEvent(SesPublished event);
void DemoField_OnSessionStateChanged(void);
/* The final outcome of a capture save (demo_rolling.c): a DemoSaveOutcome. */
void DemoField_OnSaveOutcome(uint8_t outcome);
/* The C-010 clip is READY or FAILED (demo_clip.c): a DemoClipState and, for
 * FAILED, a DemoClipFault. A failure returns Instrument to Field (p04.6). */
void DemoField_OnClipOutcome(uint8_t state, uint8_t fault);
/* Every Radio machine answer (radio_adapter.c). */
void DemoField_OnRadioAnswer(RadPublished event, const RadCommand *command);

/* Context has Classic as the Field engine and Field as the mode. */
bool DemoField_ClassicActive(void);

/* `DEMO` and `DEMO STATUS` reply `OK DEMO ...` (counters for bench evidence);
 * `ROLL`, `ROLL ON|OFF` and `ROLL SAVE` control and report rolling capture;
 * `DEMO CLIP` reports the Instrument clip. */
bool DemoField_HandleCommand(const char *command);

#endif /* SPOOKY_DEMO_FIELD_H */
