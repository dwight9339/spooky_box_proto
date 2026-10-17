# Demo image: dimmable encoder LEDs (p04.12)

Date: 2026-10-16 (local, UTC-6; Spooky Bench and log timestamps are UTC, 2026-10-17
05:21 to 05:45)  
Beads issues: `full_spooky_proto-p04.12`; noted on: `full_spooky_proto-p04.28`

This session checks the demo image with all fourteen button and encoder LED channels
on timer PWM. Before this change the encoder channels were on/off GPIO, so they could
not match the buttons' idle level
([p04.3 evidence](2026-10-04-field-on-m7-demo.md)). The session also checks a fix
found in review: `UiBoardTest_SafeOff` (sleep entry, `UI OFF`) wrote GPIO levels to
pins that the demo had handed to the timers, so the LEDs kept their PWM duty. The fix
hands the pins back to GPIO, low, and stops the timers.

Demo evidence qualifies the demo image only; it is not milestone evidence (roadmap,
demo track).

The operator confirmed the board was connected and free, with the UI board, radio, SD
card and battery in. The operator judged brightness, tint and flicker by eye and on a
phone camera, and drove the physical controls. Spooky Bench 0.7.0 flashed, reset and
captured UART7 (target CDC on COM3, Spookyprobe on COM6). An ad hoc pyserial script
drove the CLI (`build/p0412-cli.py`, logged in `build/p0412-bench.log`); it is not
part of Spooky Bench.

**Result: pass.**

- **Matched idle:** at idle, the encoders match the buttons by eye with a neutral white.
  They needed their own level, set by a new `DEMO LIGHTS ENCODER` command: 800 permille
  L\* for the encoders against 400 for the buttons. The white balance is
  `1000,450,350`.
- **Flicker:** none on camera.
- **Prompt cue:** the start prompt's green Encoder 0 was too dim at the encoder level.
  It now shows at full lightness and is easy to see.
- **`SafeOff`:** after `UI OFF` and on sleep entry, all fourteen LEDs are dark.
- **Recording:** a 30 s recording passed, and its WAV inspection passed.
- **Foreground stall:** one `FOREGROUND_BUDGET` violation occurred during the
  rolling-capture start after a reboot, outside the recording. It matches `p04.28`.

## Images

Each image is the merge of `claude/p04.12-encoder-pwm` (`1569913`) into
`demo/halloween-2026` (`2fbfb65`), plus the uncommitted changes of this session, built
in a separate worktree. The builds used GNU Tools for STM32 14.3.1
(14.3.rel1.20251027-0700) and the `Demo` preset, with `SPOOKY_BUILD_ID` set to the
build ID. Each build's source is archived as `build/<build ID>.patch` against
`2fbfb65`. Every flash passed. The CM4 image was
`e88a3fa4c4b7c2bb1c78626dec73d44aac9fb585e83b2cdaceaeefd2a99334d8` for all four.

| Build ID | Change | CM7 SHA-256 | Source patch SHA-256 |
| --- | --- | --- | --- |
| `p0412-demo-20261016a` | p04.12 and the `SafeOff` fix; encoders at the buttons' idle level | `d4a549529e3375b81077c89242b2beae1e2b497856a0e1f86c3a6c5ae661f0c0` | `bd243f979a6da2e0cc3d4352138e3cda1024eab8a65bbdf71b902b27a262a967` |
| `p0412-demo-20261016b` | Separate encoder level (`DEMO LIGHTS ENCODER`, default 600); balance default `1000,450,350` | `cd39310b84eb523c3b76328f0ca90c53fd894c97c87af1021e6ede0cf730b6f4` | `945f7ccf7ae5a0a604dcbf0645e59a8e462bcf73367cfed8f04477b8523958c6` |
| `p0412-demo-20261016c` | Encoder level default 800 | `7f722f4de3f6de105294e9749342c9703503e1b8ab6728fdc611458751608731` | `95c5d94de8aed316de5205a07db1582f3851c9e692f3a6d3d5c422027fbbb4d3` |
| `p0412-demo-20261016d` | The prompt's Encoder 0 at full lightness; left on the board | `088957d1f9a0f42fd164cc9078f3d5e1e39581ba8c34704fae17ea84080709ab` | `149c7b71062b84f45b49afcd767014774409b0c0d4252595395aeb611f3c64a1` |

Demo, Debug and Release build with the three known `main.c` warnings. The native host
tests pass 46/46 (MSVC) on the branch and 48/48 on the merge with the demo tip. Host
tests are not hardware evidence.

## Boot

The boot log of build a (flash run `2026-10-17T052129.999890_0000-d79e02fe`) printed
`[demo] Field on the M7 (decision 0011 item 12): display=1 lights=1`. The buttons and
all twelve encoder channels came up on PWM. Rolling capture started, and Classic went
from `UNABLE REASON=RADIO_STOPPED` to `RUNNING` on FM.

## Matching the encoders to the buttons

The operator judged each step on the Classic screen, where all four encoders are white.

| Image | Setting | Operator |
| --- | --- | --- |
| a | Idle 400 (duty 113) on buttons and encoders, balance `1000,1000,1000` | Encoders dimmer than the buttons; white cool. No flicker on camera. |
| a | `BALANCE 1000 1000 600` | Still cool |
| a | `BALANCE 1000 1000 350` | Green tint |
| a | `BALANCE 1000 700 350` | Still green |
| a | `BALANCE 1000 450 350` | Neutral; encoders dimmer still (duty 113, 51, 40) |
| b | Encoder level 600 (duty 281), balance `1000,450,350` | Encoders dimmer |
| b | `DEMO LIGHTS ENCODER 800` (duty 567) | Matched, neutral |
| c | Defaults: encoder 800, balance `1000,450,350`, fresh boot | Matched, neutral, steady on camera |

Balance can only scale a channel down, and the idle level was shared with the buttons,
so the encoders could not be raised on their own. The operator chose a separate
encoder level over two alternatives: a balance gain above 1000, under which channels
clip at different points and white turns blue near full, and dimming the buttons.
Scaling a level down keeps white neutral at every level, which decision 0031's flashes
need.

The issue planned one idle level shared by buttons and encoders. The bench showed that
these LEDs need different levels, so the buttons keep `IDLE` and the encoders get
`ENCODER`.

## The prompt cue

On build c, the start prompt's Encoder 0 was green at the encoder level: green duty 255,
where before p04.12 it was fully on. The operator found it too dim to read as the
confirm cue (PRES-LED-06). With `DEMO LIGHTS ENCODER 1000` set for a test (green duty
450), the operator found it bright enough. Build d shows the prompt's green or yellow
Encoder 0 at full lightness, with the balance still applied, and leaves the other
encoders at the encoder level. On build d the operator confirmed:

- the green cue is easy to see, and the idle encoders still match the buttons;
- in Shift (Encoder 3 hold), the expected encoders light white at the matched level.

## `SafeOff`: `UI OFF` and sleep entry (build d)

| Step | Result |
| --- | --- |
| `UI OFF` | `OK UI OFF watch=0 leds=0 matrix-en=0 display=off`. The operator saw all fourteen LEDs dark, staying dark while pressing buttons and turning and pressing encoders; the OLED and the matrix were off as well. |
| Spooky Bench `reset` (run `2026-10-17T053616.113462_0000-3becc970`) | Pass; the demo came back with its lights. |
| `SLEEP START` | `OK SLEEP START; CDC will disconnect; updates continue on AUX UART7`. The operator saw all fourteen LEDs dark. UART7 (console run `2026-10-17T053639.643353_0000-4413dfb1`) shows `[sleep] RTC wake self-test passed; five-minute cadence armed` and a fuel-gauge report. |
| Spooky Bench `reset` in sleep (run `2026-10-17T053743.621941_0000-8161b63b`) | Fail, `openocd_failed`: `Cortex-M PARTNO 0x0 is unrecognized`. The debug port does not answer in the low-power monitor. |
| Blue USER button | The board rebooted into the demo. |

Before the fix, `SafeOff` left the LEDs on in both cases. That failure was found in code
review; this session did not flash an image without the fix.

While the display was off after `UI OFF`, `DEMO STATUS` counted 292 failed OLED page
writes (`PAGE_FAIL`): the demo keeps composing frames for the switched-off display.
This is not a lights change.

## Recording (build d)

After the USER-button reboot, `RECORD START 30` gave:

| | Result |
| --- | --- |
| Prepare | `FIELD/F0046/AUDIO000.WAV`, 8,448 KiB preallocated in 775 ms (directory search 412 ms) |
| During | `queues=0/8,0/8 max-write=28ms margin=OK` at 9.3 s and 21.5 s; `DEMO LIGHTS` unchanged |
| End | `OK RECORD PASS`, 1,441,792 frames, 30.037 s of audio in 30,109 ms, finalized; `RECORD DIAG queues radio=1/8 pdm=1/8 max-write=28ms margin=OK`; `RECORD EVENTS written=123 lost=0` |
| `DIAG STATUS` | `RADIO_OVR=0 PDM_OVR=0 SD_ERR=0 AUDIO_ERR=0`; `DIAG LAST` unchanged from before the recording |
| Afterwards | Rolling capture restarted (`STARTS=2 FAULTS=0`) with no new violation |

**WAV inspection** (run `2026-10-17T054200.105231_0000-6fb503d5`, saved to
`build/wav-F0046-p0412d.json`) passed, with rolling capture turned off (`ROLL OFF`)
because it holds the card (`FR_LOCKED`):

- 3 channels, 48 kHz, 16-bit, 30.037 s; frames and data bytes equal the recorder's PASS
  values.
- Radio left and right RMS 503, peak 5,393, correlation 0.99995, no clipping.
- Microphone RMS 70, peak 979, no clipping.
- CRC-verified transfer, SHA-256
  `653f805d9a077a019b10b0008550e117193292f3e4df12168fe2ca0d93a6e6f0`; target healthy
  afterwards.

This branch's `host/` predates support for `FIELD/` paths in `wav inspect`, so the
inspection ran with the demo tip's `host/` on `PYTHONPATH`. Rolling capture was turned
back on (`ROLL ON`, `STATE=RUNNING FAULTS=0`).

## Foreground stall during the rolling start (p04.28)

`HAS_FAULT=1` came from one `FOREGROUND_BUDGET` violation at 17.7 s after the
USER-button reboot (`DIAG LAST SEQ=196 MS=17667 EVENT=FOREGROUND_BUDGET A=0 B=198`):

- loop 198 ms;
- recorder 186 ms;
- `ROLL ROTATE_MS_MAX=211`.

It came while rolling capture started and rotated its first segments, before the
recording, and nothing changed it afterwards. The images change only lights code. It
matches the stalls filed as `p04.28` on a fuller card (this session's recording created
`FIELD/F0046`).

## Not exercised

- **Session lights during a recording:** no one watched Button 0's breathing or the
  lights during the recording.
- **Other screens:** the menu, Manual, Utility and Instrument screens were not looked
  at with the new levels. Their encoder colours are white at the encoder level, as on
  Classic.
- **Image without the fix:** no image without the `SafeOff` fix was flashed to show the
  failure on the bench.
- **Listening:** no listening check.

## Artifacts

In `build/`:

- the manifests `p0412-demo-20261016{a,b,c,d}.json` and the source patches
  `p0412-demo-20261016{a,b,c,d}.patch`;
- `flash-p0412-demo-20261016{a,b,c,d}.json`, `reset-p0412-20261016d.json`,
  `reset-p0412-20261016d-2.json`, `console-p0412-sleep-20261016d.json` and
  `wav-F0046-p0412d.json`, each naming its run directory under
  `%LOCALAPPDATA%\SpookyBench\runs\`;
- the CLI log `p0412-bench.log`.
