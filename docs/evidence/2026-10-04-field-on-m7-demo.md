# Field on the M7 (demo image)

Date: 2026-10-04 (local, UTC-6; Spooky Bench and CLI log timestamps are UTC)  
Beads issue: `full_spooky_proto-p04.3` (demo track, decision 0011 item 12)

**This is demo-image evidence.** It covers the opt-in `Demo` preset on
`demo/halloween-2026` only. It proves nothing about Debug, Release or the product
integration under `full_spooky_proto-54w.5`.

This session ran the Field slice on the M7 without product IPC on the NUCLEO-H755ZI-Q
bench, with the UI board (switches, encoders, button and encoder LEDs, SSD1309 OLED,
IS31FL3741 matrix, TMAG5273), the radio path, an SD card and the 3,700 mAh LiPo. The
operator confirmed that the board was connected and free and drove the physical
controls. Spooky Bench 0.7.0 ran on Windows, with target CDC on COM3 and Spookyprobe on
COM6. CLI commands went through an ad hoc pyserial script that is not part of Spooky
Bench. Console captures are Spooky Bench `console` runs.

**Result:** the demo image boots into Classic. Every gesture in scope worked on the
hardware:

- C-103 to C-106: run and pause, rate, direction and distance
- C-110 and the hold time
- C-099 to C-102: the band menu
- C-090, C-092 and C-093: the session prompt

The OLED, button lights, encoder lights and matrix rendered the published state. Two
recordings passed with the matrix running during capture:

- REC138, 30 s from the CLI
- REC139, an open-ended session started and stopped from the prompt

Neither had queue overruns or budget violations. Lights were adjusted at the operator's
direction. The first session attempts failed because the board was on USB power alone;
that was a supply fault, not a firmware fault.

## Images

All images were built from `75880b64008efd3dcdc2b599f707d8ab06bd0d78` (the merge of
`main` 492dca9 into `demo/halloween-2026`) plus the uncommitted `p04.3` changes. Each
build's source is archived as a patch (`build/<build ID>.patch`, SHA-256 in the
manifest). The toolchain was GNU Tools for STM32 14.3.1+st.2, preset `Demo`. Every flash
passed.

| Build ID | Change | CM7 SHA-256 | Source patch SHA-256 |
| --- | --- | --- | --- |
| `p043-demo-20261004a` | First demo image: button idle 10% perceived, encoder lights cyan | `54d1b66a5bc44ebff0ed922ee9da386ac4ad18c77ea52ab1dc79a80bad90ec8b` | `1d566959e65a3d46f41651cc4d062f5e6c74f6a1f8915e29ca355eaa51c29dfe` |
| `p043-demo-20261004b` | Button idle 40% perceived and set with `DEMO LIGHTS IDLE`; encoder lights white; breathing from idle upward | `26b2ad3709c2999803e9e137ff481c60b697b89c19ec9ba118a9ee02afe17de3` | `125058d2d6336a3dee906ae779040e2094ab5998206a78d3be7b54ae2f34a62a` |
| `p043-demo-20261004c` | Breathing from off up to the peak; left on the board | `656f7de9c3d6e20dcfd7f78a1426778a483b56984f4c03e9c1919024e270b138` | `c2ac6ece3f685840b4688cb1250c80faa796f7abfe954394dbe4b7127f3af9a4` |

The CM4 image was `fe162352187af5241efa8f2eba35cae68c6975946f2ce96f883866ed8d71a465`
for all three. In the Demo image it returns early from `MX_TIM16_Init()`, so the M7 is
the only owner of TIM16 and PF6. Native host tests passed 32/32 (MSVC 14.29) for each
build; host tests are not hardware evidence.

## Boot

The boot log of build a (flash run `2026-10-04T160333.351631_0000-1c53fdd1`) shows the
existing bring-up passing. It then printed
`[demo] Field on the M7 (decision 0011 item 12): display=1 lights=1`. Classic started
`UNABLE REASON=RADIO_STOPPED` and changed to `RUNNING` on FM once the radio started,
13 ms later (decision 0016 item 22). The OLED showed the Classic page and matrix
feedback was on (`UI MATRIX FEEDBACK=1`). The operator confirmed the text is right side
up and readable.

## Controls

The console captures `2026-10-04T160421.221893_0000-89d6a4ae` (build a, 600 s) and
`2026-10-04T164940.798680_0000-9b6db5b4` (build c, 180 s) logged every gesture as a
published event.

| Control | What the log shows |
| --- | --- |
| C-103, Encoder 0 click | `RUN_STATE STATE=PAUSED`, then `RUNNING` on the second click |
| C-104, Encoder 0 turn | `RATE` events: 120, 100, 80, 100 and 120 per minute on FM, then 100 on AM, all `LIMITED=0` |
| C-105, Encoder 1 click | `DIRECTION DIR=DOWN`, then `DIR=UP` |
| C-106, Encoder 1 turn | `DISTANCE` events (1 to 4 to 5 to 4) |
| C-110, Encoder 2 turn | `EDGE` stepping through WRAP, BOUNCE and STOP |
| Encoder 3 turn | `HOLD_TIME` 0 to 2 and back |
| C-099, Encoder 1 long press | `ctx MENU_OPENED 2` (band) with the current band highlighted |
| C-100, Encoder 0 turn | `ctx MENU_HIGHLIGHT` 1, 2, 1 |
| C-101, Encoder 0 click | Menu closed, `[radio] switched to AM 1000 kHz`; Classic kept running and scanned AM |
| C-102, Encoder 1 click | `MENU_CLOSED` 4.0 s after the last scroll, before the 5 s timeout |
| Menu timeout | `MENU_CLOSED` 5,009 ms after opening, twice |
| C-090 and C-093 | `PROMPT_OPENED_START`, then `PROMPT_CANCELLED` on release, twice |
| C-090 and C-092 | `PROMPT_OPENED_START`, then `PROMPT_CONFIRMED_START` and a session start; `PROMPT_OPENED_STOP`, then `PROMPT_CONFIRMED_STOP` and a session stop |
| Shift (Encoder 3 hold) | `SHIFT_ENTERED`, then `SHIFT_LEFT`, twice |

`DEMO STATUS` after the first capture reported 80 inputs and 47 gestures. Nothing was
refused, unbound or reconciled, and the event queue peaked at 2.

The band change printed `switched to` twice. Both lines come from one switch: the radio
service and the radio adapter each log it. That is pre-existing on `main` and filed as
`full_spooky_proto-wl7`.

## Surfaces

The operator observed these on the hardware:

- OLED: the Classic page, the band menu, the start prompt and the notices, including
  `BAND -> AM ...` and `BAND NOW AM`.
- Button LEDs: lit while pressed. When a recording failed, Button 0 gave the three-blink
  fault pattern (PRES-LED-05).
- Encoder LEDs: lit per screen.
- Matrix: the EMF square and onset kicks, and the recording border during capture.

The operator directed three changes:

- On build a the button idle level of 10% perceived (PRES-LED-01) was too dim to see in
  normal light. Build b raised it to 40% (about 11% duty) and made it adjustable. The
  operator confirmed the buttons are visible at idle with room to brighten when
  pressed.
- The operator wants white as the default encoder colour. Build b changed it.
- The operator wants the session breathing to go all the way to off. Build c changed
  it.

The operator also wants the encoder and button idle brightness to match and to be a
setting eventually. The encoder LEDs are on/off GPIO, so that is filed as
`full_spooky_proto-p04.12`.

`DEMO STATUS` reported no OLED page write failures. The longest page write was 923 to
938 µs.

## Recording with the matrix running

**Supply fault first.** On build a the first session from the prompt prepared
`REC135.WAV`. Its first audio write failed with `FR_DISK_ERR`, and the session aborted
with nothing recorded. The display showed the fault, and Button 0 blinked three times.
Every later start failed at SD init (`FR_NOT_READY`, `hal=0x00000004`, then
`0x10000000` after a system reset). COM3 kept disappearing and reappearing.

The operator found the battery switch off, so the board was running from USB alone.
With the battery switched on, the CDC link stayed up and the card mounted
(`SD PRESENT=1 MOUNTED=1`, battery 4,186 mV). Every recording after that passed. This
is a bench supply fault, not a firmware result.

**REC138, 30 s from the CLI (build c).** `RECORD START 30` gave:

- Prepared in 496 ms. `RECORD PASS`: 1,441,792 frames, 30.037 s of audio in 30,090 ms,
  finalized.
- Queues 0/8 throughout (`RECORD DIAG` radio 1/8, pdm 1/8). Longest write 25 ms,
  margin OK.
- `DIAG LATENCY` while recording: loop at most 33 ms, MATRIX 3 ms, DEMO 3 ms. No
  service broke its budget.
- `DIAG QUEUE`: 741 posted and dispatched, nothing rejected, no reconciles.
- WAV inspection (run `2026-10-04T164131.813528_0000-2d44ffbc`, saved to
  `build/wav-REC138-p043-20261004c.json`) passed:
  - 3 channels, 48 kHz, 16-bit, 30.037 s.
  - Radio left and right RMS 342, peak 1,539, correlation 0.99997, no clipping.
  - Microphone RMS 60, peak 800.
  - SHA-256 `bd24e5fd15b5e5e58a8b0b644d2e3449b0ec148d3e755976e43ce4f46bcca477`.

**REC139, open-ended from the prompt (build c).** Started and stopped with Button 0 and
Encoder 0. `RECORD PASS`: 21.418 s of audio in 21,489 ms, `reason=stopped`, queues 1/8,
longest write 25 ms, margin OK.

While each session ran, Classic published `UNABLE REASON=SESSION` and the OLED showed it.
The tune guard stays in place until `full_spooky_proto-p04.4`. After each session
Classic went back to `RUNNING`.

**Matrix frame rate.** In the demo image the matrix keeps writing during a capture, at
most one register run per foreground pass. During REC138 it composed 190 frames in
30,079 ms (6.3 frames/s), with 3 superseded before they were fully written. With no
recording it composed 140 frames in about 21 s (about 6.6 frames/s). `DROPPED_STEPS`
stayed 0 and `FAILED` 0. `RUN_US_MAX` was 3,016 µs.

Both rates are the animation's own tempo: 160 ms per step in the lowest EMF bucket.
Limiting the writes per pass bounded the bus time per pass, but it did not lower the
composed frame rate. Whether the matrix should also slow its animation while recording
is open with the user. The magnetometer is still not read during a capture, so the EMF
outline showed the unknown grey while recording.

## Not covered

- The hold-time and EMF lines on the OLED were not checked in detail. Neither were the
  Manual, Instrument and Utility screens, which say "not in this build".
- PTT has no effect yet (`full_spooky_proto-54w.9`), and the capture save is not in this
  build (`full_spooky_proto-p04.5`).
- No long recording, and no recording regression run on the demo image. Scanning while
  recording is `full_spooky_proto-p04.4`.
