# Demo image: rolling-capture slot allocation without foreground stalls (p04.28)

Date: 2026-10-17 (local, UTC-6; Spooky Bench and log timestamps are UTC, 17:22 to
17:32)  
Beads issue: `full_spooky_proto-p04.28`

Rolling capture broke the foreground budget whenever a slot file needed a new
allocation:

| Run | Path | Stall |
| --- | --- | --- |
| [p04.27](2026-10-16-demo-band-switch-merge.md) | `ROLL SAVE` | 151 ms |
| p04.27 | Rolling restart after a recording | 166 ms |
| [p04.12](2026-10-16-encoder-pwm-demo.md) | Rolling start after a reboot | 198 ms |
| [p04.29](2026-10-17-onset-flashes-demo.md) | `ROLL ON` after a WAV transfer | 493 ms |

`OpenSegment` called FatFs `f_expand(..., 1)` whenever the slot file was new or
truncated. That call searches the FAT for a contiguous free run in one pass, and the
search lengthens as the card fills. On this card the session recorder's own stepped
search for the same size reads about 650 to 790 FAT sectors in 420 to 500 ms. Two
paths left slots without an allocation:

- `DemoRolling_Stop` cut the partial segment to its length.
- A save renamed its pinned slots into `CAPS/`.

The change:

1. **Stop keeps the allocation.** A partial segment that isn't going into a capture
   gets its true length in the WAV header but keeps its full allocation. Only the
   segment closed at a save boundary is still cut, because it becomes the capture's
   file.
2. **Background preparation.** While a segment is open and nothing else runs,
   `DemoRolling_Step` gives an empty slot without an allocation a new one. It reads
   the FAT through the storage service's run search, 8 ms per pass. That search then
   points FatFs at the run, so the following `f_expand` returns at once. Preparation
   stops when a rotation or a save starts.
3. **Ready slots first.** At start, the ROLL folder is read once to find the slot
   files that already have their full length. The catalog then prefers an empty slot
   among those (`RollingCatalog_BeginSegmentPreferring`).
4. **Stepped fallback.** A segment that must still open in an unprepared slot uses the
   same stepped search, 16 ms per pass, while the stream waits.
5. **Counters.** `ROLL` reports `PREPARED` (slots prepared in the background) and
   `OPEN_ALLOCS` (opens that had to allocate on the stream's path).

Demo evidence qualifies the demo image only; it is not milestone evidence (roadmap,
demo track).

The operator confirmed the board was connected and free, with the radio, the SD card
(the same bench card as the earlier runs) and the battery in. Spooky Bench 0.7.0
flashed and reset the board. An ad hoc pyserial script drove the CLI
(`build/p0428-cli.py`, logged in `build/p0428-bench.log`); it is not part of Spooky
Bench.

**Result: pass.** No path broke the foreground budget:

- the start after the flash and after a reset;
- three saves, two of them back to back;
- the rotations into the renamed slots;
- `ROLL OFF` and `ROLL ON` twice, once after a WAV transfer;
- a 30 s recording and the rolling restart after it.

`DIAG LAST` stayed `NONE` throughout. The longest single rolling step was 44 ms and
the longest rotation 37 ms. The background preparation kept up after every save
(`PREPARED` reached 14), and no segment open had to allocate on the stream's path
(`OPEN_ALLOCS=0`).

## Image

The image is `0d844b5` (the demo branch tip after #137) plus the uncommitted p04.28
firmware changes, archived as `build/p0428-demo-20261017a.patch`. The `usb-cli.md`
rows for the new fields were written after the build. It was built with GNU Tools for
STM32 14.3.1 (14.3.rel1.20251027-0700), preset `Demo`, with `SPOOKY_BUILD_ID` set to
the build ID. The flash passed (run `2026-10-17T172255.507860_0000-aa1e01f9`).

| Build ID | CM7 SHA-256 | CM4 SHA-256 | Source patch SHA-256 |
| --- | --- | --- | --- |
| `p0428-demo-20261017a` | `3bfb41e7bf3a4710e0b74afb43718073464367aa2da4e4db3edd4043168c4743` | `7f23d9c8b1f86ccb4aef393f3bd27a6a074bfa07a6cd602a556f153fc44c3f7f` | `41f12dc4543b49a240436c15eed0f429fc40fbd08ada9af3c8191a32db1a6ade` |

Demo, Debug and Release build with the three known `main.c` warnings. The native host
tests pass 49/49 (MSVC), including `test_prefers_ready_empty_slots` in
`rolling_catalog_test`. The FatFs paths in `demo_rolling.c` have no host test; this
bench run is their only check.

## Runs

`ROLL` fields: rotations (`ROTATE_MS_MAX`), the longest single step (`STEP_MS_MAX`),
the recorder queue high-water (`QUEUES`), `PREPARED` and `OPEN_ALLOCS`.

| Step | `ROLL` afterwards | `DIAG` |
| --- | --- | --- |
| Boot after the flash, 25 s | `ROTATE_MS_MAX=29 STEP_MS_MAX=7 QUEUES=1,1/8 PREPARED=0 OPEN_ALLOCS=0` | `LAST NONE`, `HAS_FAULT=0` |
| `ROLL SAVE` (5 segments, 27 s retained) | `SAVE=SAVED` `C032` in 2,333 ms; within 10 s `PREPARED=5`, `STEP_MS_MAX=44` | |
| 70 s of rotations after the save | `ROTATE_MS_MAX=33 STEP_MS_MAX=44 QUEUES=1,1/8 OPEN_ALLOCS=0` | `LAST NONE`, `LOOP_MAX_MS=60` |
| `ROLL OFF`, `ROLL ON` | `STARTS=2`, no allocation, `STEP_MS_MAX=44` | `LAST NONE` |
| `RECORD START 30` | `RECORD PASS`, 30.037 s; queues 1/8, max write 25 ms, margin OK; events written 123, lost 0 | |
| Rolling restart after the recording | `STARTS=3 ROTATE_MS_MAX=37 FAULTS=0` | `LAST NONE`, `HAS_FAULT=0`, no latency violation |
| `ROLL SAVE`, then a second `ROLL SAVE` 5 s later | `SAVES=3`, both committed; `PREPARED` 5 to 11 to 14; `OPEN_ALLOCS=0`; `QUEUES=1,1/8` | `LAST NONE` |
| `ROLL OFF`, WAV transfer (2.7 min), `ROLL ON` | `STARTS=4`, no allocation, `STEP_MS_MAX=44` | `LAST NONE` |
| Spooky Bench `reset` (run `2026-10-17T173118.841414_0000-76ac32ec`), 25 s | `ROTATE_MS_MAX=30 STEP_MS_MAX=7 FAULTS=0` | `LAST NONE`, `HAS_FAULT=0` |

`PREPARED` stayed 0 at both starts because every slot file already had its full
length. The second save was requested while the first save's renamed slots were being
prepared, so it exercised the abort and resumption of a preparation.

`DIAG STATUS` reported `LOOP_MAX_MS=80` after the recording, with no latency violation
recorded. It most likely comes from the session's own prepare steps
(`step-max=70ms`), which this change does not touch.

**WAV inspection** of the recording (run `2026-10-17T172807.588137_0000-59ce24ca`,
saved to `build/wav-F0048-p0428a.json`) passed:

- 3 channels, 48 kHz, 16-bit, 30.037 s; frames and data bytes equal the recorder's PASS
  values.
- Radio left and right RMS 460, peak 5,198, correlation 0.99994, no clipping.
- Microphone RMS 67, peak 858, no clipping.
- CRC-verified transfer, SHA-256
  `dd6edc69374bee4b97e53e404ca3862c32107f30ce8b96879bf086a25739be47`; target healthy
  afterwards.

## Not exercised

- **Fallback path:** a segment open that must allocate on the stream's path
  (`OPEN_ALLOCS` stayed 0) was not reached. This includes rolling starting with no
  ready slot at all. It needs every empty slot unprepared at once, which a save
  followed by background preparation never left.
- **Failed preparation:** the preparation-failure path, where no contiguous run is
  found, was not reached.
- **Fuller card:** each background step is bounded by its 8 ms budget, but the total
  preparation time per slot grows with the FAT search.
- **Lights:** no one watched the lights during this run.

## Artifacts

In `build/`:

- the manifest `p0428-demo-20261017a.json` and the source patch
  `p0428-demo-20261017a.patch`;
- `flash-p0428-demo-20261017a.json`, `reset-p0428-20261017a.json` and
  `wav-F0048-p0428a.json`, each naming its run directory under
  `%LOCALAPPDATA%\SpookyBench\runs\`;
- the CLI log `p0428-bench.log`.
