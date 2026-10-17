# Demo image: encoder and button lights flash on radio onsets (p04.29)

Date: 2026-10-17 (local, UTC-6; Spooky Bench and log timestamps are UTC, 16:50 to
17:07)  
Beads issues: `full_spooky_proto-p04.29`; noted on: `full_spooky_proto-p04.28`

This session checks the demo consumer of
[decision 0031](../decisions/0031-direct-led-radio-onset-flashes.md). In Field, every
lit encoder and button flashes on each radio onset that the decision 0013 detector
reports. The lights read onsets through their own reader of the onset feed and shape
each flash with the `light_flash` envelope from `main` (54w.8.1). Encoders keep their
hue, dark controls stay dark, and the decision's exemptions apply:

- item 2: no flash on a held button, or on Button 0 while it breathes or blinks its
  fault;
- item 3: no flash while Shift is held, while the prompt is open, or in Instrument.

Demo evidence qualifies the demo image only; it is not milestone evidence (roadmap,
demo track).

The operator confirmed the board was connected and free, with the UI board, radio, SD
card and battery in. The operator judged the flashes by eye and on a phone camera and
drove the physical controls. Spooky Bench 0.7.0 flashed the image. An ad hoc pyserial
script drove the CLI (`build/p0429-cli.py`, logged in `build/p0429-bench.log`); it is
not part of Spooky Bench.

**Result: pass.**

- **Flashes:** lit encoders and buttons flash together, in step with the matrix kicks.
  The operator found them clear, fading and still white, and visible on camera without
  banding.
- **Exemptions seen on hardware:** no flash while Shift was held, while the prompt was
  open, or on a held button. Button 0 breathed without flashes during a recording,
  while Button 1 and the encoders kept flashing.
- **Counters:** they show the lights take every onset the detector reports, and drop
  the ones that come while Shift is held or the prompt is open.
- **Instrument:** the detector produced no onsets there, so nothing could be suppressed
  on hardware; the host test covers it.
- **Recording:** a 30 s recording passed with no fault, and its WAV inspection passed.
- **Foreground stall:** turning rolling capture back on after the transfer stalled the
  foreground for 493 ms. This is `p04.28`, outside any recording.

## Image

The image is `f7c6a0b`, the merge of `main` into `demo/halloween-2026` (`p04.30`,
#136), plus the uncommitted p04.29 changes, archived as
`build/p0429-demo-20261017a.patch`. It was built with GNU Tools for STM32 14.3.1
(14.3.rel1.20251027-0700), preset `Demo`, with `SPOOKY_BUILD_ID` set to the build ID.
The flash passed (run `2026-10-17T165048.444719_0000-69a9dba2`).

| Build ID | CM7 SHA-256 | CM4 SHA-256 | Source patch SHA-256 |
| --- | --- | --- | --- |
| `p0429-demo-20261017a` | `82865b29e1387fbfa3f5aa667c29d70562bcd7b5b6b3c411a22da3df6bd453c1` | `7f23d9c8b1f86ccb4aef393f3bd27a6a074bfa07a6cd602a556f153fc44c3f7f` | `7a25873771f21e6b64d78c797be9306f9911c99ed0ffc2da02e1b6f2ad368008` |

Demo, Debug and Release build with the three known `main.c` warnings. The native host
tests pass 49/49 (MSVC), including five new `demo_lights_test` cases:

- step sizes, hue and decay;
- dark controls;
- the item 2 exemptions;
- the item 3 suppression, including a flash in progress ending for good;
- the flash settings.

Host tests are not hardware evidence.

## Flashes on Classic

`DEMO LIGHTS` after boot:
`FLASH=333,667,1000/240ms ONSETS=0 SUPPRESSED=0`, levels as set in p04.12 (idle 400,
encoder 800, balance `1000,450,350`). Classic scanned FM.

The operator watched Classic for about 20 s:

| Check | Operator |
| --- | --- |
| Do lit encoders and both buttons flash together, in step with the matrix kicks? | In step |
| How do the flashes look? | Clear pulses that fade; encoders stay white |
| Phone camera, video | Visible, no flicker or banding |

**Counters.** The detector's total (`CLASSIC ACTIVITY ONSETS`, small/medium/large)
against the lights' counters:

| Interval | Detector | Lights `ONSETS` | Lights `SUPPRESSED` |
| --- | --- | --- | --- |
| 20 s on Classic | +14 (7 to 21) | +13 (8 to 21) | 0 |
| Shift held, prompt held, Button 1 held (10:54:14 to 10:56:03) | +37 (80 to 117) | +26 (80 to 106) | +11 |
| 15 s in Instrument | 0 (222) | 0 (211) | 0 |
| 30 s recording and the rolling restart after it | – | +30 (218 to 248) | 0 |

The lights take at most one onset per foreground pass, the largest since the last
pass, so the two totals can differ by an onset that falls in the same pass or between
the two queries. Over the Shift and prompt interval, onsets taken plus onsets dropped
equal the detector's count exactly.

## Exemptions

| Check | Operator |
| --- | --- |
| Hold Encoder 3 (Shift) about 10 s | No flashes while held; flashes resumed after release |
| Hold Button 0 (start prompt) about 10 s, then release to cancel | No flashes while the prompt was open |
| Hold Button 1 about 10 s | Button 1 steady at full; Button 0 and the encoders kept flashing |
| Switch to Instrument, about 10 s, then back | No flashes in Instrument |
| 30 s recording (session active) | Button 0 breathed without flashes; Button 1 and the encoders flashed |

In Instrument the detector reported no onsets during a 15 s sample (`SCREEN=INSTRUMENT`,
detector total 222 before and after). The Instrument exemption was therefore not
exercised on hardware. `test_flash_suppressed` covers it on the host.

Not checked on hardware: the Button 0 fault-blink exemption. It needs a rejected or
aborted session; `test_flash_exempt_buttons` covers it on the host.

## Recording

With the lights flashing, `RECORD START 30` gave:

| | Result |
| --- | --- |
| Prepare | `FIELD/F0047/AUDIO000.WAV`, 8,448 KiB preallocated in 800 ms |
| During | `queues=0/8,0/8 max-write=25ms margin=OK` at 9.1 s and 19.3 s |
| End | `OK RECORD PASS`, 1,441,792 frames, 30.037 s of audio in 30,110 ms, finalized; `RECORD DIAG queues radio=1/8 pdm=1/8 max-write=26ms margin=OK`; `RECORD EVENTS written=123 lost=0` |
| `DIAG` | `DIAG LAST NONE` before and after; `HAS_FAULT=0`; `RADIO_OVR=0 PDM_OVR=0 SD_ERR=0 AUDIO_ERR=0`; `DIAG LATENCY` with no violations |
| Afterwards | Rolling capture restarted (`STARTS=2 FAULTS=0`, `ROTATE_MS_MAX=36`) with no violation |

**WAV inspection** (run `2026-10-17T170406.899206_0000-0d165721`, saved to
`build/wav-F0047-p0429a.json`) passed, with rolling capture turned off (`ROLL OFF`)
because it holds the card:

- 3 channels, 48 kHz, 16-bit, 30.037 s; frames and data bytes equal the recorder's PASS
  values.
- Radio left and right RMS 480, peak 5,067, correlation 0.99995, no clipping.
- Microphone RMS 116, peak 1,274, no clipping.
- CRC-verified transfer, SHA-256
  `8d5c29f8210c5a7918d7c3ceff3694f53aaf25d13a218788dce247878c5698e7`; target healthy
  afterwards.

## Foreground stall at `ROLL ON` (p04.28)

After the transfer, `ROLL ON` restarted rolling capture with `ROTATE_MS_MAX=494` and
`STEP_MS_MAX=479`. That raised `HAS_FAULT=1`: `DIAG LAST FOREGROUND_BUDGET A=0 B=493`,
with `DIAG LATENCY` showing loop 493 ms and recorder 479 ms, two violations. No
recording was running. This is the rolling-restart stall filed as `p04.28`, larger
than the 151 to 198 ms seen before. The lights code takes no storage path.

## Not exercised

- **Fault-blink exemption:** not checked on hardware (above).
- **Instrument suppression:** not exercised on hardware, because the detector produced
  no onsets there (above).
- **Other screens:** flashes on the band menu, Manual and Utility screens were not
  checked.
- **Bench tuning:** `DEMO LIGHTS FLASH` was not used; the defaults were kept.
- **Listening:** no listening check.

## Artifacts

In `build/`:

- the manifest `p0429-demo-20261017a.json` and the source patch
  `p0429-demo-20261017a.patch`;
- `flash-p0429-demo-20261017a.json` and `wav-F0047-p0429a.json`, each naming its run
  directory under `%LOCALAPPDATA%\SpookyBench\runs\`;
- the CLI log `p0429-bench.log`.
