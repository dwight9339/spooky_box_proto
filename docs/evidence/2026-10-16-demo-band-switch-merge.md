# Demo image: non-blocking band switch with rolling capture (p04.27)

Date: 2026-10-16 (local, UTC-6; Spooky Bench and log timestamps are UTC, 2026-10-17
04:41 to 04:44)  
Beads issues: `full_spooky_proto-p04.27`; found: `full_spooky_proto-p04.28`

This session checks the demo image after `main` was merged into
`demo/halloween-2026`. The merge brings:

- the non-blocking band switch (`54w.12.2`);
- failure isolation (`54w.12.3`);
- the band switch events (`54w.12.4`);
- the band guard lift (`54w.12`).

On the demo, the blocking switch stalled the foreground about 350 ms per switch and
raised the recorder queues to 4 of 8 ([p04.26](2026-10-16-demo-band-switch-rolling.md)).
With rolling capture always running, a slow SD write during a switch could therefore
still overrun it and show `BUFFER FAULT`.

Demo evidence qualifies the demo image only; it is not milestone evidence (roadmap,
demo track).

The operator confirmed the board was connected and free, with the SD card in. Spooky
Bench 0.7.0 flashed the image and captured UART7. An ad hoc pyserial script drove the
CLI (`build/p0427-bench.py`, logged in `build/p0427-bench.log`); it is not part of
Spooky Bench.

**Result: pass for band switches.**

- **Rolling capture:** 15 band switches with rolling capture running and a 30 s recording
  with three band switches. Rolling capture kept `FAULTS=0` and its queue high-water at
  1 of 8. Before the merge it reached 4 of 8.
- **Recording:** it passed with margin `OK`.
- **Foreground stalls:** the run found two, unrelated to band switches. A rolling save and
  the rolling restart after the recording broke the foreground budget. These predate the
  merge and are filed as `p04.28`.

## Image

GNU Tools for STM32 14.3.1 (14.3.rel1.20251027-0700), `Demo` preset, clean build of
`f3d99cd` (the merge) on `claude/p04-demo-merge-band`:

| Build ID | CM7 SHA-256 | CM4 SHA-256 |
| --- | --- | --- |
| `p0427-demo-20261016a` | `63971f8a05e2323f0838b853835a9f5f92d644e90f4355050d27e77d5a831e6c` | `7f23d9c8b1f86ccb4aef393f3bd27a6a074bfa07a6cd602a556f153fc44c3f7f` |

Demo, Debug, Release and BandSwitchQual build. Native host tests pass 48/48 (MSVC), and
the Spooky Bench Python tests pass. Host tests are not hardware evidence.

The merge needed these demo adjustments:

- **Internal band-request count:** the count behind the band menu no longer treats the
  new `BAND_STARTED` event as the command's answer.
- **Demo view:** a band switch in flight counts as the radio running, so the run line
  does not flash `RADIO NOT RUNNING`.
- **Session prompt:** it checks `RadioAdapter_AudioReady`, as `RECORD START` does.

## Band switches with rolling capture

`ROLL`'s `QUEUES` is the recorder queue high-water since the stream started.

| Step | `ROLL` |
| --- | --- |
| After boot | `STATE=RUNNING QUEUES=1,1/8 STARTS=1 FAULTS=0` |
| After each of 12 switches (AM, SW, LW, FM three times) | `QUEUES=1,1/8 FAULTS=0` every time |
| `BAND LW`, `BAND SW`, `BAND FM` back to back | `OK RADIO SUPERSEDED` (SW), `OK RADIO BAND=LW ...`, `OK RADIO BAND=FM ...`; `QUEUES=1,1/8 FAULTS=0` |
| `ROLL SAVE` | `SAVE=SAVED SAVES=1 LAST=C030`; `QUEUES=2,2/8 FAULTS=0` |

Every band command got exactly one reply. The UART log shows a
`[radio] band switch started <from>-><to>` line for each of the 17 switches, including
the three during the recording, and none failed.

For comparison, the [p04.26 run](2026-10-16-demo-band-switch-rolling.md) on the blocking
switch read `QUEUES=4,4/8` after the first band switch.

## Recording with band switches

`RECORD START 30`, then `BAND AM`, `BAND SW` and `BAND FM` at about 10, 18 and 27 s.
The demo previously refused band changes while recording; with the guard lift they are
allowed.

| | Result |
| --- | --- |
| Replies | one `OK RADIO BAND=...` per switch |
| `RECORD STATUS` after each switch | `queues=0/8,0/8 ... margin=OK` |
| End | `OK RECORD PASS`, 1,441,792 frames (30.037 s); `RECORD DIAG queues radio=1/8 pdm=1/8 max-write=27ms margin=OK`; `RECORD EVENTS written=126 lost=0` |
| Afterwards | rolling capture restarted: `STATE=RUNNING STARTS=2 FAULTS=0`, with `C030` kept |

`DIAG STATUS`: `RADIO_OVR=0 PDM_OVR=0 SD_ERR=0 AUDIO_ERR=0`.

## Foreground stalls not caused by band switches (p04.28)

`HAS_FAULT=1` came from two `FOREGROUND_BUDGET` violations, neither during a band switch:

| When | `DIAG LAST` | What ran |
| --- | --- | --- |
| During `ROLL SAVE` | `FOREGROUND_BUDGET A=0 B=151` (loop 151 ms) | `[roll] save committed C030 in 2981 ms` |
| Right after the recording ended | `FOREGROUND_BUDGET A=0 B=166` (loop 166 ms); `DIAG LATENCY` RECORDER 155 ms, AUDIO 11 ms | `[roll] running: 13 segments of 5461 ms in ROLL/`, the rolling restart |

These predate the merge, which changes no storage code:

- **2026-10-04:** the [rolling capture demo evidence](2026-10-04-rolling-capture-demo.md)
  measured saves committing in about 800 ms with no violation.
- **p04.26:** the run recorded a save violation of 81 ms
  (`FOREGROUND_BUDGET B=81`), hidden there behind the blocking switch's 351 ms.
- **Now:** the card holds 45 `FIELD` session folders and 30 captures, so directory size
  is the likely cause (decision 0014 item 7; on main, `full_spooky_proto-jjy.18`).
- **Effect:** rolling capture kept `FAULTS=0` through both stalls, so `BUFFER FAULT`
  would not show. The stalls still take queue headroom.

## Not exercised

- **Watching and listening:** no one was asked to watch the display or listen to the
  speaker during this run.
- **A failed band switch on the demo image:** the `BAND FAIL` injection exists only in
  the `BandSwitchQual` preset; main's `54w.12.3` bench covers the path.

## Artifacts

In `build/`:

- `flash-p0427-demo-20261016a.json` and `console-p0427-demo-20261016a.json`, each
  naming its run directory under `%LOCALAPPDATA%\SpookyBench\runs\`
- the UART log `build/uart-p0427-20261016a.log`
