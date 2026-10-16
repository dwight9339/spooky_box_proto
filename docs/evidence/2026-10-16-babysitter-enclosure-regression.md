# Hardware regression after the babysitter swap and enclosure rebuild

Date: 2026-10-16 (local, UTC-6; Spooky Bench log timestamps are UTC)  
Beads issue: `full_spooky_proto-lin`

The SparkFun Battery Babysitter was replaced after its USB connector was torn off.
The rig was then rebuilt into a new enclosure that mounts the PAM8302 speaker and
carries a microSD slot extender from the audio shield's card slot to the enclosure
edge (see [prototype hardware](../design/prototype-hardware.md)). The operator had
seen charging work and the USB CDC port answer, and confirmed the board was connected
and free. The new babysitter's charge-limit switch is on USB500 (switch 1 OFF,
switch 2 ON). The bench was in the basement.

**Result: pass, with FM reception inconclusive.** Recording, SD through the extender,
the new babysitter's USB, VBUS sense, charging and fuel gauge, the UI board, the
magnetometer, the headphone and speaker paths, and the microphone all passed. No FM
station was received anywhere in the band. AM and LW matched earlier evidence, so the
radio and its loop path work. The FM result is attributed to the basement location
pending a recheck elsewhere.

## Images

All three built from `32189ff` (clean tree) with GNU Tools for STM32 14.3.1
(`14.3.1+st.2` bundle). Release was also built to confirm it compiles; it was not
flashed.

| Build ID | Preset | CM7 SHA-256 | CM4 SHA-256 |
| --- | --- | --- | --- |
| `lin-ipcsmoke-20261016a` | `IpcSmoke` | `44a61ac0c7be25c1f30532663ac33e10587dec599e9a2841daf385dce6049061` | `7d9d1116cb20cd8367de3d86a2ec8f067b1cf0fa0ea9220279c6cf9492489584` |
| `lin-debug-20261016a` | `Debug` | `63b18059bb5e4769f6790f50f32587668e0ea7557f516617d923d441b1ec3db4` | `d5ab1687e85e3d5a975b5e5c98bdfa8ca445b7aa8bbabc11c6dd008f94a91809` |
| `lin-speakermonitor-20261016a` | `SpeakerMonitor` | `beb6799c7101ad299b5bc5e34b6abaadb80dc1e4931fdc1739ae3a2457d95f4d` | `bb908efa2653115ca98dfa471075136edb03cfdcd6e03c3adfe04c5fd8451799` |

Manifests are `build/lin-<preset>-20261016a.json`. Spooky Bench 0.7.0 ran every
step. Its JSON results are `build/lin-*-20261016a.json`; the CLI transcript is
`build/lin-cli-20261016a.jsonl` and the UI watch log `build/lin-uiwatch-20261016a.jsonl`.

## Automated checks

| Check | Result | Detail |
| --- | --- | --- |
| Recording regression, 60 s, ambient (18:37 UTC) | Pass | All six stages. `REC153.WAV`, 2,883,584 frames; queue high-water 1/8 both; longest write 37 ms; no overrun, SD or audio error; IPC progressing throughout; 283 s CRC-verified transfer |
| SD basic, 64 MiB × 2 (18:45 UTC) | Pass | 134,217,728 bytes written and verified, scratch file removed, card identity unchanged; longest write 43 ms, longest read 8 ms, 0.5 MiB/s aggregate |

Through the extender, the SD basic run matched or bettered the 2026-10-01 endurance run
on the same envelope (`build/jjy7-sdbasic-endurance-1.json`: longest write 280 ms, 0.4
MiB/s). The card is a 59,344 MiB SDHC/SDXC card at four bits and `CLOCKDIV=2`.

The recording regression's microphone channel was very quiet (peak 13, RMS 2.2,
against peaks of 113 to 608 in earlier ambient runs), so the microphone was checked
with a stimulus below.

## Debug image checks

The first two Debug flash attempts failed with `serial_open` before any target
access. The host PC had slept, and on resume the Spooky Probe's CDC port enumerated
but would not open (`A device attached to the system is not functioning`). Replugging
the probe USB fixed it; the third attempt passed at 19:01 UTC.

The boot log reported PASS for the codec, SAI bridge, radio, fuel gauge, magnetometer
and USB enumeration. The new gauge's configuration was already active:

```text
[fuel] PASS: DEVICE_TYPE=0x0421 FW=0x0109 DM=0x48 CHEM_ID=0x0128
[fuel] 3984 mV, SOC=69%, 34.8 C, charging at 308 mA / 1227 mW
[fuel] remaining=2314 mAh, full=3375 mAh, design=3700 mAh; SOH=90% status=2
[fuel] flags=0x0108 BAT_DET=1 ITPOR=0 CHG=1 DSG=0 FC=0 SOC1=0 SOCF=0 CFGUP=0
[fuel] CONFIG: already applied; no data-memory write needed
```

The configuration was written on a boot before this session, presumably with the
babysitter USB connected, so the gauge measured its initial OCV under charge current.
That is not the order the [fuel-gauge configuration boot](../../BRINGUP.md#fuel-gauge-configuration-boot)
asks for. Treat SOC, SOH and the mAh values as preliminary until the gauge has learned
the cell.

| Check | Result |
| --- | --- |
| `BATTERY`, `CHARGE STATUS` with USB in | `VBUS=1 STATE=CHARGING`, 299 to 308 mA, 3985 mV |
| Boot on battery with USB out (probe reset, 20:06 UTC) | `VBUS=absent`; gauge `discharging at -258 mA`, 3991 mV, `DSG=1`; all other boot stages normal |
| USB hot-plug back in | CDC re-enumerated without a reset (same `BOOT=5`); `CHARGING` at 299 to 302 mA |
| `MAG READ` | `X=0uT Y=105uT Z=105uT DIAG=0` |
| `UI STATUS` | Documented idle levels, `DROPPED=0` |
| `UI WATCH` | Every button and encoder pushbutton pressed and released once; each encoder 4 to 6 detents CW then CCW, one step per event, no reversals |
| `UI LEDS` | `OK UI LEDS PASS channels=14 all-off=1` |
| `UI MATRIX PROBE`, `UI MATRIX ANIMATE` | ACK at `0x30`; `PASS pixels=81`; operator confirmed the comet |
| `UI DISPLAY TEST` | `PASS`; operator confirmed the test image |
| `SD STATUS` | Mounted, four-bit, `CLOCKDIV=2` |
| Headphones, AM 1000 kHz | Operator heard the radio |
| Microphone, 30 s recording with speech and claps | `REC154.WAV`, CRC-verified: microphone peak 25,475, RMS 136.0; radio peak 3,988; no clipped samples; queue high-water 1/8; longest write 32 ms |

## Radio

FM was swept from 87.5 to 107.9 MHz in 200 kHz steps. Every channel read RSSI 0 to 6
dBµV and none was valid. The stations earlier sessions received were absent:

| Frequency | Today | Earlier |
| --- | --- | --- |
| FM 94.0 / 97.0 / 98.5 MHz | RSSI 0, SNR 0 | 23 to 29 dBµV, valid |
| FM 99.1 MHz | 2 to 8 | 15 to 17 (2026-10-08, 2026-10-09) |
| AM 1000 kHz | 59 to 63 | 56 (2026-09-24) |
| AM 1500 kHz | 31 to 32 | 31 |
| LW 198 kHz | 33 to 34 | 35 |

The AM/LW loop path and the receiver are unchanged. The FM antenna was plugged in.
The bench had moved back to the basement, which attenuates VHF FM much more than AM
and LW, so the FM result does not show a rebuild fault. It was not rechecked at a
location with known FM reception.

## Speaker

The `SpeakerMonitor` image flashed at 20:11 UTC and booted with the headphones
detected (`monitor on headphones`). A 180 s UART capture recorded the operator's
checks on AM 1000 kHz:

```text
[audio] headphones removed; monitor on speaker
[audio] volume ADC=30557 -28.0 dB
...                                   (sweep to -1.0 dB, down to MUTED, back up)
[audio] headphones inserted; monitor on headphones
[audio] headphones removed; monitor on speaker
[audio] headphones inserted; monitor on headphones
[audio] headphones removed; monitor on speaker
```

Each insertion and removal logged exactly once. The operator reported the speaker,
pot control and headphone switching good in the new enclosure.

## Not exercised

- FM reception outside the basement.
- `SLEEP START`, the ten-minute recording, and rail measurements under load.
- A fuel-gauge configuration from a reset gauge with the USB disconnected.

The board was left running the `SpeakerMonitor` image.
