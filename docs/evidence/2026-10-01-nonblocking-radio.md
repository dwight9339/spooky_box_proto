# Non-blocking RadioSm radio control

Date: 2026-10-01  
Beads issue: `full_spooky_proto-54w.28`

This session qualified the non-blocking Radio machine on the NUCLEO-H755ZI-Q bench.
The operator confirmed that the board was connected and free, with the SD card,
radio and microphone path installed. Spooky Bench 0.7.0 ran on Windows with target
CDC on COM3 and Spookyprobe on COM6. Spooky Bench timestamps are UTC.

## Image

| Preset | Manifest | Build ID | CM7 SHA-256 | CM4 SHA-256 |
| --- | --- | --- | --- | --- |
| IpcSmoke | `build/bench-54w28-ipcsmoke-20261001a.json` | `54w28-radiosm-ipcsmoke-20261001a` | `8d5a62805745e60187940fa953e850b417f628a70757e135e7608b603a5d814c` | `31832b6b29c8382eb5c7919652d6123e774f6c8363c2c05cac6c1d52106f9017` |

Built from clean source `dc2742abba35aa4603a4e828999b3b1b954bf138` (branch
`claude/54w6-nonblocking-radio`) with GNU Tools for STM32 14.3.1+st.2. Debug,
Release and RadioTuneQual also built; the only warnings were the existing unused
CubeMX `MX_*_Init` functions. Native host tests passed 16/16 (MSVC 14.29), and
`behavior_model.py --check` reported all four diagrams up to date. Host tests are
not hardware evidence.

## Recording regression

One `test recording-regression --seconds 60 --stimulus ambient` run,
`2026-10-01T155637.389062_0000-3c3c2474`: **pass**, all six stages.

- Target reported build `54w28-radiosm-ipcsmoke-20261001a`, boot epoch 3.
- `REC050.WAV`: 2,883,584 frames, 17,301,504 data bytes, 60.074 s audio, 60,116 ms
  elapsed. Transfer CRC32 `b221efbe`, SHA-256
  `09f8a8f0551eb5fc91f12cb64693cc5677cdbca2d08f80a219986f5a5181ad48`.
- Radio and PDM queue high-water 1/8, zero radio/PDM overrun, SD, audio, fault,
  logger-loss and IPC error counters. Maximum SD write 43 ms. IPC stayed `LINK=UP`.
- Radio channels mono-like (correlation 0.99997); radio/microphone correlation
  -0.0014; no clipped samples. The radio sat on FM 99.1 MHz (no station, RSSI 16).

`DIAG STATUS` after the run reported `LOOP_MAX_MS=193`, up from 78 before
`RECORD START`, with no budget violation. That figure is the unconditional loop
maximum and includes non-recording passes. A `DIAG LATENCY` query before any further
command reported the recording-only loop maximum as 50 ms against the 75 ms budget,
and recorder 43 ms against 70 ms, with zero violations in every service. The 193 ms
pass was therefore outside the recording window; its event history had wrapped, so
it cannot be confirmed to be the record-start pass excluded by `8lw.21`.

No listening check was made; signal statistics are not an audio-quality verdict.

## Tune durations

Measured on the same running image after the regression, not recording, using an ad
hoc pyserial script (not part of Spooky Bench). Each figure is the host round trip
from writing the command to receiving its `OK RADIO` reply, so it includes USB CDC
latency and up to one foreground loop pass and is an upper bound on the receiver
tune time. Eight in-range targets per band; every tune succeeded.

| Band | Band switch (ms) | Tune min (ms) | Tune median (ms) | Tune max (ms) |
| --- | ---: | ---: | ---: | ---: |
| FM | 146 | 17.9 | 22.4 | 49.3 |
| AM | 320 | 166.7 | 170.9 | 172.7 |
| SW | 235 | 80.9 | 81.2 | 88.9 |
| LW | 307 | 152.9 | 153.0 | 154.9 |

The FM maxima (49.3 ms at 95.5 MHz, 42.9 ms at 92.3 MHz) were the two targets with
the strongest signal (RSSI 28 and 23). Raw results (untracked, in the local `build/`
directory): `build/tune-timing-54w28-20261001a.json` and, for the responsiveness
check below, `build/tune-overlap-54w28-20261001a.txt`.

## Foreground stays responsive during a tune

In AM, `DIAG STATUS` sent 5 ms after `TUNE` was answered before the tune completed,
three times out of three:

| Tune | `DIAG STATUS` reply | `OK RADIO` reply |
| --- | ---: | ---: |
| 540 kHz | +10.6 ms | +169.9 ms |
| 1600 kHz | +14.0 ms | +173.5 ms |
| 700 kHz | +11.0 ms | +170.3 ms |

So the CLI and foreground loop keep running while the receiver settles. Band
switching is still one synchronous step, as `docs/design/usb-cli.md` documents: the
band switches above raised the unconditional `LOOP_MAX_MS` to 323 ms. Band and tune
changes stay rejected while recording in this image; in-band tuning while recording
is the `RadioTuneQual` qualification in `full_spooky_proto-54w.6`, not run here.

The radio was left on FM 99.1 MHz and the IpcSmoke pair left running.
