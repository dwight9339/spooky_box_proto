# Demo image: Button 0's LED PWM puts 160 ms click bursts in the microphone (p04.31)

Date: 2026-10-17 to 2026-10-19 (local, UTC-6; the group sweep ran 2026-10-19 16:41 to
16:43 UTC)  
Beads issue: `full_spooky_proto-p04.31`

Demo evidence qualifies the demo image only; it is not milestone evidence (roadmap,
demo track).

## Symptom

Listening to the microphone track of Field session `F0049` (`p0428-demo-20261017a`),
the operator heard noise at regular intervals. In the WAV it is a burst of about 20 ms
of short spikes every 160.63 ms (7,710 frames): five to nine clusters, each two to five
samples wide and 300 to 800 counts, on a floor of about 7 counts RMS. The radio
channels show nothing at the same times. The spike shape is what one flipped PDM bit
gives after the Sinc4, OSR 64 filter, so the disturbance reaches the PDM data, not the
acoustic path. The REC138 inspection in
[the Field on M7 evidence](2026-10-04-field-on-m7-demo.md) already logged a microphone
peak of 800; RMS and peak alone did not show the pattern.

Every Demo session on the bench card has the bursts, at the same period (`F0045` to
`F0059`, builds `p0412`, `p0427`, `p0428` and `p0429`, and the captures `C042` and
`C043`). The product and qualification images on the same card (`F0040` to `F0044`)
have none.

## Mechanism

The Demo image drives all fourteen button and encoder LEDs as hardware PWM
(`p04.3`, `p04.12`): TIM1, TIM2, TIM4, TIM12, TIM13, TIM14, TIM16 and TIM17, each at
64 MHz HSI / (PSC 2 + 1) / (ARR 999 + 1) = 21,333.33 Hz. The microphone's 3.072 MHz PDM
clock is exactly 144 times that, but it comes from PLL3, whose fractional divider
(`PLL3N = 24`, `FRACN = 4719`) runs 2.03 ppm above 24.576. Both clocks come from the HSI,
so its error cancels. A PWM edge therefore slips one PDM clock cycle every
1 / (3.072 MHz x 2.0266 ppm) = 160.63 ms, or 7,710.1 frames. That is the measured
period to within 0.1 frame. For about an eighth of each slip, the edge falls close
enough to the moment DFSDM1 samples PC3 to flip bits.

Product images drive the LEDs as GPIO on and off, with no PWM edges.

## Isolation

The operator's tests with the card read on the PC (`F0049` to `F0059`, `C042`, `C043`)
were analyzed offline:

| Test | Result |
| --- | --- |
| Matrix feedback off (`F0050`, `F0051`) | Bursts; the matrix is not the cause |
| `DEMO LIGHTS ENCODER 0` (`F0053`) | Bursts |
| `DEMO LIGHTS IDLE 0` in a session (`F0054`) | Bursts; Button 0 breathes during a session regardless of `IDLE` |
| Button 0 held three times in a session (`F0058`) | No bursts for each hold (3.4 s, 4.0 s, 4.0 s), resuming on release; Button 1 kept switching at idle 400 throughout |
| Every stop gesture (`F0049` to `F0054`) | No bursts for the 1.3 to 2.1 s that Button 0 is held before `session_end` |
| Encoders at duty 1000, buttons at idle, no session (`C042`) | Bursts |
| Every LED at duty 1000, no session (`C043`) | Bursts stop when the setting takes effect at 26.9 s; clean for the remaining 32 s |

A held button is driven at duty 1000, above ARR, so its pin stays high with no edges.
The bursts do not depend on Button 0's breathing duty: in mode 1 every period begins with
a rising edge at a fixed phase.

## Group sweep (`C044`)

The firmware for a per-channel sweep (`DEMO LIGHTS RAW`, image `p0431-demo-20261019a`)
was built but not flashed. OpenOCD could not read the debug port's IDR in three flash
attempts and three read-only `probe` runs, at 1 MHz and at 100 kHz, including after a
board reset and after the probe-side SWD cable was reseated. The board side is inside
the enclosure. Nothing was written, so the board kept
`p0428-demo-20261017a` (`OK IDENTITY ... BUILD=p0428-demo-20261017a BOOT=5`).

`host/tools/pwm_mic_sweep.py groups` ran the sweep on that image instead, with no
session, on the Classic screen (all four encoders lit). It stepped the light settings
through one rolling window, 6.5 s per step, then `ROLL SAVE` pinned it as `C044`
(3,088,384 frames). The operator then read the card on the PC. Each step was analyzed
over its middle 4.5 s, 28 beats:

| Step | Switching at about 50% duty | Clicks/s | In one 20 ms beat window | Beats with clicks |
| --- | --- | --- | --- | --- |
| control-on | none, all steady on | 0 | - | 0/28 |
| buttons | BTN0, BTN1 (`IDLE 750`) | 62.9 | 92% | 28/28 |
| enc-red | four reds (`BALANCE 500 1000 1000`) | 0 | - | 0/28 |
| enc-green | four greens | 0 | - | 0/28 |
| enc-blue | four blues | 0 | - | 0/28 |
| enc-all | all twelve encoder channels | 0 | - | 0/28 |
| control-off | none, all steady off | 0 | - | 0/28 |
| control-on-end | none, all steady on | 0 | - | 0/28 |

Clicks per half second over the whole capture start and stop at the step boundaries.
There are clicks up to 10.0 s (default settings, buttons at idle 400), none through
control-on, clicks from 16.5 to 23.5 s (buttons), and none for the last 40 s.
Afterwards the script restored `IDLE=400 ENCODER=800 BALANCE=1000,450,350`, and the
stream was stopped with `ROLL OFF` so the card could be removed.

**Result:** of the fourteen PWM channels, only Button 0 (PF6, TIM16 CH1) disturbs the
microphone. The twelve encoder channels contribute nothing, alone by colour or all
together. Button 1 contributes nothing alone (`F0058`). Button 1 (PF7, TIM17) is on the
neighbouring MCU pin and switches at the same frequency, so the coupling is unlikely to
be at the MCU pins. The backplane does not route PC2 and PC3 (CN11_35 and CN11_37 are
unconnected there). The path is more likely along Button 0's LED net (backplane, UI
cable, UI board) or that LED's current return.

## Not covered

- Each encoder channel alone, and which PWM edge disturbs the data (the RAW sweep,
  after the SWD connection is fixed).
- Button 1 alone at about 50% duty; `F0058` covers it at duty 113 only.
- The coupling path on hardware (scope on PC3 while Button 0 switches).
- A fix.

## Artifacts

- Analyzer and sweep: `host/tools/pwm_mic_sweep.py`, tests
  `host/tests/test_pwm_mic_sweep.py`.
- Group run: `build/pwm-groups-p0431-20261019b/` (`cli.jsonl`, `groups.json`,
  `groups-analysis.json`); capture `CAPS/C044` on the bench card.
- Flash and probe attempts: `build/flash-p0431-demo-20261019a.json` to `c.json` and
  `build/probe-p0431-20261019a.json` to `c.json`; Spooky Bench runs
  `2026-10-19T163055.104366_0000-7e706d6b`, `2026-10-19T163327.169472_0000-27dd1af1`,
  `2026-10-19T163522.897476_0000-49b76099` (flash).
