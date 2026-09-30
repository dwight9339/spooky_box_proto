# Recording loop-budget boundary fix

Date: 2026-09-30  
Beads issues: `full_spooky_proto-8lw.21`; follow-up `full_spooky_proto-8lw.22`

This session verified a fix for the intermittent aggregate-loop budget violation
seen in [the UI service split qualification](2026-09-30-ui-service-split.md)
(`FOREGROUND_BUDGET A=0 B=114`) and in [the IPC cold start](2026-09-30-ipc-cold-start.md)
(`A=0 B=127`). The board was connected and free, continuing the operator-confirmed
bench session of the same day. Spooky Bench 0.7.0 ran on Windows with target CDC on
COM3 and Spookyprobe on COM6.

## Cause

`docs/design/foreground-latency.md` includes a loop sample only when DMA capture is
active at both boundaries, which excludes file-open and start. The main loop instead
paired the capture state at the current loop start with the state latched at the
**end** of the previous pass, after the dispatch service had already run
`RECORD START`. The pass that ran `RadioRecorder_OpenFile` (`f_open` plus a
contiguous `f_expand` preallocation) and then enabled capture was therefore charged
as a recording pass, although no audio block can queue before `capture_enabled`.

The cold-start run shows the timing directly: `RECORD_START` at `MS=158160` and the
127 ms violation at `MS=158167`, so about 120 ms of that pass preceded capture.
`DIAG LATENCY` attributed at most 48 ms to measured services. The 114 ms case's
event history had wrapped, so its timing cannot be confirmed; the same run showed a
103 ms `LOOP_STALL` before recording, so start-path work of that size is normal.

The fix latches the capture state at each loop start in `CM7/Core/Src/main.c`. The
75 ms budget, the per-service budgets and the unconditional `LOOP_STALL` (>=50 ms)
record are unchanged. A pass in which capture starts is still covered by the
per-service measurements of the services that run after capture begins.

## Image

| Preset | Manifest | Build ID | CM7 SHA-256 | CM4 SHA-256 |
| --- | --- | --- | --- | --- |
| IpcSmoke | `build/bench-8lw21-ipcsmoke-20260930a.json` | `8lw21-loopboundary-ipcsmoke-20260930a` | `0f470e9e9fcab2878aa57dd5eba3b8f28fa4d6e6a51cd77f8699bd7e7e79d587` | `7d9d1116cb20cd8367de3d86a2ec8f067b1cf0fa0ea9220279c6cf9492489584` |

Built from `c4ab94c7e5911de5d1f4e790c5b6befe8d14dcc5` plus the uncommitted fix,
archived as `build/8lw21-source-20260930a.patch` (SHA-256
`2cd3aed3253414a3393d02f023c6768190a09a77c1a543ebc8d1339a92222d6d`), with GNU Tools
for STM32 14.3.1. Debug and Release also built; the only warnings were the existing
unused CubeMX `MX_*_Init` functions. Native host tests passed 13/13 (they do not
cover `main.c`).

## Recording regressions

Three consecutive `test recording-regression --seconds 60` runs, each reflashing and
resetting the pair (boot epochs 2, 3, 4; reset flags `0x01460000`):

| Run | File | Verdict | Max SD write | `LOOP_MAX_MS` | `HAS_FAULT` | Transfer CRC32 |
| --- | --- | --- | ---: | ---: | ---: | --- |
| `2026-09-30T165146.098795_0000-a6a28390` | `REC044.WAV` | pass | 50 ms | 73 | 0 | `a122108a` |
| `2026-09-30T165923.933574_0000-d4ca401e` | `REC045.WAV` | pass | 24 ms | 81 | 0 | `91bac26d` |
| `2026-09-30T170701.864267_0000-50e1e823` | `REC046.WAV` | **fail** `diag_unhealthy` | 58 ms | 77 | 1 | not transferred |

Every run completed 2,883,584 frames / 17,301,504 data bytes / 60.074 s, with radio
and PDM queue high-water 1/8 and zero radio/PDM overrun, SD, audio, logger-loss and
IPC error counters. IPC stayed `LINK=UP` at every sample. Passing runs' SHA-256:
`REC044` `0cd321d92f73691978bd3079d241c960ae1df9a0a0aa8ff037907fb68ef86e3b`, `REC045`
`8263964c8666dc445921e977da2e7bacab7e4fed6fd8be4062deec8cea408846`. No listening
check was made; signal statistics are not an audio-quality verdict.

`LOOP_MAX_MS` is the unconditional `LOOP_STALL` maximum and includes non-recording
passes. Run 2 observed an 81 ms pass with no budget violation, so that pass was not
a recording pass under the corrected boundary; its history had wrapped, so it cannot
be confirmed to be the start pass.

### Run 3 failure

Run 3 is a different, fully attributed violation. `DIAG LAST` (run
`2026-09-30T170948.789966_0000-55c0667a`) retained
`FOREGROUND_BUDGET A=0 B=77` at `MS=116607`, about 45 to 50 s into the recording,
where progress lines show maximum write rising from 23 ms to 58 ms. A manual
`DIAG LATENCY` afterwards reported:

```text
DIAG LATENCY ID=0 SERVICE=LOOP BUDGET_MS=75 MAX_MS=77 VIOLATIONS=1
DIAG LATENCY ID=3 SERVICE=RECORDER BUDGET_MS=70 MAX_MS=71 VIOLATIONS=1
```

with every other service at 2 ms or less. One 58 ms SD write plus conversion pushed
the recorder service over its 70 ms budget and the pass to 77 ms. This is the
slow-card case the design already anticipates and is not a measurement gap. It is
retained as a failure and tracked by `full_spooky_proto-8lw.22`.

The board ended the session running the `8lw21-loopboundary-ipcsmoke-20260930a`
IpcSmoke pair with that fault retained.

Constitution check at handoff: Principles I, V and VI were touched. The change
aligns the implementation with the documented measurement boundary without
weakening any bound, preserves provenance for a dirty image, and reports the run 3
failure as a failure. No departure is recorded.
