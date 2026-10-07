# Instrument transport and step sequencer shell on Granular (demo image)

Date: 2026-10-07 (local, UTC-6; console timestamps are UTC, 17:58 to 18:46)  
Beads issue: `full_spooky_proto-p04.14` (demo track; decisions 0020 items 2, 4, 5, 7 and
8, 0021 item 7, 0026 and 0027)

**This is demo-image evidence.** It covers the opt-in `Demo` preset on
`demo/halloween-2026` only. The transport and step pattern in `Common/` are portable,
but this run does not qualify product sequencer behavior.

The bench was the NUCLEO-H755ZI-Q with the UI board, the radio path and the SD card used
for p04.5 to p04.7. The operator confirmed it was connected and free, drove the physical
controls and listened on the monitor output. Spooky Bench 0.7.0 flashed each image on
Windows. CLI queries went to the target CDC on COM3 through `build/console_p046.py`, with
logs `build/console-p0414-20261007{a,b,c,d}.jsonl`. Searches and soaks ran through
`build/worst_p0414.py` and `build/sweep_p047.py`; their results are
`build/worst-p0414-20261007{b,c}.json` and `build/soak-p0414-20261007{c,d}.json`.

**Result: pass on the fourth image.**
- The operator's walkthrough on image a passed: the transport, the view menu, step
  browse and edit, the settings row, the knob offset and page switches behaved as
  designed, and the tempo was accurate.
- The same run showed the render costing about 2,440 µs at 4 grains, against 1,945 µs
  in the p04.7 sweep. A settings search on image b showed the settings make almost no
  difference; the cost had moved with the build's code layout. Image c aligns the grain
  render to the flash line, and every setting then cost 2,038 to 2,068 µs at 4 grains.
- Image d ships the aligned render with a 2,300 µs budget. A 180 s soak at heavy
  settings with the transport running: longest render 2,071 µs, none over budget, no
  overrun, no fault.

## Images

All four were built clean from `claude/p04.14-sequencer-shell`, preset `Demo`, GNU Tools
for STM32 14.3.1, and flashed with verification. The CM4 image was
`7f23d9c8b1f86ccb4aef393f3bd27a6a074bfa07a6cd602a556f153fc44c3f7f` for all four.

| Build ID | Source | Change | CM7 SHA-256 |
| --- | --- | --- | --- |
| `p0414-demo-20261007a` | `fbe3c86` | Transport, step pattern, view menu, step view, `DEMO SEQ` | `3da0465aae612aa2764c7e93b957f81dbc3295055d77e13a7e06d1efe1bc78d8` |
| `p0414-demo-20261007b` | `3498fe7` | `DEMO GRAIN SET` for sweeps | `d659d5545ca19f3b30624e1b9de9df48a2e31288af50dee5d1d9bf5d9f5fe242` |
| `p0414-demo-20261007c` | `8a02f1f` | `granular.c` functions and loops aligned to 32 bytes | `44ecc3ebf2cc1413b166cc35d6ee1f20b90db3dcedf30f523e87263f50296e66` |
| `p0414-demo-20261007d` | `8d1f8de` | Render budget 2,300 µs | `9d0443e82f1f393a724dceb53d84f50679c3a9098cf1e1ca44d04752ce0d0582` |

Images b to d change only a console command, a compile option for `granular.c` and a
counting threshold, so the walkthrough on image a stands for them. Debug, Release and
Demo build for each; the only warnings are the existing unused CubeMX `MX_*_Init`
functions. Native host tests pass 41/41 (MSVC 14.29), including the new
`transport_test`, `demo_sequencer_test`, a step-view case in `demo_view_test` and a
position-override case in `granular_test`. Host tests are not hardware evidence.

## Design under test

- **Transport** (`Common/Src/transport.c`). Tempo and run state; the position is in
  Q16 beats since the start, rebased on each tempo change. `Transport_Fit` sets the
  tempo and division on every clip load so 16 steps span the clip within 70 to 140 BPM
  (0026 items 2 and 3).
- **Pattern** (`Common/Src/step_pattern.c`). 16 steps with on flags and values, the
  pattern's own division and length, the playhead rule of 0027 item 3 and the even
  sweep Granular starts with (0027 item 5).
- **Sequencer** (`CM7/App/demo_clip.c`). In the radio interrupt's monitor stage the
  render is split where a step starts. An on step pins new grains to its position,
  offset by the knob around 50 %; an off step keeps the previous position. The run
  state and tempo apply at the next radio half, and a fit on load waits for the next
  step while running.
- **Views** (`CM7/App/demo_sequencer.c`, provisional, user calls of 2026-10-07). An
  Encoder 0 click on the main page or an Encoder 2 click in the step view runs or stops
  the transport. A long Encoder 2 press, or an Encoder 3 click in the step view, opens a
  view menu (ENGINE, SEQUENCER) in decision 0009's menu form. The step view follows
  C-072 to C-082 with a Tempo and Division settings row whose edits are live. The OLED
  draws 16 bars with playhead and selection marks.

## Walkthrough (image a)

The operator loaded a 3 s clip with C-010; `DEMO SEQ` then read `BPM_X100=8000 DIV=1/16
FITS=1`, the fit for 3 s. The operator then ran the transport from the main page, offset
the sequence with the position knob, opened the step view from the view menu, browsed,
edited step positions and turned steps off and on, changed and restored the tempo and
the division, returned to the main page through the menu, changed pages while it played,
and stopped it. Everything behaved as expected. The final pattern held five steps off
and edited values, and the operator had left the division at 1/8.

**Tempo.** With the transport started from the console at 80 BPM and 1/8, 161 steps
fired in 60.11 s of host time, against 160.3 expected; the counting resolution is one
step.

## Render cost and the code layout

On image a, at 250 ms grains, 20 a second, pitch 0 and envelope 50 %, 4 grains rendered
at most 2,419 µs with the transport stopped and 2,439 µs running (30 s each). The
sequencer itself therefore adds about 20 µs, but the voice cost about 25 % more than in
the p04.7 sweep, and nearly every render was over the 2,000 µs budget. The diagnostic
log recorded one foreground-budget event: the fuel-gauge service took 15 ms against
its 10 ms budget. There was no radio overrun or rolling fault, and the longest loop pass
was 66 ms.

Image b added `DEMO GRAIN SET`. With the transport running and the limit full, one
parameter was varied at a time for 12 s each:

| Parameter | Values | Longest render, image b | Longest render, image c (aligned) |
| --- | --- | --- | --- |
| Pitch | -24 to +24 semitones (9 values) | 1,997 to 1,999 µs | 2,046 to 2,068 µs |
| Envelope | 0, 50, 100 % | 1,981 to 2,013 µs | 2,039 to 2,047 µs |
| Size | 40 to 500 ms | 1,971 to 2,014 µs | 2,038 to 2,047 µs |
| Size | 10 ms (one grain at a time) | 751 µs | 737 µs |
| Spray | 0, 100 % | 2,014 µs | 2,046 to 2,067 µs |
| Density | 20, 100 a second | 1,968 to 1,987 µs | 2,047 to 2,055 µs |

No run had a radio overrun or a rolling fault. The settings move the cost by a few
percent, so they do not explain image a. The images differ only in foreground code, but
in a rebuild of image a's source `Granular_Render` started 4 bytes into a 32-byte flash
line, and in image b 12 bytes. With no instruction cache, the render's speed depends
on how its loop meets the flash lines. Image c compiles `granular.c` with
`-falign-functions=32 -falign-loops=32` (`Granular_Render` at `0x0803C900`), so every
build fetches it the same way. (The rebuild's hash differs from image a's, so its layout
is close to, not proven identical with, the flashed image.)

## Soaks

Both ran 180 s with the transport running at 80 BPM, 1/16, the limit of 4 full, and
500 ms grains, 100 a second, +12 semitones, full spray and envelope 50 %.

| Image | Longest render | Over budget | Radio overruns, rolling faults | Rolling queues | Longest loop pass | Faults |
| --- | --- | --- | --- | --- | --- | --- |
| c | 2,067 µs | 11,888 (budget 2,000 µs) | 0, 0 | 1/8 | 65 ms | `DIAG LAST NONE` |
| d | 2,071 µs | 0 (budget 2,300 µs) | 0, 0 | 1/8 | 70 ms | `HAS_FAULT=0`, `DIAG LAST NONE` |

On image d the logger dropped nothing and rolling capture kept a 66.0 s window with the
longest block write 59 ms. The longest loop pass is a maximum since boot, which includes
the clip's save and load.

## Not exercised

- The walkthrough on images b to d.
- The matrix: Granular keeps p04.7's grain view, whose dim position column shows the
  knob, not the sequenced position.
- Tempo accuracy at other tempos and divisions on hardware; host tests cover them.
