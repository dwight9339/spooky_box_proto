# Slicer engine, slice page and tempo matrix (demo image)

Date: 2026-10-07 (local, UTC-6; console timestamps are UTC, 2026-10-08 02:08 to 03:00)  
Beads issue: `full_spooky_proto-p04.15` (demo track; decisions 0020 items 3, 9 to 13
and 17, 0022 items 1 to 9, 0026 and 0027)

**This is demo-image evidence.** It covers the opt-in `Demo` preset on
`demo/halloween-2026` only. The slice map and Slicer render core in `Common/` are
portable, but this run does not qualify product Slicer behavior.

The bench was the NUCLEO-H755ZI-Q with the UI board, the radio path and the SD card used
for p04.5 to p04.14. The operator confirmed it was connected and free, drove the
physical controls and listened on the monitor output. Spooky Bench 0.7.0 flashed each
image on Windows. CLI queries went to the target CDC through `build/console_p046.py`,
with logs `build/console-p0415-20261007{a,b,c,d,e}.jsonl` (one per console session).
The soaks ran through `build/soak_p0415.py`; their results are
`build/soak-p0415-20261007{c,d}.json`.

**Result: pass on the fourth image.**
- The operator's walkthrough passed: a loaded clip first sounded like itself looping on
  the Slicer, the slice page controls all worked (none were cut), the matrix hue
  tracked the bar and froze on stop, and engine switches both ways landed on the main
  page in time with a short dip and no click.
- Three faults were found and fixed during the run: the matrix's top row lagged
  (image b), a slice-count round trip lost the identity pattern (image c), and the
  grain render had become about 18 % slower than in p04.14 (image d).
- Image d's soak, with rolling capture on and the transport running: Granular alone
  2,023 µs at most, the Slicer alone 683 µs, 60 engine switches in 182 s 2,045 µs; none
  over the 2,300 µs budget, no overrun, no fault.

## Images

All four were built clean from `claude/p04.15-slicer-engine`, preset `Demo`, GNU Tools
for STM32 14.3.1, and flashed with verification. Images b to d were built from the
working tree on `c13c0fa`, and each manifest carries that diff as its source snapshot
(`build/p0415-demo-20261007{b,c,d}.patch`). Each snapshot is byte-identical to
`git diff c13c0fa` at the commit listed below. The CM4 image was
`7f23d9c8b1f86ccb4aef393f3bd27a6a074bfa07a6cd602a556f153fc44c3f7f` for all four, the
same as p04.14's.

| Build ID | Source | Change | CM7 SHA-256 |
| --- | --- | --- | --- |
| `p0415-demo-20261007a` | `c13c0fa` | Slicer engine, page, engine menu, tempo matrix, `DEMO SLICE` | `be40d538f1984ffa6222f7bd9d328a367aed66df3d8ee3f19afc340ebdcfa6b6` |
| `p0415-demo-20261007b` | `17c50ba` | The Slicer's matrix view waits for a frame to be fully shown | `41030c2f072b76999e585c2aa629ea4ac8e1deb12d8f6820c23b7ba4d813e816` |
| `p0415-demo-20261007c` | `7e2b90a` | A slice-count change rebuilds the pattern to play the clip through | `9f0bd1880416fe97d0f8488db3bb762b1e346efe94cb143cdecc57371ed0b569` |
| `p0415-demo-20261007d` | `885d4c7` | The grain and slice renders clear their mix without `memset` | `c9fb0160e01f28556d28c2f39deb0db76fc0a573a7b407c9523390d72ec2c3a2` |

Debug, Release and Demo build; the only warnings are the existing unused CubeMX
`MX_*_Init` functions, and Debug and Release are unchanged (every new source is
Demo-only). Native host tests pass 45/45 (MSVC 14.29, no warnings), including the new
`slice_map_test`, `slicer_test`, `demo_slicer_test` and `tempo_matrix_test`, and Slicer
cases in `demo_sequencer_test` and `demo_view_test`. Host tests are not hardware
evidence.

## Design under test

- **Slice map and Slicer** (`Common/Src/slice_map.c`, `Common/Src/slicer.c`). Slices
  cover the clip without gaps; one voice; a trigger cuts the slice sounding with a 2 ms
  crossfade; pitch by playback rate (±12 semitones), gate as a share of the pitched
  slice, level; at most two voice reads per frame.
- **Engines and switching** (`CM7/App/demo_clip.c`). Granular and the Slicer both start
  on a clip and render one at a time in the radio interrupt's monitor stage. A switch
  fades the old engine out over 5 ms and the new one in over 5 ms (0027 item 2's
  fade-out, fade-in form); only the active engine's pattern fires, and the engine
  switched in joins the current step in time. The Slicer's pattern starts as the
  identity pattern, played at the tempo the clip load fits (0026).
- **Pages** (`CM7/App/demo_slicer.c`, `demo_sequencer.c`; provisional, user calls of
  2026-10-07). Slicer main page: Encoder 0 turn selects and, stopped, auditions a slice;
  Encoder 0 hold opens it (pitch, gate, level, length on Encoders 0 to 3); Encoder 1
  chooses 4, 8 or 16 slices, confirmed over edits. The engine menu opens on a long
  Encoder 1 press from either engine's main page. The step view shows the Slicer's
  pattern read-only until p04.16.
- **Matrix** (`CM7/App/tempo_matrix.c`). While the Slicer is active: one colour turning
  through the spectrum once a bar, frozen while stopped, dark with no clip.

## Walkthrough

**Image a.** The SD card did not mount at first (`FR_NOT_READY`, `hal=0x10000000`) with
the battery switched on and the card seated, through several resets; it mounted only
after the operator took the card out and put it back, then reset. C-010 then reported
`CLIP: NO BUFFER` until the card mounted. This is the signature p04.3 recorded with the
battery off, but this time the battery was on. It is a bench fault, not a p04.15
result.

The operator loaded a 3 s clip with C-010 (fit: 80 BPM, 1/16), opened the engine menu,
chose SLICER and ran the transport. Navigation behaved as designed, and the first loop
sounded like the clip. After about 2,000 steps `DEMO SLICE` read 1,926 triggers, no
fade cut short and a longest Slicer render of 771 µs.

The matrix was wrong: its top row lagged the rest, and the top-left LED showed an
unrelated colour. `UI MATRIX FEEDBACK` showed 69 of 73 frames superseded in 4 s. Every
Slicer frame changes all 81 LEDs, which the writer sends as 18 I2C runs; about 11 fit in
a 40 ms frame, and the writer starts each frame from panel row 0. The panel is mounted
rotated 180 degrees, so the logical top row and the top-left pixel (a one-pixel run)
come last and were rarely written. Image b makes the Slicer view hand over a new frame
only once the last one is fully shown. The writer itself is product code, so the fix
for it is `full_spooky_proto-54w.37`.

**Image b.** The operator reloaded the clip. The whole matrix turned as one colour
(4 frames superseded over the session) and froze on stop. Every slice page control
worked: audition on select while stopped, the Encoder 0 hold, pitch, gate, level and
length audible while running, and 16 to 8 to 4 slices with `DISCARD?` over edits.

Returning to 16 slices played as if there were still 4. The pattern read
`0,0,0,0,4,4,4,4,8,8,8,8,12,12,12,12`: 0022 item 11 moves each step to the new slice
holding its old slice's start, so 16 to 4 collapsed the identity pattern and 4 to 16
could not restore it. The Slicer's steps are read-only in this image, so nothing could
undo it. The user chose (demo-track rule 5) to rebuild the pattern on a count change
instead: each slice fires on the step where it begins and the steps between are off, so
the clip plays through at any count and 16 slices give the identity pattern again.
Image c carries it, and p04.16 decides how an edited pattern follows a count change.

**Image c.** The operator confirmed that 4 slices play the clip through and that 16
restores the identity pattern. Engine switches both ways while running dipped briefly
without a click, landed on the main page and stayed in time, with the Slicer joining
mid-pattern; a long Encoder 1 press in the step view did nothing.

## Render cost and `memset`

On image c the walkthrough left Granular's render at 2,447 µs at most and over budget
2,876 times, against p04.14's 2,071 µs and none. The soak confirmed it (below). The
grain render's code was byte-identical to p04.14's and aligned to the same 32-byte flash
line. What had moved was newlib-nano's `memset`, which the render calls on its 2 KB mix
each half and which stores one byte per pass: in p04.14's image its loop sat inside one
flash line (`0x08041048` to `0x08041053`), and in image c it straddled two
(`0x08043878` to `0x08043883`). With no instruction cache, each of 2,048 passes then
fetched two lines. Image d clears the mix with a word loop in `granular.c` and
`slicer.c`, compiled with `-fno-tree-loop-distribute-patterns` so GCC keeps it a loop;
neither render calls `memset` any more, and `Granular_Render` stays at `0x0803E6C0`.

## Soaks

Each soak had three phases, with the transport running at 80 BPM, 1/16 and rolling
capture on: Granular alone at p04.14's heavy settings (500 ms grains, 100 a second,
+12 semitones, full spray, envelope 50 %, limit 4) for 30 s, the Slicer alone (16 equal
slices) for 30 s, then `DEMO VOICE` alternating every 3 s for 180 s. Each phase cleared
the render statistics first.

| Image | Phase | Renders | Longest render | Over budget (2,300 µs) | Slicer triggers, fades cut |
| --- | --- | --- | --- | --- | --- |
| c | Granular | 2,961 | 2,435 µs | 3,065 | none |
| c | Slicer | 2,951 | 1,182 µs | 0 | 168, 0 |
| c | Switching (60) | 17,105 | 2,436 µs (2,406 µs in renders begun on the Slicer) | 8,548 | 517, 1 |
| d | Granular | 2,963 | 2,023 µs | 0 | none |
| d | Slicer | 2,955 | 683 µs | 0 | 168, 0 |
| d | Switching (60) | 17,050 | 2,045 µs (1,977 µs in renders begun on the Slicer) | 0 | 520, 0 |

Renders are counted between each phase's first and last snapshot; the maxima and the
over-budget counts also cover the 2 s settle after each phase's reset, so image c's
Granular count includes about 190 more renders. A render begun on the Slicer can end
on Granular when a switch completes inside it, so its maximum is close to Granular's. A
fade cut short means a second slice started within 2 ms of the first.

Both soaks were healthy apart from the render cost: no radio or PDM overrun, no SD or
audio error, `HAS_FAULT=0`, rolling capture with no fault, queues at most 1/8 and the
longest block write 57 ms (image c) and 55 ms (image d). On image d rolling capture kept
a 69.2 s window, the longest loop pass was 64 ms, `DIAG LAST NONE`, and the diagnostic
events were SD writes only. The longest single matrix run was 7,350 µs on image d (3,179 µs on image b); it was not
investigated.

## Not exercised

- The matrix going dark with no clip on the Slicer: host tests cover it.
- Tempo accuracy with the Slicer; p04.14 measured the transport on Granular.
- Disabled slices: the map type has them, but the demo has no control for them.
- The walkthrough on image d, whose change touches only the renders' mix clearing.
- The USB CDC port came back as COM7 after image c's flash, with Windows unable to
  read its serial number; it was back on COM3 after image d's.
