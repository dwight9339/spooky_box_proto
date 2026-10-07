# Field-to-Instrument chord with clip load (demo image)

Date: 2026-10-07 (local, UTC-6; console timestamps are UTC, 16:28 to 16:41)  
Beads issue: `full_spooky_proto-p04.6` (demo track, decision 0011 item 15)

**This is demo-image evidence.** It covers the opt-in `Demo` preset on
`demo/halloween-2026` only. It does not deliver or qualify `full_spooky_proto-v7l.2`
(transactional Field save, switch and load).

The bench was the NUCLEO-H755ZI-Q with the UI board, the radio path and the SD card used
for p04.5 ([2026-10-04 rolling capture](2026-10-04-rolling-capture-demo.md)). The operator
confirmed it was connected and free, drove the physical controls and listened on the
monitor output. Spooky Bench 0.7.0 flashed the image on Windows. CLI queries went to the
target CDC on COM3 through an ad hoc pyserial console, `build/console_p046.py`; its log is
`build/console-p046-20261007a.jsonl`.

**Result: pass.** Every acceptance check behaved as designed on the first image. The clip
load takes about 5.5 s after the save, longer than the design estimate of about 1 s
(see Load time).

## Image

| Build ID | Source | CM7 SHA-256 | CM4 SHA-256 |
| --- | --- | --- | --- |
| `p046-demo-20261007a` | `e105ce45f8ad9ce030cb889b3d6643318e718c6a`, clean | `d29fe9358466c9819cedfa24c003120d0e9ed7db31ecd5945c96e3765ce2969c` | `7f23d9c8b1f86ccb4aef393f3bd27a6a074bfa07a6cd602a556f153fc44c3f7f` |

Preset `Demo`, GNU Tools for STM32 14.3.1. The manifest is
`build/p046-demo-20261007a.json`, and the flash passed with verification
(`build/flash-p046-demo-20261007a.json`). Demo, Debug and Release build; the only warnings
are the existing unused CubeMX `MX_*_Init` functions. In the Demo image AXI `RAM_DMA` holds
471,680 of 524,288 bytes, including the 144,000-byte clip. Native host tests pass 36/36
(MSVC 14.29) after `e105ce4`, which replaced a constant `CHECK` that MSVC rejects (C4127)
with `_Static_assert`. Host tests are not hardware evidence.

## Design under test

- **Chord.** Outside a session, C-010 (Shift plus Buttons 0 and 1) asks rolling capture to
  save, switches Context to Instrument and expects the clip from that save. Instrument
  shows the clip's state while it saves and loads.
- **Load.** After the save commits, the clip is the last 144,000 frames before the save
  point: the mono sum of radio L and R, halfband-decimated to 24 kHz, 16-bit
  (`clip_decimator.c`). It is read from the committed `CAPS/CnnnSmm.WAV` segments in
  2,048-frame steps, one per recorder foreground pass when no block is pending, under the
  recorder's storage lease (`demo_clip.c`).
- **Playback.** A READY clip plays as a plain loop in the monitor stage while Instrument
  is shown (`clip_player.c`, upsampled to 48 kHz stereo). The raw capture is copied before
  the monitor stage and is unchanged.
- **Failures.** A refused or failed save, or a failed load, shows its reason and posts a
  Shift plus Button 0 gesture back to Field (C-028). An unloaded clip is never shown or
  played.
- **Sessions.** The session prompt is refused in Instrument with a visible reason.

## Chord and loop

Rolling capture held 31.7 s before the first chord. The operator fired C-010 three times,
returning to Field with C-028 between them. Each time the OLED showed SAVING for about a
second, then LOADING for several seconds, then the loop played. The operator heard the
last three seconds before the chord.

After the third load `DEMO CLIP` reported `STATE=READY CAPTURE=C006 SAMPLES=72000 MS=3000
PLAYING=1 LOADS=3 FAILURES=0`. Loads took `LOAD_MS=5487` (latest) and at most 5,549 ms from
the save's commit to READY; the longest load step was 30 ms. The longest save was 873 ms.
Captures C004 to C006 were committed; C001 to C003 were already on the card from p04.5.

Rolling capture kept running: `RETAINED_MS=67669`, queues 1/8, the longest block write
51 ms and the longest segment change 54 ms, no faults and no allocation failures.

## Session prompt, return to Field and failure paths

| Check | Operator saw | Board |
| --- | --- | --- |
| Button 0 hold in Instrument (C-091) | Refused with its reason; the loop kept playing; no session | `PROMPTS_REFUSED=1` |
| Shift plus Button 0 in Instrument (C-028) | Field with live radio | `SCREEN=CLASSIC` |
| C-010 during a session | Rejected on the OLED; stayed in Field | No save made (`SAVES` unchanged). The session `REC145.WAV` passed: 30.122 s, queues at most 1/8, `margin=OK`. Rolling restarted after it (`STARTS=2`). |
| C-010, C-028 during the load, C-010 again | A fault, then Field | The first chord committed C007. The second was busy (`BUSY=1`): `STATE=FAILED FAULT=SAVE_BUSY`, one return posted (`RETURNS=1`, none refused) |

The busy case also ended the C007 load already in progress, as designed on 2026-10-04: a
busy save request fails the clip and returns to Field.

At the end `DIAG STATUS` reported `LOOP_MAX_MS=61 SD_MAX_MS=51 RADIO_OVR=0 PDM_OVR=0
SD_ERR=0 AUDIO_ERR=0 HAS_FAULT=0`, `DIAG LAST` was `NONE`, and the logger had dropped
nothing. No foreground budget was exceeded.

## Load time

The load reads about 73 steps (70 chunks plus segment opens) in about 5.5 s, so it
advances one step every 75 ms or so. A step takes at most 30 ms, so the pace is set by how
often the recorder's foreground pass reaches the load, not by the SD card. The OLED shows
LOADING throughout, so the wait is visible. A 5 s clip (decision 0025, `p04.18`) would take
about 9 s at this pace.

## Not exercised

- A load failure from a missing or short segment or a read error. It ends in the same
  FAILED state and return path as the busy case.
- WAV inspection of the captures and of `REC145.WAV` on the host.
