# 0031. Encoder and button lights flash on radio onsets

- **Status:** Proposed
- **Date:** 2026-10-17
- **Supersedes:** none
- **Beads:** `full_spooky_proto-54w.8` (the product mapping); demo consumer
  `full_spooky_proto-p04.12` (encoder PWM on the demo image) and a demo task for the
  flashes, proposed with this record

## Context

Decision 0013 settled how the LED matrix shows radio activity: each radio onset kicks
the rows sideways. The behavior model still lists how the standalone LEDs express
radio activity as open (`docs/design/behavior/presentation.md`, Open behavior, owned
by `54w.8`). Decision 0011 names radio-activity feedback on the matrix *and* the button
LEDs as part of what the Halloween video shows, and pulls `54w.8` forward.

The user's picture of the UI (2026-10-17): the encoders and buttons flash with radio
activity, as the matrix does, except Button 0 while it breathes during a session.

Facts that shape the decision:

- The onset detector (decision 0013 items 6, 7 and 10; `CM7/App/radio_activity.c`
  and `radio_activity_feed.c`) reports small, medium and large onsets measured from
  the radio audio. It fires nothing while the radio is not running, is Faulted or is
  tuning. Each reader of the feed sees every onset once; today the readers are
  Classic's activity hold and the matrix.
- A matrix kick of 1, 2 or 3 pixels decays by one pixel every 80 ms (decision 0013
  item 8).
- The button LEDs are perceived-lightness PWM through the inverse CIE L\* curve
  (decision 0005 item 13). Every encoder LED channel is on a timer channel (decision
  0005; `full_spooky_proto-p04.12`), but the encoders are still driven on and off.
- A lit control means it has an action in the current context, and in Shift only the
  controls with an available Shift action are lit (decision 0008 item 8,
  PRES-INP-01). While the session prompt is open, only Encoder 0, the confirm, is lit
  (PRES-LED-06).
- Encoder colour is a learned page-identity cue (`spec/product/modes-and-interaction.md`,
  parameter pages; PRES-CTX-03).
- In Instrument the matrix shows the engine's activity, not radio (decision 0011
  item 16; decision 0021).

Principles involved: I (lights never delay capture), II (a flash means a measured
radio onset and nothing else; lit still means available), IV (timings stay
configurable until bench trials fix them), V (host tests are not hardware evidence),
VII (the application publishes onsets; the renderer decides brightness).

## Options

1. **Flash every control, lit or not.** Rejected by the user. An unlit control
   lighting up on an onset would blur the cue that a lit control has an action.
2. **Flash in a colour, for example white.** Rejected. It would override the page
   colour of the encoders, and white is reserved for acknowledgements and the confirm
   prompt (decision 0013 item 14).
3. **A sustained brightness that follows the activity level.** Rejected for the same
   reason the user rejected a sustained row shear on the matrix (decision 0013
   option 1): a constant offset reads as a static state, not as something happening.
4. **Keep flashing while the lights give an instruction (prompt open, Shift held).**
   Rejected by the user. The flashes would compete with the controls the user is
   being asked to press.
5. **Flash in Instrument, on radio onsets or on grain triggers.** Rejected by the
   user. Radio onsets are a Field cue, and grain flashes would be a different signal
   that needs its own decision.
6. **Chosen: a brightness pulse on the lit controls in Field, sized by the onset and
   decaying like a matrix kick, as below.**

## Decision

**What flashes**

1. In Field, each radio onset reported by the decision 0013 detector flashes every
   encoder and standalone button that is lit at that moment. A control that is dark
   stays dark.
2. These lights never flash:
   - a button that is held (it stays at 100%, PRES-LED-02);
   - Button 0 while it breathes during a session (PRES-LED-03);
   - Button 0 while its fault pattern runs (PRES-LED-05), which keeps priority.
3. No light flashes while the session prompt is open, while Shift is held, or in
   Instrument.
4. No light flashes while the radio is not running, is Faulted or is tuning. The
   detector already fires nothing then (decision 0013 item 10).

**How it flashes**

5. A flash raises the control's perceived lightness above its current level and keeps
   its colour: an encoder keeps its hue, with its channels scaled together.
6. A small, medium or large onset raises the lightness by one, two or three thirds of
   the way from the current level to full. The boost then falls by one third of that
   range every 80 ms, continuously, so a large flash returns to the current level in
   240 ms. This mirrors the matrix kick of 1, 2 or 3 pixels decaying by one pixel
   every 80 ms, so the matrix and the lights read as one event.
7. A new onset smaller than the boost still in progress is ignored; an equal or larger
   one restarts the flash at its own size (decision 0013 item 8).
8. The step fractions and the decay time are starting values, configurable until bench
   trials fix them (Principle IV).

**Meaning and load**

9. A flash means a measured radio onset and nothing else. It is never driven by RSSI,
   SNR, a tune or a command sent.
10. The lights renderer reads the onsets as its own reader of the feed, so taking them
    does not steal onsets from Classic's activity hold or the matrix.
11. Lights rendering yields to capture like the matrix (decision 0013 item 15). Under
    load it may coarsen or drop a flash, but it never shows a flash that no onset
    caused.

## Consequences

- `main` gains a third reader of the onset feed for the lights, with the feed's host
  test extended to show that each reader sees every onset once. The flash envelope
  (items 5 to 8) is portable code with host tests: step sizes, decay, the
  smaller-onset rule, and the exemptions in items 2 and 3.
- `presentation.md` gains a lights row for radio onsets, and the Open behavior row for
  EMF, activity and warning expression narrows to EMF and warnings on the direct
  LEDs. That change lands with the code that implements this record.
- The demo image implements this record on the M7 (decision 0011 item 12). It needs
  levels, not on and off, on all twelve encoder channels, so it builds on
  `full_spooky_proto-p04.12`. The lights must update more often than the OLED compose
  (50 ms, 100 ms while capturing) for an 80 ms decay step to read as a fade. Writing
  the timer compare registers is bounded and cheap.
- In the product the lights are rendered on the M4 (decision 0001, target role). This
  record decides what the lights mean, not which core renders them.
- No change to the control map or other product intent is needed. Flashes use the
  colours the controls already show, so control map D-016 is not resolved by this
  record.
- Revisit this record after the bench run if flashes on the encoders hide the page
  colour, if the flashes look busy on a noisy band, or if an 80 ms step is not visible
  on camera.

## Evidence

- None yet. Host tests and the demo-image bench check follow the tasks above.
