# Non-blocking band switch (54w.12.2)

Date: 2026-10-16 (local, UTC-6; Spooky Bench and log timestamps are UTC, 2026-10-17
01:36 to 01:57)  
Beads issue: `full_spooky_proto-54w.12.2`

This session checks that a band switch no longer holds the foreground loop. Until now
`RadioControl_SwitchBand` power-cycled the Si4735 and waited for its first tune in one
call. The [silence fill bench](2026-10-16-band-switch-silence-fill.md) measured it at
up to 349 ms of foreground per switch, with the recorder queues at 4 or 5 of 8. That
latched the storage margin warning and the foreground budget fault on every switch.
Now the Radio machine waits in a Switching state while the radio service advances the
same command sequence by one bounded bus step per foreground pass.

It uses the opt-in `BandSwitchQual` preset, which allows band changes while recording
for qualification only. Normal builds still refuse them until `full_spooky_proto-54w.12`.

The operator confirmed the board was connected and free, with the SD card in. Spooky
Bench 0.7.0 flashed the images, captured UART7, and fetched the event logs and a WAV.
Ad hoc pyserial scripts drove the CLI (`build/w12b-bench.py`, logged in
`build/w12b-bench.log`; `build/w12b-bench-b.py`, logged in `build/w12b-bench-b.log`).
The 54w.12.1 script `build/hpq11-compare.py` compared each event log with the UART
lines. None of these scripts is part of Spooky Bench.

**Result: pass.**

- **Foreground:** during two sessions with five band switches each, the loop stayed
  within its 75 ms budget and the dispatcher within its 10 ms. No budget violation
  was recorded and `HAS_FAULT` stayed 0.
- **Queues:** both recorder queues peaked at 1 of 8, and the storage margin stayed `OK`.
  Before this change they reached 4 or 5 of 8, and the margin went `LOW`.
- **Commands during a switch:** a command that arrives during a switch waits, latest
  wins. A waiting tune is checked against the new band.
- **Event logs:** the gaps and tunes stored in each event log match the UART log
  exactly, and every gap checked is exact silence.
- **Costs:**
  - The switch now takes longer, so each gap is about 50 to 150 ms longer than with the
    blocking switch.
  - The first image showed a CLI defect, now fixed in image b.

## Images

GNU Tools for STM32 14.3.1 (14.3.rel1.20251027-0700), `BandSwitchQual` preset, clean
builds on `claude/54w.12.2-nonblocking-band`:

| Build ID | Source | CM7 SHA-256 | CM4 SHA-256 |
| --- | --- | --- | --- |
| `w12b-bandqual-20261016a` | `13a0590` | `622b323b6a66bd9c9a209b86e8ed94b49503d163db6ddff589a5c991180c34b4` | `23d8a1ac972ecc3f9bb2d56de274d1634861145a2b35ebc198177b758beae655` |
| `w12b-bandqual-20261016b` | `e1f3475`: the CLI fix, and a command's reply read in the poll that wrote it | `29c75ab6bac8a091efb5a994c3d29ae9fe4dbd3e9b410645d802b5adf6659d90` | `23d8a1ac972ecc3f9bb2d56de274d1634861145a2b35ebc198177b758beae655` |

Debug, Release and BandSwitchQual build. Native host tests pass 34/34 (MSVC). They
include:

- the new switch scenarios of `radio_control_test`: the proven command order and
  delays, no poll that waits, timeouts and error status;
- the Switching scenarios of `radio_test`;
- Classic waiting out a switch, in `classic_service_test`.

The Spooky Bench Python tests pass 125/125. Host tests are not hardware evidence.

## Commands during a switch

Outside a session, with Classic paused, the driver wrote two or three commands back to
back. Replies on image b:

| Commands | Replies |
| --- | --- |
| `BAND AM`, `TUNE 1500` | `OK RADIO BAND=AM FREQ=1000 kHz`, then `OK RADIO BAND=AM FREQ=1500 kHz` |
| `BAND FM`, `TUNE 1500` | `OK RADIO BAND=FM ...`, then `ERR FM range: 87500..108000 kHz` |
| `BAND SW`, `TUNE 9500` | `OK RADIO BAND=SW FREQ=6000 kHz`, then `OK RADIO BAND=SW FREQ=9500 kHz` |
| `BAND FM`, `TUNE 120000` | `OK RADIO BAND=FM ...`, then `ERR FM range: 87500..108000 kHz` |
| `BAND LW`, `BAND SW`, `BAND FM` | `OK RADIO SUPERSEDED` (SW), `OK RADIO BAND=LW ...`, `OK RADIO BAND=FM ...` |

**Defect found on image a.** `BAND AM` followed by `TUNE 1500` was answered at once
with `ERR FM range`. The CLI checked a tune's range against the current band before
posting it, so a tune sent right behind a band command was checked against the old
band. The Radio machine already checks the range against the band the tune will run
in. Image b removes the CLI check, and the first row shows the result.

## Sessions

Classic scanned at its default 120/min. Each session switched band about every 10 s.

| Image | Session | Origin | Band switches | Result | Queues (radio, PDM) | Margin | `RECORD EVENTS` |
| --- | --- | --- | --- | --- | --- | --- | --- |
| a | F0021 | `1:1611135` | FM→AM, AM→SW, SW→LW, LW→FM, FM→AM | `PASS`, 2,883,584 frames | 1/8, 1/8 | `OK` | `written=252 lost=0` |
| a | F0022 | `1:5279107` | AM→SW, SW→LW, LW→FM, FM→AM, AM→FM | `PASS`, 2,883,584 frames | 1/8, 1/8 | `OK` | `written=253 lost=0` |
| b | F0023 | `1:1799593` | FM→AM, AM→SW, SW→LW, LW→FM, FM→AM | `PASS`, 2,883,584 frames | 1/8, 1/8 | `OK` | `written=253 lost=0` |
| b | F0024 | `1:5468903` | AM→SW, SW→LW, LW→FM, FM→AM, AM→FM | `PASS`, 2,883,584 frames | 1/8, 1/8 | `OK` | `written=256 lost=0` |

`DIAG LATENCY` after each image's sessions:

| | LOOP max (budget 75 ms) | DISPATCH max (budget 10 ms) | Violations | `HAS_FAULT` |
| --- | --- | --- | --- | --- |
| Blocking switch (54w.12.1, F0016 to F0019) | 342 to 349 ms | 322 ms | one per switch | 1 (`FOREGROUND_BUDGET`) |
| Image a | 40 ms | 5 ms | 0 | 0 |
| Image b | 58 ms | 6 ms | 0 | 0 |

- **Image b's loop maximum:** it came with a 52 ms SD write in F0023; the `RECORDER`
  service maximum was 52 ms.
- **Maxima across sessions:** `DIAG LATENCY` reported the same maxima after F0024 as
  after F0023, so its recording-only maxima carry over between recordings. The table
  gives the value after the second session.
- **All-time loop maximum:** `DIAG STATUS` reported 80 ms (image a) and 74 ms
  (image b). That counts outside recording too. Recording preparation measured
  steps of up to 74 ms and 68 ms, but nothing logged attributes the maximum to a pass.
- **Health:** `RADIO_OVR=0 PDM_OVR=0 SD_ERR=0 AUDIO_ERR=0` throughout.

### Gap lengths

Each band switch is a decision 0004 gap, and its length is the switch's duration. Every
`[gap]` line on both images reports `missed=0`. Lengths in frames (ms):

| Switch | Blocking (54w.12.1) | Image a | Image b |
| --- | --- | --- | --- |
| FM→AM | 11,264 to 15,360 (235 to 320) | 19,968 to 25,088 (416 to 523) | 16,384 to 19,968 (341 to 416) |
| AM→FM | 6,656 (139) | 12,288 to 17,408 (256 to 363) | 9,216 to 13,824 (192 to 288) |
| AM→SW | 11,264 (235) | 19,968 to 20,992 (416 to 437) | 15,360 to 17,920 (320 to 373) |
| SW→LW | 14,336 to 14,848 (299 to 309) | 24,064 to 25,088 (501 to 523) | 20,480 (427) |
| LW→FM | 6,656 to 7,680 (139 to 160) | 12,288 to 18,432 (256 to 384) | 9,216 to 12,800 (192 to 267) |

The polled switch takes longer than the blocking one. Each of its steps waits for a
foreground pass, and during recording a pass takes several milliseconds. On image a,
every command and its reply took separate passes, which added 100 to 220 ms per
switch. Image b reads a command's reply in the poll that wrote it, and the poll that
ends a delay writes the next command. That recovered 60 to 100 ms.

The remaining 50 to 150 ms comes from the seven fixed delays and the receiver's waits
rounding up to pass boundaries. These are the proven delays, so shortening them would
need its own qualification.

### Event logs and audio (image b)

Both event logs pass the reader rules (no problems reported). Every record matches the
UART log, and sequence numbers are contiguous:

| Session | Tune starts | Tune ends | Classic | Gap starts | Gap ends |
| --- | --- | --- | --- | --- | --- |
| F0023 | 118 / 118 | 117 / 117 | 5 / 5 | 5 / 5 | 5 / 5 |
| F0024 | 119 / 119 | 119 / 119 | 5 / 5 | 5 / 5 | 5 / 5 |

F0023's last tune was still in flight at session end, so it has no tune end.

The F0023 WAV was transferred with passing CRCs (SHA-256
`bfb67566a85521642dc87640ba7731b055e51e9cc867f9fa401fdc1b891c8eb1`).
`spookybench wav retune --events` checked every frame of its five gaps:

| Gap (session frames) | Switch | Frames | Nonzero frames inside |
| --- | --- | ---: | ---: |
| 616,023–634,966 | FM→AM | 18,944 | 0 |
| 1,108,055–1,125,974 | AM→SW | 17,920 | 0 |
| 1,597,015–1,617,494 | SW→LW | 20,480 | 0 |
| 2,088,023–2,100,310 | LW→FM | 12,288 | 0 |
| 2,578,519–2,594,902 | FM→AM | 16,384 | 0 |

The tool's overall result is `fail` (`zero_run_outside_retune`). This is not caused by
the band switches:

- All 42 unexplained zero runs are in SW stretches, which have no usable signal. That
  is the same pattern as the
  [retune qualification](2026-10-09-retune-qualification.md) and the 54w.12.1 bench.
- FM, AM and LW have no unexplained zero run and no tune overhang.

## Not exercised

- **A failed band switch on hardware:** host tests cover the failure paths. Keeping a
  session recording through a failed switch belongs to `54w.12.3`.
- **Listening:** no one listened to the monitor during these switches.
- **Image a's WAVs and F0024's WAV:** not transferred. Image b's silence fill code is
  the same as image a's.

## Artifacts

Spooky Bench result files in `build/`, each naming its run directory under
`%LOCALAPPDATA%\SpookyBench\runs\`:

- `flash-w12b-bandqual-20261016a.json` and `-b.json`
- `console-w12b-bandqual-20261016a.json` and `-b.json`
- `events-F0023-w12b.json` and `events-F0024-w12b.json`
- `wav-F0023-w12b.json`
- `retune-F0023-w12b.json`

The UART logs are `build/uart-w12b-20261016a.log` and `build/uart-w12b-20261016b.log`.
