# Recording regression: first live loopback run (REC018)

Date: 2026-09-26  
Beads issue: `full_spooky_proto-5yv.2`

First hardware run of `spookybench test recording-regression`. It was the
known-stimulus acceptance run for the single unattended regression command. It used
Spooky Bench 0.7.0 from the uncommitted working tree (runner source SHA-256
`a05163e632b61295664151b45e88008f5046517dfbad4a68053c9c69e34aa3ac`). After this run,
the only runner source change was the default deadline formula (`regression.budget`),
raised from 450 s + 2 × duration to 450 s + 7 × duration. This run used the older
default of 630 s and finished in 453 s, so no stage behaved differently.

```powershell
host/.venv/Scripts/python.exe -B -m spookybench --json --profile host/bench.local.json test recording-regression --manifest build/bench-jjy1-ipcsmoke-20260926a.json --seconds 60 --stimulus loopback
```

Run directory: `%LOCALAPPDATA%/SpookyBench/runs/2026-09-26T201953.755776_0000-a213ee47`.
Result: `pass`, target health healthy, final state running, `human_required=false`.

## Image and setup

| Item | Value |
| --- | --- |
| Manifest | `build/bench-jjy1-ipcsmoke-20260926a.json` (same image as the [ten-minute baseline](2026-09-26-ten-minute-recording-baseline.md)) |
| Source revision | `ff7292f613f49a23b26f1907ae19e8412d42011d`, clean; no firmware changes since |
| CM7 SHA-256 | `3515acbbf8291a0e08002e8cc42e61c6772aa425b90b33a2246bd5a58b03dd59` |
| CM4 SHA-256 | `8f6e005ffefdf6896fcaf85d5a51554aa64e97ecae3672b412a94ed29ba377cc` |
| Stimulus | Earbud on the monitor output taped over the microphone (acoustic loopback of the radio program) |
| Radio | FM 99,100 kHz, RSSI 13 dBµV, SNR 0 dB, `VALID=0`: static, which serves as broadband program material |
| Volume | ADC 57097, 87%, attenuation 7.0 dB, not muted |
| SD | SDHC/SDXC, 59,344 MiB capacity, 58,690 MiB free before; recorder idle, `last-file=none` |
| Bench power | SYSOFF switch ON, babysitter USB plugged in, no other power sources (operator-reported; same as the ten-minute baseline) |

## Stages

| Stage | Result | Duration |
| --- | --- | ---: |
| `boot_smoke` | pass | 41.7 s |
| `prerequisites` | pass | 5.6 s |
| `recording` | pass | 108.9 s |
| `wav_inspect` | pass | 267.8 s |
| `accounting` | pass | 0 s |
| `alignment` | pass | 7.1 s |
| `post_transfer_health` | pass | 21.5 s |

Total run time was 453 s.

## Recording and retrieval

| Evidence | Value |
| --- | ---: |
| File | `REC018.WAV` |
| Frames / data bytes | 2,883,584 / 17,301,504 |
| Audio / elapsed | 60.074 s / 60.113 s |
| Progress records | 11 |
| Queue high-water | radio 1/8, PDM 1/8 |
| Max SD write | 26 ms |
| Transfer | 17,301,548 bytes, 17,165 frames, CRC32 `170921ea` |
| SHA-256 | `e89d0a465942dabde5c772809ca6638c32bcce73b9a6d84fdfb2245a9492a8f8` |
| Transfer time | 267.7 s (about 65 KB/s) |

The WAV frame and data-byte counts equal the recorder's PASS accounting.

| Channel | Range | Peak | RMS | Clipped |
| --- | ---: | ---: | ---: | ---: |
| Radio left | -1443..1472 | 1472 | 332.42 | 0 |
| Radio right | -1445..1470 | 1470 | 332.47 | 0 |
| Microphone | -1364..1295 | 1364 | 279.97 | 0 |

Observations, not gates: radio left/right correlation was 0.999973 (`mono_like`).
Zero-lag radio/microphone correlation was about -0.0006, as expected for a delayed
acoustic copy.

## Loopback alignment

12 of 12 windows were detected. Lag was 278 frames (5.79 ms) in every window, with zero
spread and consistent positive polarity. `drift_reliable=true` with 0 frames of drift
over the file. The radio and microphone tracks stayed continuous for the whole
recording. The 278-frame startup offset is within the 172–549 frame range measured on
REC009–REC015 (`full_spooky_proto-hpq.1`).

## Health, IPC and SD

- DIAG after transfer: `RADIO_OVR`, `PDM_OVR`, `SD_ERR`, `AUDIO_ERR` and
  `HAS_FAULT` all 0. Deltas since boot: `COUNT` +125, `OVERWRITTEN` +581,
  `SD_MAX_MS` 26.
- LOG after transfer: every loss and error counter 0, `PEAK` 1377, `TX_BYTES`
  +1414.
- IPC `LINK=UP` with forward progress at every sample: boot +62 TX, recording
  +255/+188, after recording +227, after transfer +2997 TX.
- SD: same card identity. Free space dropped by 16 MiB for the 16.5 MiB file, so no
  other file was removed or rewritten. `REC018.WAV` remains on the card.

## Findings

WAV retrieval dominates the run time. The acknowledged stop-and-wait transfer uses
frames of about 1 KiB and runs at about 65 KB/s, matching the REC016/REC017
retrievals (about 2,600 s each). The original default deadline would have stopped a
600 s regression mid-transfer. It is now 450 s + 7 × duration (4,650 s at 600 s),
covered by a host test. Faster transfer is tracked separately in Beads.

## Human checks

The operator listened to `REC018.WAV` after retrieval. It sounded like audio: radio
static, as expected with no valid station at 99.1 MHz. This subjective check is recorded
separately from the automated channel statistics above.

Constitution check at handoff: Principles I, II, IV, V and VI were touched. No
departure is recorded.
