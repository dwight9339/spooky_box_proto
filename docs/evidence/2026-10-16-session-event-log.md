# Session event log on hardware (decision 0030 item 20)

Date: 2026-10-16 (local, UTC-6; Spooky Bench and log timestamps are UTC, 21:30 to
22:00)  
Beads issue: `full_spooky_proto-hpq.11`; answers Q5 for `full_spooky_proto-54w.6`

This session qualifies the session event log (`EVENTS.BIN`) of
[decision 0030](../decisions/0030-field-session-folder-and-event-log.md) as its item 20
requires:

- the recording regression;
- the [decision 0015 item 10](../decisions/0015-raw-radio-track-during-in-band-tunes.md)
  FM and AM run, repeated with the tune intervals read from `EVENTS.BIN` instead of
  the UART log;
- no records lost at Classic's fastest rate with bounce edges;
- every event matching the UART `[retune]` and `[classic]` lines.

The operator confirmed the board was connected and free, powered up battery first,
with the SD card in. Spooky Bench 0.7.0 ran the regression, flashed the
qualification image, captured UART7 for 600 s, and fetched the event logs and WAVs.
An ad hoc pyserial script drove the CLI (`build/hpq11-qual.py`, logged in
`build/hpq11-qual.log`). A second ad hoc script compared the event logs with the
UART lines (`build/hpq11-compare.py`). Neither is part of Spooky Bench.

**Result: pass.**

- **Recording regression:** passes, including its new event-log stage.
- **Qualification recordings:**
  - FM at 400 jumps a minute with bounce edges wrote 1,007 records and AM at 180
    wrote 366, with none lost.
  - Every tune start, tune end and Classic event in the UART log appears in
    `EVENTS.BIN` at the same session frame, with none missing or extra.
- **Retune check:** from `EVENTS.BIN`, every zero run lies in a stamped tune with no
  overhang, on both bands. This needed one reader-rule fix, described below.

## Images

Both built clean from `d2544a9` (`claude/hpq.11-event-log`) with GNU Tools for STM32
14.3.1 (14.3.rel1.20251027-0700).

| Build ID | Preset | CM7 SHA-256 | CM4 SHA-256 |
| --- | --- | --- | --- |
| `hpq11-ipcsmoke-20261016a` | `IpcSmoke` | `747f6d198f36b2f4f7dc4049b94234f2e868da54402d1fff9073fd8a2b98b183` | `7d9d1116cb20cd8367de3d86a2ec8f067b1cf0fa0ea9220279c6cf9492489584` |
| `hpq11-tunequal-20261016a` | `RadioTuneQual` | `3c8d4a3103ae26381ff0105dc4127d7a5e98393d3ca34eeb894428001152d92b` | `5533e35d55d0d5d83f0b7bc0ce32d88bb322dc62754f8d281462eb62dfc5fb8f` |

`RadioTuneQual` allows in-band tuning while recording (the decision 0003 guard,
opt-in), so Classic kept scanning through each recording. Debug, Release, IpcSmoke
and RadioTuneQual build. Native host tests pass 33/33 (MSVC), including the new
`session_event_log_test`, whose encoding check matches the session-format hex
example byte for byte. The Spooky Bench Python tests pass 112/112. Host tests are not
hardware evidence.

## Recording regression

Run `2026-10-16T213058.268202_0000-6780dda7`, 60 s: **pass**. Stages: boot smoke,
prerequisites, recording, WAV inspection, accounting, manifest, events and
post-transfer health.

- **Recording:** `FIELD/F0009/AUDIO000.WAV`, 2,883,584 frames. Queues reached at most
  1/8, the longest write was 24 ms, and there was no overrun, SD or audio error, or
  fault.
- **WAV transfer:** SHA-256
  `27ae3508498163de3a5eecaed5cd98b03ab6a12d6110fa4339bebd8551f9a269`.
- **Event log:** three records: session start, the frame-0 Classic snapshot, and
  session end at frame 2,883,583. The manifest has `events_written=3` and
  `events_lost=0`, and the recorder reported
  `RECORD EVENTS written=3 lost=0 queue-max=2/64 write-max=3ms failed=0`. Classic
  does not tune during recordings in this image, because the tune guard is in place.
- **Preparation:** `events=15ms`, the time to create `EVENTS.BIN`.

## Qualification recordings

On the `RadioTuneQual` image, each a 60 s `RECORD START 60` while Classic scanned:

| Session | Band and settings | Origin | Result | Queues | `RECORD EVENTS` |
| --- | --- | --- | --- | --- | --- |
| F0010 | FM, 400/min, bounce, distance 103 channels | `1:933309` | `PASS`, 2,883,584 frames | at most 1/8, longest write 51 ms | `written=1007 lost=0 queue-max=18/64 write-max=5ms failed=0` |
| F0011 | AM, 180/min (AM's limit), bounce | `1:4980608` | `PASS`, 2,883,584 frames | at most 1/8, longest write 26 ms | `written=366 lost=0 queue-max=15/64 write-max=8ms failed=0` |

`DIAG STATUS` afterwards showed `RADIO_OVR=0 PDM_OVR=0 SD_ERR=0 AUDIO_ERR=0
HAS_FAULT=0`. `LOG STATUS` showed no dropped writes or bytes and no lost
transmission. `LOOP_MAX_MS` reached 323 ms from the CLI band switches between
recordings.

### Event logs

`spookybench session events` decoded both logs and found no reader-rule problem:
session start at frame 0, session end at frame 2,883,583, no record after it, no
sequence gap and no events-lost record.

| Session | Records | Session start / end | Classic | Tune start | Tune end |
| --- | ---: | --- | ---: | ---: | ---: |
| F0010 | 1,007 | 1 / 1 | 203 | 401 | 401 |
| F0011 | 366 | 1 / 1 | 3 | 181 | 180 |

- **Frame 0:** each log holds a Classic snapshot at frame 0. F0011 also holds a tune
  start at frame 0 flagged "began before origin" (flag 2): a tune was in flight when
  capture started (decision 0030 item 11).
- **F0011's extra start:** its 181 tune starts against 180 ends come from that
  frame-0 start and a last tune still in flight at session end. The last tune's end
  was observed after the last frame, so it is not in the log (item 10).

### Against the UART log

The UART log (`build/uart-hpq11-tunequal-20261016a.log`) holds 2,103 `[retune]` and
282 `[classic]` lines. Restricted to each recording's frames, after subtracting its
origin:

| Session | Tune starts (UART / log) | Tune ends | Classic events |
| --- | --- | --- | --- |
| F0010 | 401 / 401, identical | 401 / 401, identical | 202 / 202, identical |
| F0011 | 180 / 180, identical | 180 / 180, identical | 2 / 2, identical |

The frame-0 snapshot and the tune flagged as beginning before the origin have no
UART line inside the recording, and are not counted. Sequence numbers are contiguous
in both logs.

### Retune check from `EVENTS.BIN`

Both WAVs were transferred with `spookybench wav inspect`, with passing CRCs:

| Session | SHA-256 | Radio RMS and peak | Microphone RMS and peak |
| --- | --- | --- | --- |
| F0010 | `a964eebc6e7d452f25b3b441283daae9ae0f6e86c82d1535edad1736958a41e6` | 457, 6,672 | 11, 112 |
| F0011 | `59aba1b9b3b13b55734d6ee6a88630644ef5f948b21aa0c4831775b7df57742d` | 249, 5,569 | 9, 172 |

Neither file has clipping. `spookybench wav retune --wav <WAV> --events <EVENTS.BIN>`:

| Session | Tunes | Zero runs checked | Outside every tune | Largest overhang | Margin by item 10 |
| --- | ---: | ---: | ---: | ---: | ---: |
| F0010 (FM) | 401 | 189 | 0 | 0 | 0 |
| F0011 (AM) | 181 | 284 | 0 | 0 | 0 |

The first F0011 analysis found one unexplained zero run: frames 2,880,093 to the end
of the file. It began after the last tune start, at 2,877,923, a tune whose end came
after the session ended. The reader paired only tunes that had an end. The fix makes
a tune start with no end before session end run to session end. It is in
`host/spookybench/session_events.py`, with a test, and the session-format reader
rules now state it. The rerun above is with the fix. This was a reader gap, not a
firmware fault.

The FM and AM margins of 0 agree with the
[2026-10-09 qualification from the UART log](2026-10-09-retune-qualification.md).

## Not exercised

- **Loss under load:** the queue never exceeded 18 of 64. Loss, the events-lost
  record and resynchronization are covered by host tests only.
- **Write failure:** an event-log write failure was not induced.
- **PTT events:** no PTT changes were made during these recordings.
- **Gap records:** these belong to `full_spooky_proto-54w.12`.
- **Sparse flushing:** sparse 2 s flushes happened in the regression's quiet
  recording, which logged only three records, but were not timed separately.

## Artifacts

Spooky Bench result files in `build/`, each naming its run directory under
`%LOCALAPPDATA%\SpookyBench\runs\`:

- `regression-hpq11-20261016a.json`
- `flash-hpq11-tunequal-20261016a.json`
- `console-hpq11-tunequal-20261016a.json`
- `events-F0010-hpq11.json` and `events-F0011-hpq11.json`, with decoded JSON lines
  in each run's `session/` folder
- `wav-F0010-hpq11.json` and `wav-F0011-hpq11.json`
- `retune-F0010-hpq11.json`, `retune-F0011-hpq11.json` (before the reader fix) and
  `retune-F0011-hpq11b.json` (after)
