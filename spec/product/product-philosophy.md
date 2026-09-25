# Product philosophy

Spooky Box should feel like a field instrument: responsive, legible, and
expressive, with behavior a user can learn by using it. It combines radio
scanning, microphone capture, magnetic-field sensing, session recording, and
visual feedback. The experience may be evocative or mysterious, but the
instrument should not fabricate unexplained sensor meaning.

This document describes enduring product intent. Concrete hardware ownership,
transport, and firmware state are in [architecture.md](../../docs/design/architecture.md).

## Instrument first

Controls should manipulate understandable quantities (position, range, rate,
sensitivity, mode, and recording state) rather than merely trigger effects.
Response should be immediate enough to invite performance. A user should be
able to develop intuition about what a knob or button will do before looking
at the display.

Manual tuning and automatic scanning should have different character. Manual
tuning can feel weighted and tactile; a scan can feel rhythmic and procedural.
Neither should conceal what frequency territory the radio is actually using.

## Stable meaning, variable texture

Core signals retain their meaning across modes:

| Signal | Meaning |
| --- | --- |
| EMF level | Disturbance relative to a calibrated ambient magnetic baseline, not an absolute claim about its cause. |
| Radio activity | Measured change or activity in incoming radio audio. |
| Scan position | The radio engine's position within its selected territory. |
| Session state | Whether the device is idle, armed, recording, finalizing, or reviewing. |

Presentation may vary by mode, but it must not silently redefine these
signals. In field mode, the LED matrix primarily conveys EMF level; radio
activity adds movement, shimmer, or transient texture. A display can explain
the exact mode and measurements, while the matrix gives them an immediate
physical presence.

## Meaningful interaction

- A control's physical axis should map to a consistent conceptual axis where
  possible. The proposed classic-mode scheme treats one encoder as **motion**
  and the other as **scale/territory**; its exact mapping remains subject to
  testing with the UI hardware.
- A band change, calibration, recording start/stop, or fault is a real state
  transition. Audio, lights, and display should acknowledge it coherently.
- Feedback should identify uncertainty and failure honestly. A missing SD card
  or failed recording should not look like a successful session.
- The interface should remain useful without requiring constant screen reading.

## Recording is a promise

When the device says it is recording, preserving the audio is more important
than animation smoothness. If the system cannot keep that promise, it should
make the failure visible and retain whatever valid data can be recovered.
Visual workloads may slow down or simplify before they compromise capture.

## Architecture-independent presentation

The application should publish semantic facts and events, not pixel-by-pixel
instructions. The UI may render them on a TFT, LED matrix, button LEDs, or a
later display technology. The product character should survive changes in
driver chips, board layout, or core ownership.

The initial 13x9 matrix breakout can present a centered logical 9x9 surface,
matching the likely square final display without making application behavior
depend on that specific breakout.

## What this document does not decide

This is not a feature-completion checklist or a claim that an EMF anomaly has
a paranormal interpretation. Scan algorithms, UI timing targets, session
metadata, exact control maps, and acceptance criteria need separate,
testable requirements as those features are implemented.

The original [STM32 pivot reference](../../reference/legacy_docs/spooky_box_architecture_stm32h745_pivot.md)
contains more exploratory examples. The earlier
[Teensy/RP2040 PDF](../../reference/legacy_docs/Spooky%20Box%20Design%20Philosophy%20Requirements%20Architecture.pdf)
is retained as historical design context, not the current compute plan.
