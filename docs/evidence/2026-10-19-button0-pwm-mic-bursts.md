# Demo image: Button 0's LED PWM puts 160 ms click bursts in the microphone (p04.31)

Date: 2026-10-17 to 2026-10-19 (local, UTC-6; the group sweep ran 2026-10-19 16:41 to
16:43 UTC, the per-channel sweeps 2026-10-20 03:08 to 03:47 UTC)  
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
enough to a PDM clock edge to corrupt the bit captured there. The layout section
below shows the coupling is onto the clock line, PC2. Only one of the two edges in each
PWM period does this (see the per-channel sweep).

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
was built but could not be flashed at first (it was later flashed through the on-board
ST-LINK; see the per-channel sweep). OpenOCD could not read the debug port's IDR in three flash
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

## Per-channel sweep (`F0060` to `F0079`)

**Flash.** With the enclosure top off, the operator found the Pico-to-Nucleo SWD
leads correct by continuity, with SWDIO at 3.3 V and SWCLK low at both ends. The probe
was reflashed with the debugprobe UF2 (CMSIS-DAP 2.0.0). The handshake still failed:
OpenOCD at `-d3` logged `SWD ack not OK ... JUNK` for every JTAG-to-SWD and
dormant-to-SWD sequence. Its UART path (GP4/GP5) delivered clean target logs, so
the probe's USB link, its ground and the target were all good.

The Nucleo's on-board ST-LINK connected at once: JP1 (`STLK_RST`) removed, Pico USB
unplugged, ST-LINK V3J17M10, STM32CubeProgrammer 2.23.0 hot-plug connect, device
0x450 Rev V, 3.26 V. JP1 had been fitted throughout, including for the successful
Spooky Bench flashes on 2026-10-17. So the target's debug port is fine and the fault
is on the Pico side, most likely its SWD pins (GP2/GP3). That is not yet confirmed
with a spare probe or a scope.

`p0431-demo-20261019a` was flashed through the ST-LINK with STM32CubeProgrammer
(`-d <elf> -v` for each core, then `-rst`) rather than Spooky Bench. The ELF hashes
matched the build declaration before programming (CM7 `6215fb4d...`, CM4
`7f23d9c8...`), and both downloads verified (CM7 at `0x08000000`, CM4 at
`0x08100000`). After the reset, `DIAG IDENTITY` reported
`BUILD=p0431-demo-20261019a BOOT=4`, and `DEMO LIGHTS` reported `RAW=OFF`. Spooky Bench's
flash record does not exist for this image.

**Sweep.** `host/tools/pwm_mic_sweep.py sweep --port COM3 --seconds 4` turned the
rolling stream off. For each step it set `DEMO LIGHTS RAW <mask> <duty> 1000`,
recorded a 4 s Field session, fetched it with the CRC transfer, and analyzed the
microphone over 25 beats. Every other LED held duty 1000, with no edges. The controls
started on the Classic screen with no one touching the rig.

| Step | Session | Verdict | Clicks/s | In one beat window | Beats with clicks |
| --- | --- | --- | --- | --- | --- |
| steady-on (mask 0, all 1000) | `F0060` | clean | 0 | - | 0/25 |
| steady-off (mask 0, all 0) | `F0061` | clean | 0 | - | 0/25 |
| all fourteen at 500 | `F0062` | bursts | 55.1 | 96% | 25/25 |
| BTN0 at 500 | `F0063` | bursts | 52.9 | 92% | 25/25 |
| BTN1 at 500 | `F0064` | clean | 0 | - | 0/25 |
| each of ENC0_R to ENC3_B at 500, alone | `F0065` to `F0076` | clean, all twelve | 0 | - | 0/25 |
| BTN0 at 100 | `F0077` | bursts | 59.1 | 91% (9% in the adjacent window) | 25/25 |
| BTN0 at 900 | `F0078` | bursts | 60.8 | 94% (6% in the adjacent window) | 25/25 |
| steady-on-end | `F0079` | clean | 0 | - | 0/25 |

Afterwards the script set `RAW OFF` and `ROLL ON`; the lights returned to `IDLE=400
ENCODER=800 BALANCE=1000,450,350`.

**Which edge.** In PWM mode 1 the rising edge is at the counter reset and the falling
edge CCR x 3 timer ticks later. The PDM period is 20.833 ticks, so the falling edge
sits (3 x CCR / 20.833) mod 1 of a beat after the rising edge:

- 0 at duty 500: 1,500 ticks is exactly 72 PDM cycles, so the edges cannot be told
  apart;
- 0.4 of a beat (64 ms) at duty 100;
- 0.6 of a beat (96 ms) at duty 900.

If both edges disturbed the data, the 100 and 900 steps would show two trains 64 ms
apart. Each shows one train, with the remainder in the window next to it, so only one
edge disturbs the data. Recordings start at arbitrary points in the beat, so this
sweep cannot say whether it is the rising or the falling edge.

**Result:** confirmed per channel. Button 0 (PF6, TIM16 CH1) is the only one of the
fourteen LED PWM channels that disturbs the microphone, by one of its two edges, at
every duty tried (100, 500, 900). Button 1 and each of the twelve encoder channels are
clean on their own.

## Button 0 with its LED disconnected (`F0080` to `F0084`)

Button 0 is the only one of the fourteen LEDs whose PWM causes this. Yet some backplane
PWM traces pass closer to CN11_35 and CN11_37 (PC2 and PC3) than Button 0's, and the UI
board sits about a foot from the audio shield. The operator found that PF6 also goes up
into the audio shield through the Zio headers, though not close to the PDM traces on
the shield. They then pulled Button 0's shunt jumper on the backplane. That cuts PF6
off from the UI cable, the UI board and the LED, leaving only the pin, the Nucleo
trace, the Zio header into the shield and the backplane stub up to the jumper.
`pwm_mic_sweep.py sweep --channels BTN0 --follow-up ""` then ran 2026-10-20 03:44 to
03:47 UTC:

| Step | Session | Verdict | Clicks/s | In one beat window | Beats with clicks |
| --- | --- | --- | --- | --- | --- |
| steady-on | `F0080` | clean | 0 | - | 0/25 |
| steady-off | `F0081` | clean | 0 | - | 0/25 |
| all fourteen at 500 | `F0082` | bursts | 58.6 | 92% | 25/25 |
| BTN0 at 500 | `F0083` | bursts | 60.1 | 95% | 25/25 |
| steady-on-end | `F0084` | clean | 0 | - | 0/25 |

**Result:** the bursts do not need Button 0's LED, cable or UI-board load. With the jumper
out they are as strong as with it fitted (`F0063`, 52.9 clicks/s). The steady-off
control keeps every timer running with no pin edges, and it is clean. So the cause is
PF6's own voltage edge, coupled upstream of the jumper: on the MCU, the Nucleo board,
the Zio header or the audio shield. PF7, PF8 and PF9 are PF6's neighbours on the MCU
and do not couple, which makes coupling inside the package unlikely. The leading
candidate is the PF6 trace on the Nucleo passing close to the PC2 or PC3 routing.

## Coupling path on the Nucleo layout

The operator traced the nets in ST's NUCLEO-H755ZI-Q (MB1364) layout files. On an
inner layer, the PF6 trace leaves its via beside CN11_35, wraps around pins 35 and 36,
and then runs immediately alongside the PC2 trace for a long stretch. PC2 heads for the
Zio connector (A4) past CN9 pins 9 and 10, and the two traces turn together. PF7 passes
close to the CN11_35 and CN11_37 pads on the top layer but never runs parallel to PC2
or PC3, consistent with Button 1 being clean.

PC2 is the DFSDM1 clock output to the microphone (PC3 is its data), so PF6's edges couple
onto the **PDM clock**. A coupled glitch can only add or shift a clock edge if it lands
next to the clock edge that the microphone or DFSDM1 acts on, and only one glitch
polarity does so. That would explain why only one of PF6's two edges matters, and only
during the part of each 160.6 ms beat when the clocks line up. The explanation fits
every result above but has not been checked with a scope on PC2.

Nothing in the backplane or UI-board layout is involved. The coupling is on the Nucleo
board itself, so any Demo image that PWMs PF6 on this Nucleo will show it.

## Not covered

- A scope capture of the glitch on PC2 (PDM clock) during `DEMO LIGHTS RAW 1 500`.
- Which of Button 0's edges disturbs the data. A recording that changes Button 0's
  duty partway through would show it: the rising edge's train keeps its phase, and the
  falling edge's train moves.
- The Pico's SWD fault (spare probe or a scope on GP2/GP3).
- A fix (`full_spooky_proto-p04.32`).

## Artifacts

- Analyzer and sweep: `host/tools/pwm_mic_sweep.py`, tests
  `host/tests/test_pwm_mic_sweep.py`.
- Group run: `build/pwm-groups-p0431-20261019b/` (`cli.jsonl`, `groups.json`,
  `groups-analysis.json`); capture `CAPS/C044` on the bench card.
- Per-channel run: `build/pwm-sweep-p0431-20261019a/`; jumper-out run `build/pwm-sweep-p0431-20261019b/` (`cli.jsonl`, `summary.json`,
  `run.log`, the twenty fetched WAVs with their CRC32s); ST-LINK flash log
  `build/flash-p0431-stlink-20261019.log`; build declaration
  `build/p0431-demo-20261019a.json`.
- Flash and probe attempts through the Pico: `build/flash-p0431-demo-20261019a.json` to `c.json` and
  `build/probe-p0431-20261019a.json` to `c.json`; Spooky Bench runs
  `2026-10-19T163055.104366_0000-7e706d6b`, `2026-10-19T163327.169472_0000-27dd1af1`,
  `2026-10-19T163522.897476_0000-49b76099` (flash).
