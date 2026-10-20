# Demo image: Button 0 only off or full, no microphone bursts (p04.32)

Date: 2026-10-20 (local, UTC-6; Spooky Bench and log timestamps are UTC, 17:12 to
17:17)  
Beads issue: `full_spooky_proto-p04.32`

Demo evidence qualifies the demo image only; it is not milestone evidence (roadmap,
demo track).

## Change

[The p04.31 evidence](2026-10-19-button0-pwm-mic-bursts.md) found that any partial PWM
duty on Button 0's LED (PF6, TIM16 CH1) puts click bursts in the microphone, about
20 ms every 160.63 ms. On the Nucleo, PF6 runs beside the PDM clock (PC2). The operator
chose a software fix for the demo (user direction 2026-10-20): Button 0 is only ever
off or at full duty, so PF6 never switches while the lights run.

| State | Before | After |
| --- | --- | --- |
| Idle | idle level (`IDLE=400`, duty 113) | off |
| Session | breathing, off to lightness 850 over 4 s | blinks 1 s on, 1 s off |
| Pressed | full | full |
| Shift, mode switch available | idle level | full |
| Session fault | three fast blinks | unchanged |
| Radio onset flash | rose toward full when idle | never |

Button 1 and the encoders are unchanged. `DEMO LIGHTS IDLE` now sets only Button 1's
idle level. `DEMO LIGHTS RAW` can still switch Button 0, for the bench.

This departs from product presentation behaviour on the demo image only.
PRES-LED-01 gives Button 0 an idle level, and PRES-LED-03 and
[decision 0005](../decisions/0005-button-0-session-prompt.md) item 12 have it breathe
during a session. Neither document is changed. On this Nucleo, either behaviour needs
PF6 at a partial duty, which conflicts with Principle I.

## Build and flash

- Source: branch `claude/p04.32-button0-steady` from `demo/halloween-2026` `8801b90`.
  The changes are in `CM7/App/demo_lights.c` and `.h`, `tests/demo_lights_test.c` and
  `docs/design/usb-cli.md`.
- Native host tests 49/49 (MSVC 14.29). In `demo_lights_test`, the button tests now
  expect the new states, and a new sweep checks that Button 0's duty is 0 or 1000 for
  every combination of Shift, session, presses, mode-switch availability, Field or
  Instrument, seven screens, five idle levels, a session fault and large onsets, over
  4 s.
- Debug, Release and Demo build. Demo `p0432-demo-20261020a`: CM7 SHA-256
  `0de36422bb71...`, CM4 `7f23d9c8b1f8...` (unchanged), declaration
  `build/p0432-demo-20261020a.json`.
- Spooky Bench flash, pass, both cores verified (run
  `2026-10-20T171214.411502_0000-4b2b0227`). This was the first flash through the
  replacement Pico (`E66540F0A378602B`, `full_spooky_proto-v4i`). Afterwards
  `DIAG IDENTITY` reported `BUILD=p0432-demo-20261020a BOOT=3`.

## Bench

The operator confirmed the board was connected and free, on the Classic screen with
default lights (`IDLE=400 ENCODER=800 BALANCE=1000,450,350 RAW=OFF`). An ad hoc
driver (`build/p0432-bench/`, using `host/tools/pwm_mic_sweep.py`) ran the steps, and
`pwm_mic_sweep` analyzed each recording:

| Step | Lights | File | Result |
| --- | --- | --- | --- |
| Out of a session: the 60.6 s rolling window since boot, saved with `ROLL SAVE` | default; Button 0 dark | `CAPS/C045` | **clean**: 2 clicks in 377 beats, at 5.0 s and 8.5 s, in no beat pattern |
| A 20 s Field session (`RECORD START 20`) | default; Button 0 blinking | `FIELD/F0085` | **clean**: 0 clicks in 125 beats |
| Positive control, 4 s | `RAW 1 500`: Button 0 at duty 500, all else steady | `FIELD/F0086` | bursts: 55.6 clicks/s, 90% in one beat window, 25 of 25 beats |

The control shows the rig still picks up Button 0's PWM, so the two clean results are
meaningful. The two clicks in `C045` fall around when the operator reconnected the
board's USB, so they are probably handling noise. Before the fix, every Demo session
and capture carried about 50 to 60 clicks per second.

The operator watched Button 0: dark at idle and blinking during the session
recording, as intended. A later hands-on check also passed: full while pressed, and
full while Shift is held with its mode-switch action available.

Afterwards the driver set `RAW OFF`, and the rolling stream was stopped (`ROLL OFF`)
so the card could be read on the PC.

## Not covered

- A full recording regression on this image (the change touches only light duties).
- The product. PRES-LED-01 and -03 still call for partial duty on PF6, and on this
  Nucleo that conflicts with Principle I.

## Artifacts

- `build/p0432-bench/` (`cli.jsonl`, `run.log`, `audio/session-default.WAV` CRC32
  `ca9a555d`, `audio/control-raw-btn0.WAV` CRC32 `d6518864`); capture `CAPS/C045` on
  the bench card.
- `build/flash-p0432-demo-20261020a.json`, `build/p0432-demo-20261020a.patch`.
