# 0005. Button 0 session prompt and standalone button lighting

- **Status:** Accepted 2026-09-27
- **Date:** 2026-09-27
- **Supersedes:** none
- **Beads:** `full_spooky_proto-54w.18` (proposal), `full_spooky_proto-54w.1` (command
  policy), `full_spooky_proto-54w.2` (gestures)

## Context

The roadmap waits on a physical binding for starting and stopping a session. The user
directed one on 2026-09-27: Button 0 in both operating modes, through a hold-and-confirm
prompt, with defined lighting for the two standalone buttons.

Constraints from the existing design:

- Shift plus Button 0 switches operating mode in both modes, and Shift plus Buttons 0
  and 1 is the Field save, switch and load chord (control map C-008, C-010, C-028).
- Holding the Encoder 0 button jumps to Manual and back in Field (C-011, C-012).
- Instrument Mode had an open Button 0 press for the microphone-injection mix policy
  (C-031, D-013). The user considers that idea unsettled and wants it deferred.
- Principle I requires explicit confirmation before a control interrupts a recording.
  Principle II requires display, lights and audio to agree, and forbids showing success
  before the outcome is known. Principle III forbids latched controls after lost input.
  Principle IV keeps unsettled timings configurable.
- Every UI-board LED pin was chosen for a PWM-capable timer, so dimming and breathing can
  run in hardware.

## Options

1. **Toggle on a short press of Button 0.** Rejected. A brush against the button would
   start or stop a session, and stopping needs explicit confirmation.
2. **A Shift chord.** Rejected. Shift plus Button 0 already switches operating mode.
3. **Toggle after a long hold, without confirmation.** Rejected. A long hold during
   performance could still stop a session by accident; a second, deliberate control is
   safer.
4. **Hold Button 0 to open a prompt, confirm with the Encoder 0 button, cancel by
   releasing Button 0.** Chosen.

## Decision

Items marked *(agent)* were proposed by the agent to close gaps in the user's direction.
Everything else is the user's direction.

**Gesture**

1. In Field or Instrument, pressing Button 0 without Shift held starts a hold. When the
   hold reaches the session hold threshold, a prompt opens: a start prompt when no
   session is active, a stop prompt when one is.
2. While Button 0 is held and the prompt has not yet appeared, every other control
   gesture is swallowed.
3. While the prompt is open, only two actions are recognized. A press of the Encoder 0
   button confirms, and releasing Button 0 cancels. Everything else is dismissed.
4. Confirming issues StartSession or StopSession through the shared command policy.
   Feedback follows the session's published outcome, not the confirmation.
5. *(agent)* After confirming, the rest of the Button 0 hold is swallowed until Button 0
   is released.
6. *(agent)* If the session state changes while the prompt is open, for example because
   a session ends on its own, the prompt closes without issuing a command, and the rest
   of the hold is swallowed.
7. A short press of Button 0 has no action in Field or Instrument.
8. The release of a control already held when Button 0 went down is always delivered,
   and input reconciliation closes any prompt without issuing a command (Principle III).
9. A release is interpreted in the context where its press began. A release whose press
   was swallowed or dismissed is swallowed too.
10. The session hold threshold stays configurable until bench trials set it (Principle
    IV, control-map D-006).

**Lighting**

11. Each standalone button LED holds about 10% perceived brightness while its button is
    not pressed and no session is active, and 100% while its button is held.
12. While a session is active, Button 0 breathes slowly when not pressed. Active includes
    finalizing, so breathing lasts until the session's files are closed.
13. Perceived brightness is CIE L\* lightness. Firmware converts it to PWM duty through
    the inverse L\* curve, so 10% perceived brightness is roughly 1% duty. The breathing
    period stays configurable (Principle IV).
14. *(agent)* While a session is active, Button 1 keeps the idle rule: 10% when not
    pressed, 100% while held.
15. *(agent)* A rejected start or an aborted session shows a distinct fault pattern on
    Button 0, such as three fast blinks, before the idle level resumes.
16. *(agent)* While the prompt is open, the Encoder 0 LED lights to show where to
    confirm.

**Deferred**

17. The Instrument microphone-injection mix policy is removed from the plan until the
    idea is developed. Button 0 carries no Instrument performance action.

## Consequences

- The InputResolution region gets its first machine: the Button 0 hold and prompt
  ([input-resolution.md](../design/behavior/input-resolution.md)).
- The standalone button LEDs need a hardware PWM driver with an L\* table. The per-pin
  timer and channel should be recorded in the prototype hardware document when that
  driver is written.
- Protected product documents lose the Button 0 mix-policy text, open decision 15,
  control-map row C-031 and decision D-013, and gain the new bindings. That change needs
  its own approval.
- What duration a physical start requests stays open under `54w.1`.
- Revisit if bench trials show the two-control confirmation is awkward in performance,
  for example on a one-handed grip.

## Evidence

None yet. Host tests for input resolution belong to `54w.3`; bench checks of LED levels
and hold timing belong to the UI integration work.
