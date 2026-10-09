# Monitor-only PTT

Date: 2026-10-08 (local, UTC-6; console timestamps are UTC, 2026-10-09 02:59 to 03:07)  
Beads issue: `full_spooky_proto-54w.9` (C-016, C-017; decision 0028)

The bench was the NUCLEO-H755ZI-Q with the RF board, audio shield and SD card of the
same day's [power-up trials](2026-10-08-sd-power-up-order.md), powered up battery first.
The operator confirmed it was connected and free and listened on the headphones. Spooky
Bench 0.7.0 flashed the image and transferred the WAV. CLI commands went to the target
CDC (COM3) through `build/console_p046.py`, logged in
`build/console-54w9-20261008a.jsonl`.

**Result: pass.** PTT silenced the radio in the monitor with no audible click at either
edge, the raw radio track was unchanged through every PTT interval of a recording, and
each press and release was stamped on the radio timeline.

## Image

| Item | Value |
| --- | --- |
| Build ID | `54w9-ptt-20261008a`, preset Debug, GNU Tools for STM32 14.3.1 |
| Source | `93d0a06` (`main`) plus the uncommitted 54w.9 change, archived as `build/54w9-ptt-20261008a.patch` |
| Manifest | `build/54w9-ptt-20261008a.json` |
| CM7 / CM4 SHA-256 | `31f59f1ed6f3ae1a882da11845ad10d89f572bde9e8183d7a84071652afd1600` / `d5ab1687e85e3d5a975b5e5c98bdfa8ca445b7aa8bbabc11c6dd008f94a91809` |

Debug and Release build with only the existing unused CubeMX `MX_*_Init` warnings.
Native host tests pass 31/31 (MSVC), including the new `monitor_ptt_test` and `MONITOR`
cases in `command_policy_test`. Host tests are not hardware evidence.

## Setup

The image boots with the Classic scan running on FM. `CLASSIC PAUSE` stopped it and
`TUNE 99100` parked the radio on 99.1 MHz (`RSSI=17 SNR=2 VALID=0`), which the operator
heard loud and clear.

## Listening

Three cycles of `MONITOR PTT ON`, 3 s, `MONITOR PTT OFF`, 3 s. The operator heard the
radio drop out and return each time, with no pop on any edge. `MONITOR` then read
`PTT=0 GAIN_Q15=32768 PRESSES=3 RELEASES=3 UNSTAMPED=0`; `DIAG STATUS` was clean.

Each reply carries the gain before the fade (32768 on press, 0 on release), because it
is sent before the next radio half is rendered.

The stamped intervals were 145,487, 150,239 and 145,442 frames (3.03, 3.13 and 3.03 s),
against 3.02, 3.13 and 3.02 s between the commands on the host.

## Recording

`RECORD START 30` with three PTT cycles from about 4.5 s: `REC146.WAV`, `RECORD PASS`,
1,441,792 frames (30.037 s), finalized, queues 1/8, longest write 24 ms, no overrun or
fault. `RECORD TIMELINE` gave `origin=16423219`, so each stamp minus the origin is a
WAV frame. `spookybench wav inspect` transferred the file with a passing CRC check
(run `2026-10-09T030609.105547_0000-370c24a2`); a copy is `build/REC146-54w9.WAV`.

| Interval | WAV frames | Radio L RMS | Radio R RMS |
| --- | --- | --- | --- |
| Before PTT | 0 to 218,121 | 356.9 | 357.0 |
| PTT 1 | 218,121 to 362,859 | 342.4 | 342.5 |
| Between | 362,859 to 508,106 | 335.4 | 335.4 |
| PTT 2 | 508,106 to 653,374 | 346.0 | 346.1 |
| Between | 653,374 to 798,652 | 377.2 | 377.2 |
| PTT 3 | 798,652 to 944,186 | 345.6 | 345.6 |
| After PTT | 944,186 to 1,441,792 | 370.8 | 370.9 |

The longest run of zero samples on both radio channels was one frame. The radio track's
level inside the PTT intervals is within the spread of the level outside them: PTT did
not reach the raw capture. The microphone track was 2.5 to 3.5 RMS throughout (a quiet
room).

## Latch

A lost release is covered by host tests only: `context_test` checks that reconciliation
ends PTT on a page and in a menu, and that PTT ends when a utility opens while it is
held. `AudioPath_SetPtt(false)` with PTT already off does nothing. The physical Button 1
path is not wired on `main`; it is exercised on the demo image.

## Not exercised

- Button 1: this image has no physical-control path; PTT came from the CLI.
- A press during a receiver transition (the gated stream) or before the radio stream
  starts (`UNSTAMPED`).
- The fade measured on the monitor output; only listening judged it.
- How closely a stamp follows the press; the stamps are foreground observations with
  the 3,600-frame bound of decision 0012 item 6.
