# Rolling capture with saves (demo image)

Date: 2026-10-04 (local, UTC-6; Spooky Bench and CLI log timestamps are UTC)  
Beads issue: `full_spooky_proto-p04.5` (demo track, decision 0011 item 14)

**This is demo-image evidence.** It covers the opt-in `Demo` preset on
`demo/halloween-2026` only. It does not deliver or qualify `full_spooky_proto-hpq.5`,
whose format, recovery and shared-block rules (decision 0010, `hpq.3`) this demo does not
implement.

The bench was the NUCLEO-H755ZI-Q with the UI board, the radio path, an SD card (SDHC/SDXC,
59,344 MiB) and the 3,700 mAh LiPo switched on. The operator confirmed it was connected
and free, and drove the physical controls. Spooky Bench 0.7.0 ran on Windows, with target
CDC on COM3 and Spookyprobe on COM6. CLI commands went through an ad hoc pyserial script.

After the runs the operator moved the card to the host. The captures were checked there
by `build/check_captures_p045.py`, which uses Spooky Bench's WAV analysis. Results are in
`build/caps-check-p045-20261004b.json` and `build/caps-check-p045-20261004c.json`.

**Result: pass after two fixes found on the bench.**
- Outside a session the recorder kept a rolling window of 62.7 to 70.7 s in thirteen
  reused segment files. Each segment change took at most 37 ms. Queues peaked at 1/8 and
  2/8, with no overrun and no fault.
- Three saves committed captures of 62.7 to 65.4 s. Two were saves from the CLI, and one
  was Shift plus Button 1 (C-009). The audio continues across every segment join, and
  every file agrees with its descriptor.
- A second save while one was committing was answered busy.
- A session started from the prompt turned rolling capture off and recorded normally. A
  save during it was rejected on the display, and rolling capture restarted when it
  ended.
- EMF and the fuel gauge stayed live while rolling, with no foreground budget broken on
  the final images.

The fixes:
1. Build a's first save made its capture folder within one pass. That broke the recorder
   and loop budgets once (72 ms and 84 ms). From build b, captures go into a `CAPS`
   folder made before capture starts.
2. Builds a and b wrote the first descriptor line past the end of a 96-byte buffer,
   truncating the header and adding stray bytes. Build c checks every line and writes
   only what fits.

## Design under test

- **Segments.** The recorder's three-channel blocks go to `ROLL/SLOT00.WAV` to
  `SLOT12.WAV`. Each segment holds 64 blocks (262,144 frames, 5.461 s) and is reused in
  place. A slot file is preallocated with `f_expand` when it is new or was truncated.
  Each segment has a WAV header, rewritten with the true length when a partial segment
  closes.
- **Catalog.** `CM7/App/rolling_catalog.c` keeps at least the newest 704 blocks
  (decision 0010 item 1).
- **Save.** A save resolves at the next block boundary:
  1. It closes the segment.
  2. It pins the newest segments that cover the window.
  3. It continues the stream in a free slot.
  4. It renames the pinned segments to `CAPS/CnnnSmm.WAV`, one per foreground pass.
  5. It writes `CAPS/Cnnn.TXT` last. Only that commit makes the capture saved.
- **Outcomes and the save slot.** Outcomes are writing, saved, busy, unavailable and
  failed, and there is one save slot.
- **Sessions.** A session turns rolling capture off. Rolling restarts with an empty
  window when the session ends.
- **Sensor guards.** In the demo image the magnetometer, fuel-gauge and battery guards
  follow session capture only, so EMF stays live outside sessions. The matrix
  one-run-per-pass limit also applies only during a session (build b).
- **Host test.** `tests/rolling_catalog_test.c` covers the 704-block wrap, the oldest
  segment being reclaimed, pinning the newest window, a save at a boundary and before
  warm-up, a second save while busy, nothing to save, allocation failure while pinned,
  and discarding the window.

## Images

All images were built from `3a8c3956104befe24370afe2d84b06a357941675`
(`demo/halloween-2026` with p04.3) plus the uncommitted p04.4 and p04.5 changes. Each
build's source is archived as `build/<build ID>.patch`, the full diff from that commit,
with its SHA-256 in the manifest. The toolchain was GNU Tools for STM32 14.3.1+st.2,
preset `Demo`. Every flash passed.

| Build ID | Change | CM7 SHA-256 | Source patch SHA-256 |
| --- | --- | --- | --- |
| `p045-demo-20261004a` | Rolling capture; captures in `CAPnnn/` folders | `d2f274e25a08566fc34f48ec11a401004d54edf992fea0a601121eb23701176b` | `8b644569c75032e54705a257a559ee64c12a34c7f2c9a73ce6648ce9e3fb0e95` |
| `p045-demo-20261004b` | `CAPS` folder made at start; matrix limit during sessions only; bare `ROLL` | `f3b1678f0563626779e47ea5cf2026312a34f39d7f20251e7e671af2343422a9` | `e7d3a5c6db04d22242ec207f155fb0dd19da6662cb13ffb11fddc37e3509d2d8` |
| `p045-demo-20261004c` | Descriptor lines checked; left on the board | `728b03c9ff52dae8864313d08dd5c16ec49056c039779f3517590df2ad4eabc2` | `a53b4b11d14c0568a94056d2b70c093b85f47ac0b283f077948a9dace41baa8a` |

The CM4 image was `fe162352187af5241efa8f2eba35cae68c6975946f2ce96f883866ed8d71a465`
for all three. Native host tests passed 34/34 (MSVC 14.29). Host tests are not hardware
evidence.

## Rolling alone

On build a, rolling capture started at boot (`[roll] running: 13 segments of 5461 ms in
ROLL/`). EMF read `VALID` while it ran.
- At 24 s: 4 segment changes, longest 33 ms; longest block write 29 ms; queues 1/8;
  longest loop pass 36 ms; MAG 8 ms.
- At 107 s: the window held 68.5 s (`RETAINED_MS=68522`). There had been 19 segment
  changes (longest 33 ms) and 7 old segments reclaimed. The longest block write was
  32 ms, queues stayed at 1/8, and there were no faults and no allocation failures.

On build a, rolling counts as capture, so the matrix wrote at most one run per pass. It
superseded 243 of 698 frames (35%). On build b, with the session-only limit, it
superseded 13 of 589 (2%).

On build b at 87 s the window held 66.5 s. There had been 16 segment changes (longest
31 ms), the longest storage step was 7 ms, the longest block write 27 ms, and queues
stayed at 1/8.

On build c at 71 s the window held 66.0 s. There had been 13 segment changes (longest
30 ms), the longest block write was 52 ms, and queues stayed at 1/8.

## Saves

| Build | Request | Capture | Frames (s) | Request to commit | Longest step | Budget |
| --- | --- | --- | --- | --- | --- | --- |
| a | `ROLL SAVE`, then `ROLL SAVE` at once | `CAP001/` | 3,137,536 (65.37) | 871 ms | 72 ms | **1 violation**: RECORDER 72 ms (70), LOOP 84 ms (75) |
| b | `ROLL SAVE`, then `ROLL SAVE` at once | `CAPS/C001` | 3,133,440 (65.28) | 783 ms | 18 ms | none: LOOP 60 ms, RECORDER 52 ms |
| b | Shift plus Button 1 (C-009), by the operator | `CAPS/C002` | 3,084,288 (64.26) | 773 ms | 18 ms | none |
| c | `ROLL SAVE` | `CAPS/C003` | 3,010,560 (62.72) | 838 ms | 18 ms | none: LOOP 59 ms, RECORDER 52 ms |

The second `ROLL SAVE`, sent while the first was committing, was counted busy
(`BUSY=1`). On build b it replied `SAVE=BUSY`. On build a its reply was lost, because
the USB send rejects a second reply while the first is in flight.

After a save, the rolling window held about 3.2 s. The save moves its segments out of
the ring, so the next window starts from the save point; this is the demo narrowing.

The operator saw `SAVING CAPTURE...` and then `CAPTURE SAVED` for C002. The console
logged `SHIFT_ENTERED`, the save request and its commit.

Build c numbered its capture C003, because `CAPS/C001S00.WAV` and `C002S00.WAV` already
existed on the card.

## Session interplay (build b)

The operator started a session from the Button 0 prompt. The console logged:
- `[roll] stopped; window discarded`, then the session preparing and
  `OK RECORD START file=REC144.WAV duration=open`.
- During the session the operator tried Shift plus Button 1, and the display showed
  `SAVE: NOT IN SESSION`.
- The operator stopped the session from the prompt: `OK RECORD PASS file=REC144.WAV
  frames=774144 ... audio=16.128s elapsed=16194ms reason=stopped`.
- Straight after, `[roll] running` again. The header counted the buffer up from 0 s, and
  `ROLL` later reported 64.6 s held with `STARTS=2`.

`DIAG LATENCY` after this sequence, recording-only maxima since boot:
- LOOP 60 ms and RECORDER 52 ms, with no violations.
- FUEL 5 ms, MAG 8 ms, MATRIX 5 ms and DEMO 4 ms.

Console capture: `2026-10-04T182810.887198_0000-3c14179b`.

## Captures on the card

Every listed segment exists, and every capture holds more than the 60.07 s window:
- Each segment is a valid recorder WAV: 3 channels, 48 kHz, 16-bit, no clipping.
- Each header, file size and frame count matches its descriptor line.
- The segment sequence numbers are consecutive, and the totals match the frames the
  firmware reported.

| Capture | Segments | Frames | Seconds | Radio RMS per segment | Largest radio step across a join |
| --- | --- | --- | --- | --- | --- |
| `CAP001/` (build a) | 12 | 3,137,536 | 65.365 | — | 479 |
| `CAPS/C001` (b) | 12 | 3,133,440 | 65.280 | 362 to 487 | 517 |
| `CAPS/C002` (b) | 12 | 3,084,288 | 64.256 | 357 to 478 | 596 |
| `CAPS/C003` (c) | 12 | 3,010,560 | 62.720 | 341 to 480 | 277 |

**Joins.** At each join the step in the left radio channel between the last frame of
one segment and the first of the next was compared with the steps inside the 256 frames
either side. In all 44 joins across the four captures, the join step was below the 99th-percentile
step inside its neighbours, and so below the largest. For example, C002's largest join
step, 596, sits against a p99 of 861 there. So there is no gap or splice between
segments.

**Descriptors.** The descriptors of `CAP001`, `C001` and `C002` (builds a and b) have a
corrupt first header line, for example `segments=12 frames=3133` followed by stray
bytes. The cause was the buffer bug above. Their segment lines are intact. `C003.TXT`
(build c) is correct:

```text
SPOOKY DEMO CAPTURE 1
format=48000Hz/16-bit/3ch [radio-L,radio-R,mic]
segments=12 frames=3010560
C003S00.WAV frames=262144 sequence=2
...
```

## Not covered

- Card removal or a write failure while rolling. The fault path (discard the window,
  `BUFFER FAULT`, retry after 5 s) is not exercised on hardware.
- A nearly full card, a long run of hours, or many saves in a row.
- A save requested so early that the window holds only the open segment (answered
  unavailable at the boundary).
- `ROLL OFF` and `ROLL ON`.
- An idle stream with no SD activity, so the effect of rolling on power is not measured.
