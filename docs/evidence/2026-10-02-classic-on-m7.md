# Classic on the M7

Date: 2026-10-02 (local, UTC-6; Spooky Bench timestamps are UTC, 2026-10-03)  
Beads issue: `full_spooky_proto-54w.33`

This session ran the Classic scan engine on the NUCLEO-H755ZI-Q bench
([spec 001](../../spec/specs/001-classic-scan-engine/spec.md),
[decision 0016](../decisions/0016-classic-scan-motion.md)). The operator confirmed that
the board was connected and free with the radio and microphone path installed. The SD
card was out at first and reinserted mid-session (below). Spooky Bench 0.7.0 ran on
Windows with target CDC on COM3 and Spookyprobe on COM6. Physical controls are not
wired in product images, so Classic was driven through the USB CLI, which issues the
same commands as C-103 to C-106 and C-110 ([USB CLI](../design/usb-cli.md#classic-scan-engine)).

## Image

| Preset | Manifest | Build ID | CM7 SHA-256 | CM4 SHA-256 |
| --- | --- | --- | --- | --- |
| IpcSmoke | `build/bench-54w33-classic-ipcsmoke-20261002a.json` | `54w33-classic-ipcsmoke-20261002a` | `5476488aecafea955ffb645cf495263eb928aa6b2ee7987377992cd7384c46bb` | `7d9d1116cb20cd8367de3d86a2ec8f067b1cf0fa0ea9220279c6cf9492489584` |

Built from clean source `2c74f5733f0e256952667952d349b9c1a48e3898` (branch
`claude/54w33-classic-m7`) with GNU Tools for STM32 14.3.1+st.2. Debug, Release,
IpcSmoke, IpcMismatch and RadioTuneQual also built; the only warnings were the existing
unused CubeMX `MX_*_Init` functions. Native host tests passed 27/27 (MSVC 14.29). Host
tests are not hardware evidence. Flash: run `2026-10-03T015153.685306_0000-7020bbc0`,
pass.

## Startup (spec SC-001, log side)

Four boots after `reset` (runs `...015301.487689...`, `...015321.097784...`,
`...015325.873997...`, `...015330.655667...`). Every boot logged the same sequence
with no input:

```text
[classic] t=689 pos=1:10192 RUN_STATE STATE=UNABLE REASON=RADIO_STOPPED DIR=UP RATE=120/120 LIMITED=0 DIST=1 EDGE=WRAP BAND=FM FREQ=99100
...
[classic] t=697 pos=1:10564 RUN_STATE STATE=RUNNING REASON=NONE DIR=UP RATE=120/120 LIMITED=0 DIST=1 EDGE=WRAP BAND=FM FREQ=99100
[radio] tuned FM 99200 kHz (99.200 MHz): RSSI=13 dBuV SNR=0 dB valid=0
[radio] tuned FM 99300 kHz (99.300 MHz): RSSI=13 dBuV SNR=1 dB valid=0
```

The first pass publishes every fact; Classic is unable to scan for 8 ms until the
Radio machine reports it has started, then runs. The listening trial below is the
audible side: the user heard Classic scanning FM at the default settings.

## Classic over the CLI

An ad hoc pyserial script (not part of Spooky Bench) drove the CLI while Spooky Bench
captured the console (run `2026-10-03T015413.024475_0000-ef549503`). Raw exchanges:
`build/classic-cli-54w33-20261002a.jsonl` (untracked, local `build/` directory).

| Check | Result |
| --- | --- |
| Rate 120 per minute, FM | 61 jumps in a 30 s window (expected 60) |
| Rate 400, FM | 99 jumps in 15 s (expected 100) |
| Setting 400 on AM | Runs at 200, `LIMITED=1`; 50 jumps in 15 s (expected 50); distance 10 kHz |
| `CLASSIC PAUSE` | Frequency and jump count unchanged for 3 s |
| `CLASSIC RUN` | Jumped at once (102.8 to 102.9 MHz within 0.2 s) |
| CLI `TUNE 95000` while running | Classic continued upward from 95.0 MHz |
| Bounce at 108.0 MHz | Reversed; `DIRECTION` event `DIR=DOWN` |
| Stop at 108.0 MHz | `SWEEP_COMPLETE`; `CLASSIC RUN` started a new sweep at 87.5 MHz |
| FM to AM to FM | AM's distance and rate limit applied; FM resumed with its own |
| Tune commands | `REFUSED=0 FAILED=0` throughout |

Each change produced one `[classic]` log line with the tick and the radio sample
position: rate changes, pause and resume, edge changes, the bounce reversal, sweep
complete, and the band change's distance and rate events.

The first attempt at the session check answered `ERR RECORD no SD card detected`: the
card had been left out. The operator reinserted it, `SD STATUS` reported it mounted,
and the session checks below were run then.

## Unable to scan during a session (spec FR-027, SC-007)

`RECORD START 10` with Classic running (runs `2026-10-03T020219.782766_0000-eba74aa8`
and `2026-10-03T020342.278444_0000-90c72c49`; raw exchanges in
`build/classic-session-54w33-20261002a.jsonl` and `...b.jsonl`):

- `CLASSIC` reported `STATE=UNABLE REASON=SESSION` from Preparing through the end of
  the recording, sampled every second. The jump count stayed at 1145 (first run) and the
  frequency at 103.4 MHz before and after the second run: no tune was issued or retried.
- The log has `RUN_STATE STATE=UNABLE REASON=SESSION` when the session entered
  Preparing and `RUN_STATE STATE=RUNNING` when it returned to Idle. The first run's
  capture began about 2 s late and missed the first event; `LOG STATUS` showed
  `DROP_WRITES=0`, so nothing was lost on the target.
- Scanning resumed after the session. Both recordings passed: `REC101.WAV` and
  `REC102.WAV`, 483,328 frames each, finalized.

This shows the recording guard of decision 0003 still in force on the default build.
Scanning during a session is `full_spooky_proto-54w.6` and spec SC-006
(`full_spooky_proto-54w.10`).

## Recording regression

One `test recording-regression --manifest build/bench-54w33-classic-ipcsmoke-20261002a.json --seconds 60 --stimulus ambient`
run, `2026-10-03T020443.121995_0000-4149284a`: **pass**, all six stages.

- Target reported build `54w33-classic-ipcsmoke-20261002a`, boot epoch 18. Classic was
  scanning when the run began (radio at FM 107.9 MHz at the prerequisites check).
- `REC103.WAV`: 2,883,584 frames, 17,301,504 data bytes, 60.074 s audio, 60,123 ms
  elapsed. Transfer CRC32 `1ba18002`, SHA-256
  `7a1a45d613330020b4c642cd39b1911f3635f74cee88e19ba93e2c764c9fc24b`.
- Radio and PDM queue high-water 1/8; zero radio/PDM overrun, SD, audio, fault,
  logger-loss and IPC error counters. Maximum SD write 37 ms.
- Radio channels mono-like (correlation 0.99997); radio/microphone correlation 0.0018;
  no clipped samples.

`DIAG LATENCY` afterwards, recording-only maxima (`build/diag-latency-54w33-20261002a.txt`):
loop 43 ms (budget 75), recorder 37 ms (70), Classic 1 ms (10), every other service at
most 2 ms, zero violations. Before the session `DIAG STATUS` reported
`LOOP_MAX_MS=326` outside recording, from the synchronous band switches of the CLI
check (`full_spooky_proto-54w.12`).

No listening check was made of the WAV; signal statistics are not an audio-quality
verdict.

## Listening trial (decision 0016 item 23)

The user listened on headphones while Claude set each configuration over the CLI.
Judgments are the user's.

| Setting | Judgment |
| --- | --- |
| FM, defaults: 120 per minute, 1 channel, wrap | "Could be a smidge too fast"; nothing wrong or unexpected, just the normal gaps between jumps |
| FM, 100 per minute | Briefly preferred as the default, then: 120 "is actually still a good default"; faster than expected at first, "but it works" |
| FM, 60 | Not too slow to be interesting |
| FM, 240 | Still interesting; fragments pop out |
| FM, 400 | "A good ceiling" |
| FM, 5 channels at 120 | Interesting; "doesn't feel predictable in any way" |
| FM, 20 and 70 channels at 120 | Interesting. The distance control is "a good way to tune randomness": walks get more or less predictable depending on how evenly the step divides the band |
| AM, defaults: 120, 1 channel (10 kHz) | Works fine |
| AM, 200 (maximum; setting 240, limited) | "Might be ok"; occasional short landings |
| AM, 180 | Feels better; the user chose 180 as AM's maximum |
| SW | Skipped: no reception at the basement bench location (RSSI 0) |
| LW | All noise, with some variation in its character; needs hands-on time with the knobs to judge |

Outcomes: the 120 default and FM's 400 maximum stand; AM's maximum should be 180
([decision 0018](../decisions/0018-classic-am-lw-maximum-rate.md), accepted the same
day; the AM checks above ran with the earlier maximum of 200). SW and LW are not
judged.

## Not done

- **SC-009** (the user predicts the next few landings and says what each control does)
  needs physical controls and is deferred. The trial's remark that a 5-channel FM walk
  "doesn't feel predictable" is input to that judgment, not a verdict on it.
- SW and LW listening, and the feel of the tables in the hand.
- Scanning during a recording (SC-006) waits for `full_spooky_proto-54w.6`.
