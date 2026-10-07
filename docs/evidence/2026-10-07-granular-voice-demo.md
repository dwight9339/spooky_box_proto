# Granular voice with provisional Instrument pages (demo image)

Date: 2026-10-07 (local, UTC-6; console timestamps are UTC, 16:51 to 17:29)  
Beads issue: `full_spooky_proto-p04.7` (demo track, decision 0011 item 16, decision 0020
items 1, 6 and 13)

**This is demo-image evidence.** It covers the opt-in `Demo` preset on
`demo/halloween-2026` only. It does not deliver or qualify `full_spooky_proto-v7l.1`;
the grain limit and its measurements hold at the current 64 MHz, cache-off clock.

The bench was the NUCLEO-H755ZI-Q with the UI board, the radio path and the SD card used
for p04.5 and p04.6. The operator confirmed it was connected and free, drove the
physical controls and listened on the monitor output. Spooky Bench 0.7.0 flashed each
image on Windows. CLI queries went to the target CDC on COM3 through
`build/console_p046.py`, with logs `build/console-p047-20261007{a,b,c}.jsonl`. The sweep
steps ran through `build/sweep_p047.py`; their results are
`build/sweep-p047-limit{3,4,5,6}.json` and `build/soak-p047-limit4.json`.

**Result: pass on the third image, after one failure found on the bench.**
- Image a played and its pages and matrix worked, but with 15 to 16 grains sounding the
  render took up to 6.8 ms of each 10.7 ms radio half. The foreground starved, the
  radio queue overran 21 times and rolling capture faulted and restarted 23 times.
- Image b added a runtime grain limit; a sweep with rolling capture running held limits
  3 to 6.
- Image c ships a limit of 4. A 180 s soak at the worst-case settings passed with no
  render over its 2,000 µs budget and no overrun, fault or loop-budget breach.

## Images

All three were built clean from `claude/p04.7-granular-voice`, preset `Demo`, GNU Tools
for STM32 14.3.1, and flashed with verification. The CM4 image was
`7f23d9c8b1f86ccb4aef393f3bd27a6a074bfa07a6cd602a556f153fc44c3f7f` for all three.

| Build ID | Source | Change | CM7 SHA-256 |
| --- | --- | --- | --- |
| `p047-demo-20261007a` | `a6a54f0` | p04.7 rebased onto `e112ec1`, slice quantization removed | `a5c1816831ed3d05ed2c3dc1e690a65e1f830c9b27cb5f0b1e6638a443c5fc66` |
| `p047-demo-20261007b` | `d227603` | Runtime grain limit, default 3; `DEMO GRAIN MAX <n>` | `f2bb934c6295330b8e4926db00c0beecf7dfcc9e8a812e606da4d732abb33577` |
| `p047-demo-20261007c` | `3252932` | Default limit 4, render budget 2,000 µs | `cae416c5d258c1f37735dde8db6d87a8d142f44b10a55258df8df9dac2b12c7f` |

Debug, Release and Demo build for each; the only warnings are the existing unused CubeMX
`MX_*_Init` functions. Native host tests pass 39/39 (MSVC 14.29), including the new
grain-limit test. Host tests are not hardware evidence.

## Design under test

- **Voice.** `Common/Src/granular.c` reads the 24 kHz clip through overlapping grains:
  start interval 1 / density, start at position plus a random spray offset, playback at
  the pitch's rate with linear interpolation, under a trapezoid envelope. It renders
  48 kHz stereo in the monitor stage of the radio DMA interrupt, after the raw capture
  is copied. There is no slice parameter (decision 0020 item 6).
- **Grain limit (images b and c).** At most `max_grains` grains sound at once (1 to 16).
  A grain due at the limit is dropped and counted.
- **Pages (provisional, C-025 not settled).** Page 1: position, size, density, pitch on
  Encoders 0 to 3. Page 2: spray, nothing (shown as "-"), envelope, level. An Encoder 3
  click advances and wraps (C-024).
- **Matrix.** Grain activity instead of EMF: the nine columns span the clip, each
  sounding grain lights its column by its envelope, and the position is a dim column
  (decision 0020 item 13).

## Pages, sound and matrix (image a)

The operator loaded clips with C-010 and heard grains instead of p04.6's plain loop.
Every encoder on both pages changed its value on the OLED and the sound as expected;
page 2's Encoder 1 showed "-" and did nothing; Encoder 3 clicks wrapped the pages. The
matrix followed the grains. The clip loads took 4,926 ms at most after the save.

## Render cost at 16 grains (image a)

With the operator's settings (150 ms grains, 100 a second, +2 semitones, 24 % spray),
`DEMO GRAIN` read `ACTIVE=15 HIGH=16/16 RENDER_US_MAX=6821`, and 47,744 of 60,271 renders
were over the 1,500 µs budget. `DIAG STATUS` read `LOOP_MAX_MS=96 SD_MAX_MS=76
RADIO_OVR=21 HAS_FAULT=1`; the last event was `RADIO_OVERRUN`. `ROLL` read
`STATE=FAULT FAULTS=23 STARTS=23`: rolling capture had faulted on each overrun and
restarted. Throughout its newest 128 events, the diagnostic log held loop stalls of 52
to 78 ms and foreground-budget events for the matrix service (12 ms).

The M7 runs at 64 MHz from HSI with the I- and D-caches off
([foreground latency](../design/foreground-latency.md)), so a grain costs about 0.42 ms
per radio half. 16 grains use about two thirds of the CPU in the radio interrupt.
Raising the clock or enabling the caches is outside this task
(`full_spooky_proto-8lw.23`).

## Grain-limit sweep (image b)

The operator set the worst case: 500 ms grains, 100 a second, +24 semitones, 100 %
spray, 100 % envelope, so every limit stays full. With rolling capture running, each
step set the limit, cleared the render statistics and ran 45 s.

| Limit | Longest render | Share of a radio half | Longest loop pass | Radio overruns, rolling faults | Rolling queues |
| --- | --- | --- | --- | --- | --- |
| 3 | 1,540 µs | 14 % | 59 ms | 0, 0 | 1/8 |
| 4 | 1,945 µs | 18 % | 66 ms | 0, 0 | 1/8 |
| 5 | 2,360 µs | 22 % | at most 66 ms | 0, 0 | 1/8 |
| 6 | 2,784 µs | 26 % | 72 ms | 0, 0 | 1/8 |

The longest loop pass is `DIAG STATUS LOOP_MAX_MS`, a maximum since boot, so each value
bounds its step from above. The diagnostic log keeps only its newest 128 events and each
step recorded about 550, mostly SD writes; those 128 held no loop stall, overrun or
budget event. At 6 the loop came within 3 ms of its 75 ms budget, so 8 was not run.

## Soak at the shipped limit (image c)

At the same worst case and the image's default limit of 4, with no `DEMO GRAIN MAX`
sent, a 180 s run read:
- `DEMO GRAIN`: `ACTIVE=4 HIGH=4/4 RENDER_US_MAX=1902 OVER_BUDGET=0 BUDGET_US=2000`;
- `ROLL`: `STATE=RUNNING FAULTS=0 QUEUES=1,1/8 WRITE_MS_MAX=57 ROTATE_MS_MAX=47`,
  window 67.8 s;
- `DIAG STATUS`: `LOOP_MAX_MS=66 SD_MAX_MS=57 RADIO_OVR=0 PDM_OVR=0 HAS_FAULT=0`;
  `DIAG LAST NONE`; the logger dropped nothing.

## Not exercised

- Sessions while Granular plays: the session prompt is refused in Instrument (p04.6),
  and the voice stops outside Instrument.
- The worst case at limit 4 for longer than 180 s, and the matrix and pages on image c
  (unchanged from image a apart from the limit).
- Host WAV inspection.
