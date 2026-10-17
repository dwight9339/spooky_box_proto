# In-band tuning while recording on normal images (54w.6)

Date: 2026-10-16 (local, UTC-6; Spooky Bench and log timestamps are UTC, 22:17 to
22:50)  
Beads issue: `full_spooky_proto-54w.6`; follow-up `full_spooky_proto-itn`

This session checks the lift of the in-band tune guard on `main`
([decision 0003](../decisions/0003-radio-control-during-recording.md)). The shared
command policy now allows `TUNE`, `UP`, `DOWN` and Classic's jumps in every session
state, while band changes stay refused until `full_spooky_proto-54w.12`. The images
are the normal `IpcSmoke` and `Debug` builds; the opt-in `RadioTuneQual` preset is
removed.

Decision 0003's questions for in-band tuning were answered before this session:

- **Q1 to Q3 (stream continuity, timing and scan rate):**
  [tune while recording](2026-10-01-tune-while-recording.md).
- **Q4 (raw-track meaning):** [decision 0015](../decisions/0015-raw-radio-track-during-in-band-tunes.md).
- **Q5 (tune events stored with the session):**
  [session event log](2026-10-16-session-event-log.md).
- **Q6 and Q8 (failure isolation and bus ownership):** accepted on host-test coverage
  by the user on 2026-10-01 (`54w.6` notes).
- **Q7 (acknowledgement):** concerns band changes and stays with `54w.12`.

The operator confirmed the board was connected and free, powered up battery first,
with the SD card in. Spooky Bench 0.7.0 ran the regression, flashed the Debug pair,
captured UART7 for 420 s, and fetched the event logs and WAVs. An ad hoc pyserial
script drove the CLI (`build/w6-bench.py`, logged in `build/w6-bench.log`). The
event logs were compared with the UART lines by `build/hpq11-compare.py`. Neither
script is part of Spooky Bench.

**Result: pass.**

- **Recordings:** with the guard lifted, Classic scanned through every recording,
  and every recording passed with exact accounting, bounded queues, no lost events
  and no faults.
- **CLI:** tunes during a recording were accepted, and a band change was refused
  with the new reply.
- **FM:** passes the retune check from `EVENTS.BIN`.
- **AM:** has one exact-zero run outside a tune. It is near-silent receiver output
  10 ms after a tune ended, not a tune mute. It is described below and filed as
  `full_spooky_proto-itn`.
- **SW:** had no usable signal, as before.

## Images

Both built clean from `2163b3d` (`claude/54w.6-tune-guard-lift`) with GNU Tools for
STM32 14.3.1 (14.3.rel1.20251027-0700).

| Build ID | Preset | CM7 SHA-256 | CM4 SHA-256 |
| --- | --- | --- | --- |
| `w6-ipcsmoke-20261016a` | `IpcSmoke` | `554f6d0bf418f1cb80806555f20ebb6c176646782dcbbcb410884d2547d00320` | `7d9d1116cb20cd8367de3d86a2ec8f067b1cf0fa0ea9220279c6cf9492489584` |
| `w6-debug-20261016a` | `Debug` | `b19dd1aff24d1a6eb6e3d0a86c654eb135bd71bacd7057f99f42b3971af9df08` | `d5ab1687e85e3d5a975b5e5c98bdfa8ca445b7aa8bbabc11c6dd008f94a91809` |

Debug, Release and IpcSmoke build. Native host tests pass 32/32 (MSVC):
`command_policy_test` allows tuning in every session state, and
`classic_service_test` checks that Classic scans through Preparing, Recording and
Finalizing. The Spooky Bench Python tests pass 112/112. Host tests are not hardware
evidence.

## Recording regression

Run `2026-10-16T221713.366901_0000-2149d408`, 60 s on `IpcSmoke`: **pass**, every
stage including manifest and events.

- **Recording:** `FIELD/F0012/AUDIO000.WAV`, 2,883,584 frames. Queues reached at most
  1/8, the longest write was 38 ms, and there was no overrun, SD or audio error, or
  fault.
- **WAV transfer:** SHA-256
  `c53434e728d87cd52727fe6690e82cede34ccc0342dddafd646d52bccae44518`.
- **Events:** Classic scanned through the recording at its default 120/min. The event
  log holds 243 records (120 tune pairs, the frame-0 snapshot, start and end) with
  none lost.

## Sessions on the Debug image

Each was a 60 s `RECORD START 60` while Classic scanned with bounce edges:

| Session | Band and rate | Origin | Result | Queues | `RECORD EVENTS` |
| --- | --- | --- | --- | --- | --- |
| F0013 | FM, 400/min, distance 103 channels | `1:958748` | `PASS`, 2,883,584 frames | at most 1/8, longest write 26 ms | `written=1010 lost=0 queue-max=18/64` |
| F0014 | AM, 180/min (AM's limit) | `1:5262701` | `PASS`, 2,883,584 frames | at most 1/8, longest write 27 ms | `written=366 lost=0 queue-max=14/64` |
| F0015 | SW, 240/min (SW's limit) | `1:9365115` | `PASS`, 2,883,584 frames | at most 1/8, longest write 51 ms | `written=485 lost=0 queue-max=16/64` |

Fifteen seconds into F0013, the CLI sent:

```text
TUNE 99100     -> OK RADIO BAND=FM FREQ=99100 kHz (99.100 MHz) RSSI=4 SNR=1 VALID=0
UP             -> OK RADIO BAND=FM FREQ=88300 kHz ...
DOWN           -> OK RADIO BAND=FM FREQ=107800 kHz ...
BAND AM        -> ERR RADIO band changes disabled while recording
RECORD STATUS  -> OK RECORD ACTIVE ... audio=30.1s queues=0/8,0/8 max-write=25ms margin=OK
```

`UP` and `DOWN` step from wherever Classic has tuned by then, because Classic keeps
scanning around the CLI commands.

After each session, `DIAG STATUS` showed
`RADIO_OVR=0 PDM_OVR=0 SD_ERR=0 AUDIO_ERR=0 HAS_FAULT=0`, and `LOG STATUS` showed no
dropped writes or bytes and no lost transmission. `LOOP_MAX_MS` reached 323 ms from
the CLI band switches between sessions. Classic reported `FAILED=0` throughout, so no
tune failed.

### Event logs against the UART log

`spookybench session events` found no reader-rule problem in any log. Restricted to
each session's frames, the UART `[retune]` and `[classic]` lines and the event log
agree exactly, and sequence numbers are contiguous:

| Session | Tune starts (UART / log) | Tune ends | Classic events |
| --- | --- | --- | --- |
| F0013 | 403 / 403 | 403 / 403 | 201 / 201 |
| F0014 | 181 / 181 | 180 / 180 | 2 / 2 |
| F0015 | 241 / 241 | 240 / 240 | 1 / 1 |

F0013's extra tunes over the regression's rate are the three CLI tunes. In F0014 and
F0015 the last tune was still in flight at session end, so its end is not in the log.

### Retune check from `EVENTS.BIN`

All three WAVs were transferred with passing CRCs and no clipping.
`spookybench wav retune --events`:

| Session | WAV SHA-256 | Tunes | Zero runs | Outside every tune | Overhang |
| --- | --- | ---: | ---: | ---: | --- |
| F0013 (FM) | `2de83bc78aef4628d38fb19e1bf751fe0c6cea771487ac4dd4af8488815847e0` | 403 | 188 | 0 | 0 |
| F0014 (AM) | `d416b1df8cc9a032c11b72ca5a36550473ac410657e3ba7a3266cae29542bd60` | 181 | 284 | 1 | 0 |
| F0015 (SW) | `b183f7fa15aa67c68df561931818783f775e92df0bd3fd455edab93eefb2e877` | 241 | 4,019 | 3,479 | not qualified |

**F0014.** The one run outside every tune is 54 frames (1.1 ms) at frame 1,989,970. It
begins 468 frames after the end stamp of the tune to 1630 kHz (RSSI 23 dBuV, SNR 0,
no station).

| Window around the run | Peak | Exact zeros |
| --- | ---: | --- |
| 100 to 50 ms before | 4 | 2,399 of 2,400 (the tune's mute, inside the stamped tune) |
| 50 to 10 ms before | 18 | 1,660 of 1,920 |
| 10 ms before | 12 | 75 of 480 |
| the run | 0 | 54 of 54 |
| 10 ms after | 6 | 68 of 480 |
| 10 to 100 ms after | 30 | 88 of 4,266 |

Either side of it the samples are -1 to -3. This is near-silent receiver output that
rounds to exact zero for 1.1 ms. Decision 0015 stores it as delivered and treats it as
a measurement. Item 10's criterion, "every exact-zero run of 1 ms or more after the
first tune lies inside a marked retune interval", cannot tell it from a tune mute. The
AM runs that set the margins
([2026-10-09](2026-10-09-retune-qualification.md),
[2026-10-16 event log](2026-10-16-session-event-log.md)) passed. The question is
filed as `full_spooky_proto-itn`.

**F0015.** SW had no usable signal: radio RMS 13, peak 552. Its zero runs are the
receiver's idle output, as in the 2026-10-09 qualification, and SW stays unqualified.

## Not exercised

- **Tune failures:** no tune failed or was abandoned, so a failed tune during a
  recording was not observed on hardware. Failure isolation rests on host tests, as
  accepted on 2026-10-01.
- **Band changes during a recording** are still refused; they belong to `54w.12`.
- **Ten-minute session:** a session of ten minutes at the fastest rates is spec 001
  SC-006, the M3 acceptance in `full_spooky_proto-54w.10`.
- **Button 0:** the CLI started every session.

## Artifacts

Spooky Bench result files in `build/`, each naming its run directory under
`%LOCALAPPDATA%\SpookyBench\runs\`:

- `regression-w6-20261016a.json`
- `flash-w6-debug-20261016a.json`
- `console-w6-debug-20261016a.json`
- `events-F0013-w6.json` to `events-F0015-w6.json`
- `wav-F0013-w6.json` to `wav-F0015-w6.json`
- `retune-F0013-w6.json` to `retune-F0015-w6.json`

The UART log is `build/uart-w6-debug-20261016a.log`.
