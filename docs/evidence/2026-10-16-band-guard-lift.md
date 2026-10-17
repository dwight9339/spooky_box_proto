# Band guard lift: band changes while recording (54w.12)

Date: 2026-10-16 (local, UTC-6; Spooky Bench and log timestamps are UTC, 2026-10-17
03:25 to 03:37)  
Beads issue: `full_spooky_proto-54w.12`

This session is the qualification that lifts the last radio recording guard. The
shared command policy rejected band changes while recording. Principle VI kept that
guard until a bench test showed the guarded workload safe, and
[decision 0003](../decisions/0003-radio-control-during-recording.md) makes band
changes legal in every session state once they are qualified.

`54w.12`'s acceptance asks for bench evidence on:

- repeated transitions;
- the audio discontinuity policy;
- exact recorder accounting;
- queue and error budgets;
- a shared command policy that permits only qualified cases and records timestamped
  transitions.

The user's answers to Q1–Q8 (2026-10-16) set the policy. Four subtasks implemented and
benched it, each on the opt-in `BandSwitchQual` image:

| Subtask | Change | Evidence |
| --- | --- | --- |
| `54w.12.1` | Silence fill and exact gap events (decision 0004) | [band switch silence fill](2026-10-16-band-switch-silence-fill.md) |
| `54w.12.2` | Non-blocking band switch | [non-blocking band switch](2026-10-16-nonblocking-band-switch.md) |
| `54w.12.3` | A failed switch keeps the session recording with an open gap and a visible fault | [failed band switch during a session](2026-10-16-failed-band-switch-session.md) |
| `54w.12.4` | Started, completed and failed events | [band switch events](2026-10-16-band-switch-events.md) |

This run uses a normal Release image with the guard lifted. It is a 10-minute session
with 71 band switches through all four bands while Classic scans.

The operator confirmed the board was connected and free, with the SD card in. Spooky
Bench 0.7.0 flashed the image, captured UART7 and fetched the event log over USB. After
the run the operator moved the card to the PC, and the session folder was copied from
it. Ad hoc scripts did the rest. None is part of Spooky Bench:

- `build/w12e-soak.py` drove the CLI, logged in `build/w12e-soak.log`;
- `build/hpq11-compare.py` compared the event log with the UART log.

**Result: pass. The band guard is lifted.**

- **Recording:** a 600 s recording passed with exact accounting.
- **Switches:** it held 71 band switches, every one announced, completed and stored as an
  exact gap of digital silence.
- **Budgets:** queues, storage margin and foreground latency stayed within budget, with no
  fault.

## Image

GNU Tools for STM32 14.3.1 (14.3.rel1.20251027-0700), `Release` preset, clean build of
`30cf09c` on `claude/54w.12-band-guard-lift`. Band changes are allowed in every session
state:

| Build ID | CM7 SHA-256 | CM4 SHA-256 |
| --- | --- | --- |
| `w12e-release-20261016a` | `777da5c73241d924ffe5279ddda48ef9561e50bff617484539952bb1232e6ef2` | `ec799c695d55c9a3525a9f85410f9dbc6ea73ec0924682a3a972de0bdab8c1fc` |

Debug, Release and BandSwitchQual build. Native host tests pass 33/33 (MSVC). The
policy test now expects band changes allowed in every session state, and its
band-qualification variant is removed. The Spooky Bench Python tests pass 125/125. Host
tests are not hardware evidence.

## Workload

- **Session F0044:** `RECORD START 600`, origin `1:749940`, with Classic running at its
  default 120/min.
- **Band commands:** roughly every 8.5 s the driver sent one, cycling AM, SW, LW, FM.
- **Bursts:** every twelfth command was replaced by a burst of `BAND LW`, `BAND SW` and
  `BAND FM` written back to back. In each burst, SW waits behind LW and is superseded
  by FM.
- **Status:** `RECORD STATUS` was read after every command.

| Band commands | Replies | Band switches performed |
| --- | --- | --- |
| 61 single, plus 5 bursts of 3 (76) | 76, one per command: 71 `OK RADIO BAND=...` and 5 `OK RADIO SUPERSEDED` | 71 |

Classic made 1,169 in-band tunes in the same session, all `tuned`. It reported
`REFUSED=0 FAILED=0` and kept scanning in each new band.

## Results

- **Recording:** `OK RECORD PASS`, 28,803,072 frames (600.064 s), 172,818,432 bytes,
  finalized.
- **Queues:** radio and PDM peaked at 1 of 8. The 66 `RECORD STATUS` replies read
  `queues=0/8,0/8` 63 times and `0/8,1/8` 3 times, with margin `OK` every time.
- **SD:** `RECORD LATENCY` counted 7,032 writes, the longest 41 ms. Histogram:
  6,953 under 10 ms, 76 in 10–19 ms, 1 in 20–29 ms, 2 in 40–49 ms.
- **Foreground:** `DIAG LATENCY` reported the loop at most 47 ms against its 75 ms budget
  during recording. The dispatcher and every other service stayed within budget, with no
  violations. `DIAG STATUS` all-time `LOOP_MAX_MS=50`.
- **Faults:** `RADIO_OVR=0 PDM_OVR=0 SD_ERR=0 AUDIO_ERR=0 HAS_FAULT=0`, and
  `DIAG LAST NONE`. `LOG STATUS` reported no dropped writes or bytes and no transport
  errors.
- **Event log:** `RECORD EVENTS written=2549 lost=0 queue-max=13/64 write-max=39ms`. The
  manifest reports `events_written=2549 events_lost=0`.

### Transitions recorded with timestamps

The UART log holds, for every one of the 71 switches:

- a `[radio] band switch started <from>-><to>` line;
- a `[gap] start=... end=...` line, with `missed=0` every time;
- a `[radio] switched to ...` line.

It holds no `FAIL` line.

The event log, fetched over USB and identical to the card's copy (SHA-256
`b1cedfa5af76526290d996f531b4688425d51e9d458edf18511a10a7df7e738a`), passes the reader
rules. It matches the UART log record for record:

| Tune starts | Tune ends | Classic | Gap starts | Gap ends | Sequence |
| --- | --- | --- | --- | --- | --- |
| 1,169 / 1,169 | 1,169 / 1,169 | 66 / 66 | 71 / 71 | 71 / 71 | contiguous |

### Gaps in the audio

The WAV copied from the card has SHA-256
`3ee71e085ef9bd1aea7ba0929c3592f7164ea1db11a8c7812b293e8e1eb1d31b`.
`spookybench wav retune --events` checked every frame of every gap: all 71 gaps are
closed, and every radio frame inside them is zero. Gap lengths by switch:

| Switch | Count | Frames | Milliseconds |
| --- | ---: | --- | --- |
| FM→AM | 17 | 14,336 to 19,456 | 299 to 405 |
| AM→SW | 17 | 13,312 to 17,920 | 277 to 373 |
| SW→LW | 16 | 16,384 to 17,408 | 341 to 363 |
| LW→FM | 16 | 9,728 to 11,264 | 203 to 235 |
| LW→LW (first command of each burst) | 5 | 16,896 to 17,408 | 352 to 363 |

A switch to the band already in use still power-cycles the receiver and is recorded
as a gap.

The tool's overall result is `fail` (`zero_run_outside_retune`, 3,691 runs). The band
switches are not the cause:

- **FM and LW:** no unexplained zero run.
- **SW:** 3,690 runs, in stretches with no usable signal, as in every earlier bench since
  the [retune qualification](2026-10-09-retune-qualification.md).
- **AM:** one run, a single zero frame on the first frame after an FM→AM gap: the
  receiver's first output sample.
- **Tracking:** short runs right after gaps into SW are the same near-silent receiver
  output. This pattern is tracked in `full_spooky_proto-itn`, now noted there.
- **SW overhang:** the measured SW tune overhang of 1,328 frames exceeds SW's provisional
  512-frame settle margin. SW and LW margins stay unqualified until reception is
  usable, as decision 0015's qualification recorded.

## Not exercised

- **A failed band switch on this image:** covered on `BandSwitchQual` by `54w.12.3`.
  Normal builds have no fault injection.
- **Listening:** no one listened to the monitor during these switches.
- **Physical band selection:** no band was chosen with the physical band menu. The CLI
  drove every switch. The physical controls reach the same Radio machine through the
  same command policy.
- **A longer soak:** this run lasted 600 s, not hours.

## Artifacts

In `build/`:

- `flash-w12e-release-20261016a.json`, `console-w12e-release-20261016a.json` and
  `events-F0044-w12e.json`, each naming its run directory under
  `%LOCALAPPDATA%\SpookyBench\runs\`
- `retune-F0044-w12e.json`
- the UART log `build/uart-w12e-20261016a.log`

The session folder copied from the card is in the session's scratch directory, not in
the repository.
