# Band switches during rolling capture on the demo image (p04.26)

Date: 2026-10-16 (local, UTC-6; Spooky Bench and CLI timestamps are UTC 2026-10-17
01:05 to 01:08, local 19:05 to 19:08)  
Beads issue: `full_spooky_proto-p04.26` (demo track; the product fix is
`full_spooky_proto-54w.12.1`)

**This is demo-image evidence.** It covers the opt-in `Demo` preset on
`demo/halloween-2026` only.

The [2026-10-09 speaker monitoring evidence](2026-10-09-demo-speaker-monitor.md) found
the fault. Band switches during rolling capture dropped radio half-buffers, which left
the microphone queue ahead until it overran after about three switches. The product
fix, decision 0004's silence fill, is on `main`
([band-switch silence fill](2026-10-16-band-switch-silence-fill.md)). This session
checks the demo image after `main` is merged into the demo branch.

The operator confirmed the board was connected and free, powered up battery first,
with the SD card in. Spooky Bench 0.7.0 flashed the image. An ad hoc pyserial script
drove the CLI (`build/p0426-bench.py`, not part of Spooky Bench), logged in
`build/p0426-bench.log`.

**Result: pass.**

- **Rolling capture:** it kept running through four band switches with no fault and
  no restart.
- **Queues:** the radio and microphone queues stayed level.
- **Save:** a window saved right after the switches.
- **Session:** a session recorded into a session folder with its event log, refused a
  band change, and handed back to rolling capture.

## Image

| Build ID | Source | CM7 SHA-256 | CM4 SHA-256 |
| --- | --- | --- | --- |
| `p0426-demo-20261016a` | `ea9191a` (`claude/p04.26-demo-merge`, clean) | `aa7962737b6fac9bc9fb3390716615a6121107bad85d3b77637d54a0303df708` | `7f23d9c8b1f86ccb4aef393f3bd27a6a074bfa07a6cd602a556f153fc44c3f7f` |

GNU Tools for STM32 14.3.1 (14.3.rel1.20251027-0700). `ea9191a` merges `main`
(through PR #127) into `demo/halloween-2026` at `5bb274d`:

- silence fill and gap events (`54w.12.1`);
- session folders and the event log (`hpq.10`, `hpq.11`, `hpq.13`);
- the in-band tune guard lift (`54w.6`).

The merge commit lists its conflict resolutions and two fixes:

- a duplicate `ClassicAdapter_GetState` was removed;
- the session event log starts only for a session, not when rolling capture shares
  the stream.

Demo, Debug, Release and BandSwitchQual build. Native host tests pass 49/49 (MSVC),
and the Spooky Bench Python tests pass 126/126. Host tests are not hardware evidence.

## Band switches during rolling capture

`ROLL`'s `QUEUES` field is the recorder queue high-water since the stream started, as
radio then microphone.

| Step | `ROLL` |
| --- | --- |
| After boot | `STATE=RUNNING QUEUES=1,1/8 STARTS=1 FAULTS=0` |
| `BAND AM` | `QUEUES=4,4/8 STARTS=1 FAULTS=0` |
| `BAND SW` | `QUEUES=4,4/8 STARTS=1 FAULTS=0` |
| `BAND LW` | `QUEUES=4,4/8 STARTS=1 FAULTS=0` |
| `BAND FM` | `QUEUES=4,4/8 STARTS=1 FAULTS=0` |
| `ROLL SAVE` | `SAVE=WRITING`, then `SAVE=SAVED SAVES=1 LAST=C029` |

- **Level queues:** the two high-water marks rise together and stay level, because the
  radio is no longer short of blocks after a switch.
- **Before the fix:** the [p04.26 report](2026-10-09-demo-speaker-monitor.md) shows the
  same check reading `1,5/8` and then `1,6/8`, and a `PDM queue overrun` on the third
  switch.
- **Why 4 of 8:** the blocking band switch holds the foreground for up to 351 ms
  (`LOOP_MAX_MS=351`). That is the cost `full_spooky_proto-54w.12.2` removes.

`DIAG STATUS`: `RADIO_OVR=0 PDM_OVR=0 SD_ERR=0 AUDIO_ERR=0`. `HAS_FAULT=1` came from
`FOREGROUND_BUDGET` (`DIAG LAST`), the blocking switch, not from audio.

## A session on the demo image

| Step | Result |
| --- | --- |
| `RECORD START 30` | `FIELD/F0020/AUDIO000.WAV`, preparation 245 ms |
| `BAND AM` 13 s in | `ERR RADIO band changes disabled while recording` |
| End | `RECORD PASS`, 1,441,792 frames (30.037 s); queues 1/8; margin `OK`; `RECORD EVENTS written=123 lost=0 queue-max=10/64 write-max=5ms failed=0` |
| 12 s later | rolling capture `STATE=RUNNING STARTS=2 FAULTS=0`, the saved `C029` kept |

## Not exercised

- **Alignment in the saved window:** the saved window's radio and microphone alignment
  was not measured with a stimulus. Pairing by block count is restored by
  construction, and the level queues above show no lead.
- **Physical controls:** the band menu and Button 0 were not used; the CLI drove the
  band switches.
- **The saved capture's files:** not fetched.
