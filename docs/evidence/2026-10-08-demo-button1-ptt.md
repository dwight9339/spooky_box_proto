# Button 1 PTT on the demo image

Date: 2026-10-08 (local, UTC-6; console timestamps are UTC, 2026-10-09 03:14 to 03:15)  
Beads issue: `full_spooky_proto-p04.24` (demo track; decision 0011 item 10 as amended
by decision 0028)

**This is demo-image evidence.** It covers the opt-in `Demo` preset on
`demo/halloween-2026` only. The PTT fade itself is product code, proven on `main` in
[monitor-only PTT](2026-10-08-monitor-only-ptt.md).

The bench was the one of that session, powered up battery first. The operator confirmed
it was connected and free, pressed Button 1 and listened on the headphones. Spooky Bench
0.7.0 flashed the image. CLI queries went to the target CDC (COM3) through
`build/console_p046.py`, logged in `build/console-p04ptt-20261008a.jsonl`.

**Result: pass.** Button 1 in Field faded the radio out of the monitor while held and
back on release, with no click at either edge, while rolling capture kept running.

## Image

| Build ID | Source | CM7 SHA-256 | CM4 SHA-256 |
| --- | --- | --- | --- |
| `p04ptt-demo-20261008a` | `dd9af2f` (`claude/p04-demo-ptt`) | `c293acaae03ee31810bb1955b7ce3862f39de319b6ab81092ad4db62f5409d9c` | `7f23d9c8b1f86ccb4aef393f3bd27a6a074bfa07a6cd602a556f153fc44c3f7f` |

`dd9af2f` merges `main` (PR #113 and PR #114) into `demo/halloween-2026` and passes the
demo's `CTX_CMD_MONITOR_PTT` to `AudioPath_SetPtt` in `CM7/App/demo_field.c`. The CM4
image is unchanged from p04.14 to p04.16. Debug, Release and Demo build with only the
existing unused CubeMX `MX_*_Init` warnings; native host tests pass 46/46 (MSVC). Host
tests are not hardware evidence.

## Walkthrough

At boot `MONITOR` read `PTT=0 PRESSES=0`, rolling capture was `RUNNING` and `DIAG
STATUS` was clean. `CLASSIC PAUSE` and `TUNE 99100` parked the radio on 99.1 MHz.

The operator held Button 1 for about 3 s three times. The radio dropped out of the
headphones while it was held and came back on release, with no click. Then:

- `MONITOR`: `PTT=0 GAIN_Q15=32768 PRESSES=3 RELEASES=3 UNSTAMPED=0`, the last press
  held 165,515 frames (3.45 s).
- `ROLL`: `STATE=RUNNING`, `FAULTS=0`, queues at most 1/8, longest write 25 ms.
- `DIAG STATUS`: `RADIO_OVR=0 PDM_OVR=0 SD_ERR=0 AUDIO_ERR=0 HAS_FAULT=0`.

## Not exercised

- The rolling capture's radio track during PTT: no capture was saved and inspected.
  The same monitor stage left a recording's radio track unchanged on `main`.
- PTT with a menu open, in a session, or across a mode switch; the behavior model's
  host tests cover the gesture rules.
- PTT in Instrument, where Button 1 has no PTT binding and the clip replaces the radio.
