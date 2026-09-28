# Session shadow machine: recording regression and CLI paths (REC019 to REC021)

Date: 2026-09-28  
Beads issue: `full_spooky_proto-8lw.14`

First hardware run of the Session StateSmith machine in shadow mode beside the
recorder ([decision 0006](../decisions/0006-statesmith-behavior-model.md) item 18),
with the M7 event queue of [decision 0007](../decisions/0007-m7-event-queue.md) wired
into the foreground loop. The recorder keeps authority. The machine is fed its reports
through the queue and must agree with `RadioRecorder_IsActive()` after every drain; a
disagreement is recorded as a `SESSION_MISMATCH` diagnostic fault.

## Image and setup

| Item | Value |
| --- | --- |
| Manifest | `build/bench-8lw14-ipcsmoke-20260928a.json`, preset `IpcSmoke` |
| Source revision | `c8a65aee816d1fe4ec29057cf1107b9b027b3645` plus uncommitted changes, snapshot `build/8lw14-shadow-source-20260928a.patch` (SHA-256 `f1e9f99312c9c5bfe780be44fa813c2cfa5e8e1bde836944629f8935f74f5c97`) |
| CM7 SHA-256 | `cc274e3541d343712c4a0252b379cc40b8d473447e023221a1180a7d17f3a27c` |
| CM4 SHA-256 | `8f6e005ffefdf6896fcaf85d5a51554aa64e97ecae3672b412a94ed29ba377cc`, unchanged from the [REC018 run](2026-09-26-recording-regression-live-rec018.md) |
| Stimulus | Ambient: radio static on FM 99,100 kHz (RSSI 13 dBµV, `VALID=0`); microphone not driven |
| Volume | ADC 57106, 87%, attenuation 7.0 dB, not muted |
| SD | SDHC/SDXC, 59,344 MiB, 58,674 MiB free before |
| Bench power | Not recorded for this session |

## Unattended recording regression

```powershell
host/.venv/Scripts/python.exe -B -m spookybench --json --profile host/bench.local.json test recording-regression --manifest build/bench-8lw14-ipcsmoke-20260928a.json --seconds 60
```

Run directory: `%LOCALAPPDATA%/SpookyBench/runs/2026-09-28T141443.692760_0000-a8677a03`.
Result: `pass`, `human_required=false`, 447 s.

| Stage | Result | Duration |
| --- | --- | ---: |
| `boot_smoke` | pass | 42.1 s |
| `prerequisites` | pass | 5.6 s |
| `recording` | pass | 109.0 s |
| `wav_inspect` | pass | 267.8 s |
| `accounting` | pass | 0 s |
| `post_transfer_health` | pass | 21.5 s |

| Evidence | Value |
| --- | ---: |
| File | `REC019.WAV` |
| Frames / data bytes | 2,883,584 / 17,301,504 |
| Audio / elapsed | 60.074 s / 60.114 s |
| Queue high-water | radio 1/8, PDM 1/8 |
| Max SD write | 43 ms |
| Transfer | 17,301,548 bytes, CRC32 `d9ec225c` |
| SHA-256 | `dcdb5bae2be921b8a1657226e27cdb3ac837455d8bcb682c9aa1df0731a2dcfd` |
| Final `DIAG STATUS` | `HAS_FAULT=0`, `SD_MAX_MS=43`, `LOOP_MAX_MS=154`, all error counters 0 |
| Log | no drops, no transport loss or errors |

The WAV frame and data-byte counts equal the recorder's PASS values. The radio channels
have no clipped samples, and left/right correlation was 0.99997 (`mono_like`), an
observation only. The microphone was not driven (RMS 2.7), as in the ambient
[ten-minute baseline](2026-09-26-ten-minute-recording-baseline.md). The foreground loop
gap of 154 ms is within the 72 to 288 ms recorded by the ten-minute baselines. The
listening check was not performed.

## CLI paths the regression does not reach

Immediately after the regression, without reflashing, a bounded serial script sent the
commands below on the target CDC while `spookybench console --seconds 60` captured the
UART (run directory `%LOCALAPPDATA%/SpookyBench/runs/2026-09-28T142246.195827_0000-193e74a3`).
Command replies are in `build/8lw14-paths-20260928a.json`.

| Command | Reply | Shadow log |
| --- | --- | --- |
| `RECORD START 0` | `ERR RECORD duration must be 1..3600 seconds` | Sent before the console capture started; not captured |
| `RECORD STOP` (idle) | `OK RECORD already idle` | `StopIgnored` |
| `RECORD START 30` | `OK RECORD START file=REC020.WAV` | `RecordingStarted`, `state=Recording` |
| `RECORD START 5` (active) | `ERR RECORD already active` | `RecordingRejected` |
| `RECORD STOP` | `OK RECORD STOP requested`, then `OK RECORD PASS file=REC020.WAV frames=90112` | `RecordingStopping`, `state=Finalizing`, `RecordingCompleted`, `state=Idle` |
| `RECORD STOP` (again) | `OK RECORD already idle` | `StopIgnored` |
| `RECORD START 2` | `OK RECORD START file=REC021.WAV`, then `OK RECORD PASS frames=98304` | `RecordingStarted`, `state=Recording`, `RecordingCompleted`, `state=Idle` |

After the script:

```text
OK DIAG V=1 CORE=7 COUNT=128 OVERWRITTEN=635 SD_MAX_MS=43 LOOP_MAX_MS=154 RADIO_OVR=0 PDM_OVR=0 SD_ERR=0 AUDIO_ERR=0 HAS_FAULT=0
OK DIAG QUEUE CAP=32 RESERVE=8 COUNT=0 PEAK=2 POSTED=758 DISPATCHED=758 REJ_INPUT=0 REJ_CMD=0 REJ_INTERNAL=0 RECONCILES=0 MAX_WAIT_MS=10
```

`DIAG DUMP` (sequences 636 to 763, covering the script) contained no
`SESSION_MISMATCH` or `EVENT_QUEUE_LOSS` event, and no gaps.

## Limits

- The second `RECORD STOP` arrived after the stop had already finished at the next
  block, about 85 ms later, so it exercised stop-while-idle, not a repeated stop
  during Finalizing. That transition, start failures after the card is opened, and
  capture faults are covered by host tests only.
- `REC020.WAV` and `REC021.WAV` were left on the card and were not retrieved or
  inspected.
- Diagnostic history had wrapped, so the dump shows only its last 128 events. The
  latest fault is kept separately from the history, so `HAS_FAULT=0` covers the whole
  boot, including the regression's recording: no `SESSION_MISMATCH` fault was recorded.
