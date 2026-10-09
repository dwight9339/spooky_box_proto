# Slicer step editing in the sequencer view (demo image)

Date: 2026-10-08 (local, UTC-6; console timestamps are UTC, 2026-10-09 01:02 to 01:29)  
Beads issue: `full_spooky_proto-p04.16` (demo track; decision 0022 and the user call of
2026-10-08 that rebuilds an edited pattern on a slice-count change only after `DISCARD?`,
in place of 0022 item 11)

**This is demo-image evidence.** It covers the opt-in `Demo` preset on
`demo/halloween-2026` only and does not qualify product sequencer behavior.

The bench was the NUCLEO-H755ZI-Q with the UI board, the radio path and the SD card used
for p04.5 to p04.15. The operator confirmed it was connected and free, drove the
physical controls and listened on the monitor output. Spooky Bench 0.7.0 flashed and
reset the target on Windows. CLI queries went to the target CDC (COM3) through
`build/console_p046.py`, with logs `build/console-p0416-20261008{a,b}.jsonl` (one per
console session). The soak ran through `build/soak_p0415.py`, unchanged from p04.15;
its result is `build/soak-p0416-20261008a.json`.

**Result: pass on the first image.**
- Slicer steps are editable in the step view: the value runs over the slices and stops
  at both ends, edits are heard at once, steps turn on and off, and an edited pattern
  audibly rearranges the clip.
- Granular's and the Slicer's patterns, each with its own division, survived engine
  switches both ways with the transport running.
- A slice-count change with an edited pattern asked `DISCARD?`; cancelling kept the
  pattern and confirming rebuilt it. A change with nothing edited applied at once.
- The soak stayed within the 2,300 µs render budget in every phase, with no overrun and
  no fault.

## Image

Built clean from `7a34e47` on `claude/p04.16-slicer-steps`, preset `Demo`, GNU Tools
for STM32 14.3.1, and flashed with verification.

| Build ID | Source | CM7 SHA-256 | CM4 SHA-256 |
| --- | --- | --- | --- |
| `p0416-demo-20261008a` | `7a34e47` | `36aeb6350d2565f64958acc9fc4f34399ecf62c353d504208ec6ad3f76685632` | `7f23d9c8b1f86ccb4aef393f3bd27a6a074bfa07a6cd602a556f153fc44c3f7f` |

The CM4 image is the same as p04.14's and p04.15's. Debug, Release and Demo build; the
only warnings are the existing unused CubeMX `MX_*_Init` functions, and Debug and
Release are unchanged (every changed source is Demo-only). Native host tests pass
45/45 (MSVC), including the new step-range, per-engine pattern and confirm cases in
`demo_sequencer_test` and `demo_slicer_test`. Host tests are not hardware evidence.

## Design under test

- **Step values** (`CM7/App/demo_sequencer.c`). The sequencer takes a step's value
  range from the active engine: permille of the clip for Granular, one slice a detent
  within 0 to count − 1 for the Slicer. The display shows slices from SL01.
- **Edited pattern** (`CM7/App/demo_slicer.c`). A step edit sets `PATTERN_EDITED`, and an
  edited pattern counts as an edit, so an Encoder 1 count change asks `DISCARD?` first.
  A second Encoder 1 click applies the count and rebuilds the pattern (each slice fires
  on the step where it begins, the steps between are off); any other gesture cancels and
  is swallowed.
- **Reporting** (`CM7/App/demo_field.c`). `DEMO SLICE` reports `PATTERN_EDITED`.

## Walkthrough

**Start.** `DIAG IDENTITY` reported `BUILD=p0416-demo-20261008a`. The SD card did not
mount (`FR_NOT_READY`, `stage=HAL_INIT`, `hal=0x10000000`) with the battery switched on:
the same signature as p04.15's first image. The operator reseated the card, Spooky
Bench reset the target (`BOOT` 3 to 4), and the card initialized (`stage=OK`). `SD
STATUS` then answered `FR_LOCKED` because rolling capture held the storage lease, which
its SD writes confirmed. This is a bench fault, not a p04.16 result.

**(1) Editing Slicer steps.** The operator loaded a clip with C-010 (fit: 80 BPM, 1/16,
16 slices of 187 ms), chose SLICER in the engine menu, ran the transport and opened
SEQUENCER. In step edit, Encoder 0 ran SL01 to SL16 and held at both ends, each change
was heard at once, and an Encoder 1 click turned the step off and on. A few more edits
audibly rearranged the loop. `DEMO SEQ` then read
`STEPS=1:1,0:1,1:2,1:3,0:4,1:5,1:3,1:4,0:5,1:2,1:2,1:5,0:3,1:6,1:4,1:6` with
`PATTERN_EDITED=1`; the Slicer had made 1,315 triggers with no fade cut short and a
longest render of 730 µs.

**(2) Patterns kept per engine.** With the transport running, the operator switched to
Granular, which played its untouched pattern (16 even steps), edited several of its
steps and set its division to 1/4 (`DEMO SEQ ENGINE=GRANULAR DIV=1/4`), went to the
Slicer and back once, then returned to the Slicer. The Slicer's steps and its 1/16
division were identical to the record from (1), and the rearranged loop played on.

**(3) Count change.** On the Slicer's main page the operator chose 8 slices: `DISCARD?`
appeared, and an Encoder 3 click cancelled it and kept the loop. Choosing 8 again and
confirming rebuilt the pattern to play the clip through on steps 1, 3, 5 and so on.
Choosing 4 then applied at once with no prompt. `DEMO SLICE` read `COUNT=4 EDITED=0
PATTERN_EDITED=0` with four 750 ms slices, and `DEMO SEQ` showed steps 1, 5, 9 and 13
on, firing slices 0 to 3. The console recorded only the final state of (3); the cancel
and the 8-slice state rest on the operator's observation.

Health through the walkthrough: `RADIO_OVR=0 PDM_OVR=0 SD_ERR=0 AUDIO_ERR=0
HAS_FAULT=0`.

## Soak

For a like-for-like comparison with p04.15, the operator first set the Slicer back to 16
slices (applied at once, giving the identity pattern) and Granular's division back to
1/16; Granular's step positions stayed edited. The soak was p04.15's: transport at
80 BPM, 1/16 with rolling capture on; Granular alone at p04.14's heavy settings (500 ms
grains, 100 a second, +12 semitones, full spray, envelope 50 %, limit 4) for 30 s, the
Slicer alone for 30 s, then `DEMO VOICE` alternating every 3 s for 180 s. Each phase
cleared the render statistics first.

| Phase | Renders | Longest render | p04.15 image d | Over budget (2,300 µs) | Slicer triggers, fades cut |
| --- | --- | --- | --- | --- | --- |
| Granular | 2,943 | 2,047 µs | 2,023 µs | 0 | none |
| Slicer | 2,954 | 685 µs | 683 µs | 0 | 168, 0 |
| Switching (60) | 17,059 | 2,048 µs (1,901 µs in renders begun on the Slicer) | 2,045 µs | 0 | 521, 1 |

Renders are counted between each phase's first and last snapshot, as in p04.15. The
render code in `Common/` is unchanged from p04.15; Granular's 24 µs rise is within the
spread that image layout has moved it by before and leaves 253 µs of margin. The one
fade cut short in the switching phase is a second slice starting within 2 ms of the
first, as image c of p04.15 also showed once.

No radio or PDM overrun, no SD or audio error, `HAS_FAULT=0`, `DIAG LAST NONE`. Rolling
capture ran with no fault and no allocation failure, queues at most 1/8, the longest
block write 56 ms and a window of 66.8 to 70.1 s; the longest loop pass was 65 ms. All
127 diagnostic events in the final dump were SD writes.

## Observations

- `DEMO SLICE` keeps reporting the last `SOUNDING` slice while Granular is active (read
  `SOUNDING=6` with `ACTIVE=0` before the soak): the Slicer updates the field only when
  it renders. It is a report quirk, not audible: switching in retriggers the current
  step's slice. Filed as a follow-up.

## Not exercised

- Editing Slicer steps with the transport stopped.
- Slice settings changed in the open slice together with step edits before a count
  change; host tests cover `EDITED` from either source.
- A long soak with an edited, rearranged Slicer pattern; the soak used the identity
  pattern for comparison with p04.15.
