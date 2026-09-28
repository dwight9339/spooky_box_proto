# Session authority and recording-safe command policy (REC022 to REC027)

Date: 2026-09-28  
Beads issue: `full_spooky_proto-8lw.4`

First hardware run after the Session StateSmith machine took authority over the
recorder and the CLI began using the shared recording-safe command policy from
[decision 0008](../decisions/0008-recording-safe-command-policy.md). This is Slice A:
the existing timed CLI recorder, current command surface and Session authority. It
does not qualify open-ended sessions, card-full reserve handling or the 4 GiB ending.

## Image and setup

| Item | Value |
| --- | --- |
| Manifest | `build/full_spooky_proto-8lw4-slice-a.json`, preset `IpcSmoke` |
| Source revision | `dc3c6a242d3008e9b28d17236df946c849ca6ce0` plus uncommitted changes, snapshot `build/full_spooky_proto-8lw4-slice-a.patch` (SHA-256 `95b8a70cc0554165a662437874a2b4ed2e02800e20d82e5661090d8093be2783`) |
| CM7 SHA-256 | `09d0b17c9b8904cc0f770dc1dab3cf21cd32e492e30da9c3eccf64e71a172580` |
| CM4 SHA-256 | `e1a6e7533ddf36ce13c3633ff8abd7dd87a983c621b7706d6d86ff6f40cf31ca` |
| Compiler | GNU Tools for STM32 14.3.1, `-g`, `SPOOKY_IPC_SMOKE=ON` |
| Stimulus | Ambient: radio static on FM 99,100 kHz (`VALID=0`); microphone not driven |
| Volume | ADC 57102, 87%, attenuation 7.0 dB, not muted |
| SD | SDHC/SDXC, 59,344 MiB, 58,656 MiB free before the unattended run |
| Bench power | Not recorded for this session |

The canonical Ninja 1.13.2 IpcSmoke build hung before launching a compiler, as tracked
in `full_spooky_proto-8lw.15`. The same preset-equivalent tree built both cores with
NMake before this run.

## Unattended recording regression

```powershell
host/.venv/Scripts/python.exe -B -m spookybench --json --profile host/bench.local.json test recording-regression --manifest build/full_spooky_proto-8lw4-slice-a.json --seconds 60
```

Run directory: `%LOCALAPPDATA%/SpookyBench/runs/2026-09-28T194831.008114_0000-9cee82ee`.
Result: `pass`, `human_required=false`, 447.8 s.

| Stage | Result | Duration |
| --- | --- | ---: |
| `boot_smoke` | pass | 42.2 s |
| `prerequisites` | pass | 5.6 s |
| `recording` | pass | 108.9 s |
| `wav_inspect` | pass | 267.8 s |
| `accounting` | pass | 0 s |
| `post_transfer_health` | pass | 21.5 s |

| Evidence | Value |
| --- | ---: |
| File | `REC022.WAV` |
| Frames / data bytes | 2,883,584 / 17,301,504 |
| Audio / elapsed | 60.074 s / 60.111 s |
| Queue high-water | radio 1/8, PDM 1/8 |
| Max SD write | 51 ms |
| Transfer | 17,301,548 bytes, CRC32 `250b15e1` |
| WAV SHA-256 | `de458ff9e381c2047bc7d58c0cad61356ec8ea421845396e416810af2ae78856` |
| Final health | `HAS_FAULT=0`; all radio, PDM, SD and audio error counters 0 |
| Logging | no dropped writes, bytes, transport data or errors |

The WAV frame and data-byte counts equal the recorder's PASS values. All three channels
were nonconstant and had no clipped samples. Radio left/right correlation was 0.99997
(`mono_like`), an observation only. The microphone was not driven (RMS 2.1). The
listening check was not performed.

## CLI command-policy paths

A bounded pyserial script exercised the target CDC after the regression. The first
attempt used a 20-second session while allowing two seconds per command. `REC023.WAV`
therefore completed before the final `TUNE`, `SD STATUS` and stop commands; those
commands correctly ran while idle, but the harness reported them as failures. That
attempt is partial evidence only. It also created a clean timed `REC024.WAV`. Its UART
capture is `%LOCALAPPDATA%/SpookyBench/runs/2026-09-28T195623.847713_0000-fe29c42b`
(1,330 bytes, no host drops) and shows authoritative `Recording` and `Idle` state
transitions with no mismatch line.

The corrected sweep used a 60-second session and completed every active-session command
before stopping it as `REC025.WAV`. It then ran a ten-second timed `REC026.WAV`.
CDC replies are preserved in `build/full_spooky_proto-8lw4-cli-transcript-pass.txt`.

| Command or check | Observed result |
| --- | --- |
| `RECORD STOP` while idle | `OK RECORD already idle` |
| `RECORD START 60` | `OK RECORD START file=REC025.WAV` |
| `RECORD START 10` while active | `ERR RECORD already active` |
| `SLEEP START` | `ERR SLEEP unavailable while recording` |
| `SD REINIT` | `ERR SD unavailable while recording` |
| `WAV FETCH REC022.WAV` | `ERR WAV unavailable while recording` |
| `EMF ZERO` | `ERR EMF unavailable while recording` |
| `UI LEDS`, `UI MATRIX ANIMATE`, `UI DISPLAY TEST 2` | `ERR UI unavailable while recording` for each |
| `BAND AM`, `TUNE 100000` | `ERR RADIO tuning disabled while recording` for each |
| `SD STATUS` while active | Cached `OK SD STATUS CARD=PRESENT OWNER=RECORDER FILE=REC025.WAV FRAMES=454656` |
| `RECORD STOP` | Stop acknowledged; `REC025.WAV` passed with 540,672 frames and 11.264 s audio |
| `RECORD START 10` | `REC026.WAV` passed with 483,328 frames and 10.069 s audio |
| Queue after the sweep | Capacity 32, reserve 8, peak 2, no rejected input, command or internal events, maximum wait 10 ms |

The corrected script initially expected `DIAG LAST` to return the most recent policy
event. The command actually reports the most recent *fault*, so `OK DIAG LAST NONE` was
the correct response. This was a harness assertion error, not a firmware failure.

A focused follow-up (`REC027.WAV`) rejected `UI LEDS`, immediately dumped diagnostics,
and then stopped cleanly. Sequence 1355 contained
`EVENT=COMMAND_REJECTED A=17 B=1`: semantic action 17 (`UI_TEST_PATTERN`) in Session
state 1 (`Recording`). The 128-event dump ended with `GAPS=0` and contained no
`SESSION_MISMATCH` or `EVENT_QUEUE_LOSS`. Its CDC transcript is
`build/full_spooky_proto-8lw4-command-diag-transcript.txt`.

## Final target state and limits

The final read-only diagnostic run is
`%LOCALAPPDATA%/SpookyBench/runs/2026-09-28T200829.902591_0000-50663ee7`:
`HAS_FAULT=0`, `SD_MAX_MS=51`, `LOOP_MAX_MS=70`, with every radio, PDM, SD and audio
error counter zero. The recorder was idle after the clean `REC027.WAV` stop.

- `REC023.WAV` through `REC027.WAV` remain on the card and were not retrieved or
  listened to. Only `REC022.WAV` received the full transfer and WAV inspection.
- A repeated stop during the short Finalizing state and start/open/capture failures are
  covered by host tests, not this bench session.
- Physical-control routing is not yet present on this prototype surface. The policy
  table is host-tested for those semantic actions, but this bench session exercises
  only the current CLI surface.
- Open-ended recording, card-full reserve behavior and the 4 GiB ending belong to
  Slice B and are not claimed by this evidence.
