# Recorder SD write latency and split conversion

Date: 2026-09-30  
Beads issue: `full_spooky_proto-8lw.22`

This session measured recorder `f_write` latency over long recordings and verified a
change that moves block conversion out of the pass that carries the write. It
follows the fully attributed `RECORDER 71/70 ms` violation (one 58 ms write) in
[the loop-budget boundary session](2026-09-30-loop-budget-boundary.md). The board
was connected and free, continuing the operator-confirmed bench session of the same
day. Spooky Bench 0.7.0 ran on Windows with target CDC on COM3 and Spookyprobe on
COM6. Media and stimulus were unchanged from earlier sessions that day (the same SD
card, ambient stimulus).

## Instrumentation

`RECORD LATENCY` (documented in [the USB CLI](../design/usb-cli.md)) reports, for the
current or last recording, the number of block writes, the longest `f_write`, the
longest block conversion, and an `f_write` histogram in 10 ms bins (0-9 ms through
60-69 ms, then 70 ms and above). The replies below were read with one manual request
each on COM3 after the recording completed. The same script also sent a bare `DIAG`
request, which the target rejected with a usage error; that was a script mistake and
had no other effect.

## Images

Both pairs were built from `c4ab94c7e5911de5d1f4e790c5b6befe8d14dcc5` plus
uncommitted changes, with GNU Tools for STM32 14.3.1. IpcSmoke inherits the Debug
preset (`-O0`), and the M7 instruction and data caches are disabled in every image.

| Build ID | Change | Manifest | Snapshot SHA-256 | CM7 SHA-256 |
| --- | --- | --- | --- | --- |
| `8lw22-writehist-ipcsmoke-20260930a` | `RECORD LATENCY` only | `build/bench-8lw22-ipcsmoke-20260930a.json` | `9cef8fef6d29fedd43a80a890540118691d6628aafc49450810a13e632c25fe0` | `1b9e2101268a159254b8c0a4af03fad357b148251d11e433a36684a438cf08fa` |
| `8lw22-splitwrite-ipcsmoke-20260930b` | plus split conversion/write | `build/bench-8lw22-ipcsmoke-20260930b.json` | `2ee436fe7d70fdf05c1edb09931c987abb3f05043059cb5cf4d30de826b458d1` | `7c2e19b548c77a5ee91a92553e00599eacc1d7176bb929437d87f787cb378c9c` |

CM4 SHA-256 was `7d9d1116cb20cd8367de3d86a2ec8f067b1cf0fa0ea9220279c6cf9492489584` in
both. Debug and Release also built for each change; the only warnings were the
existing unused CubeMX `MX_*_Init` functions.

## Baseline distribution

Boot smoke `2026-09-30T173155.269679_0000-1604681a` flashed the first pair and passed.
`test ipc-load --seconds 600` run `2026-09-30T173244.438297_0000-40d725e5` passed:
`REC047.WAV`, 28,803,072 frames, 600.064 s, queue high-water 1/8, zero overrun, SD
and audio errors, IPC `UP` throughout.

```text
OK RECORD LATENCY writes=7032 write-max=52ms convert-max=14ms write-hist-10ms=0,6616,411,2,0,3,0,0
DIAG LATENCY ID=0 SERVICE=LOOP BUDGET_MS=75 MAX_MS=72 VIOLATIONS=0
DIAG LATENCY ID=3 SERVICE=RECORDER BUDGET_MS=70 MAX_MS=65 VIOLATIONS=0
```

94.1% of writes took 10-19 ms and 5.8% took 20-29 ms; five of 7,032 took 30 ms or
more, three of them 50-59 ms, and none reached 60 ms. Conversion of each
4096-frame block took up to 14 ms in the unoptimized, uncached image, and it ran in
the same pass as the write, so the recorder service peaked at write plus about
13 ms. With a 70 ms budget, any write of 57 ms or more violated it, which accounts
for the earlier 58 ms case.

## Split conversion and write

The recorder now converts one queued block into the output buffer and releases the
queue entries in one pass, and writes that buffer with a single `f_write` in the
next. A block is counted in `frames_written` only after its write; an abort between
the two passes drops the pending converted block, as it did not reach the file.
[Foreground latency](../design/foreground-latency.md) records the change. The 70 ms
recorder budget and 75 ms loop budget are unchanged.

The recording regression `2026-09-30T174526.080912_0000-8a804330` (60 s) passed every
stage: boot smoke, prerequisites, recording, CRC-verified WAV retrieval, accounting
and post-transfer health. `REC048.WAV` contained 2,883,584 frames / 17,301,504 data
bytes / 60.074 s; transfer CRC32 `6331cd12`, SHA-256
`e0f625213a7bdc694739be2cd4da4198ea052c26b3218cccd22b43191f5727dc`. Queue
high-water was 1/8 and all error and loss counters were zero.

```text
OK RECORD LATENCY writes=704 write-max=24ms convert-max=14ms write-hist-10ms=0,699,5,0,0,0,0,0
DIAG LATENCY ID=0 SERVICE=LOOP BUDGET_MS=75 MAX_MS=30 VIOLATIONS=0
DIAG LATENCY ID=3 SERVICE=RECORDER BUDGET_MS=70 MAX_MS=24 VIOLATIONS=0
```

The 600 s run `2026-09-30T175310.698206_0000-64d62280` passed: `REC049.WAV`,
28,803,072 frames, 600.064 s, queue high-water 1/8, zero overrun, SD and audio
errors, IPC `UP` throughout.

```text
OK RECORD LATENCY writes=7032 write-max=56ms convert-max=15ms write-hist-10ms=0,6944,83,1,0,4,0,0
DIAG LATENCY ID=0 SERVICE=LOOP BUDGET_MS=75 MAX_MS=62 VIOLATIONS=0
DIAG LATENCY ID=3 SERVICE=RECORDER BUDGET_MS=70 MAX_MS=56 VIOLATIONS=0
```

The recorder maximum now equals the write maximum. A 56 ms write, which would have
put the old combined pass at about 70 ms, left 14 ms of recorder headroom and 13 ms
of loop headroom. Every other service stayed at 3 ms or less.

## Interpretation and limits

Across the two 600 s recordings (14,064 writes) the slowest write was 56 ms and none
reached 60 ms; with the earlier 58 ms write, the observed tail on this card is under
60 ms. The split gives the write pass the full 70 ms budget, so a write up to about
69 ms now passes. This is one card, one stimulus and one day's data; a slower card
or longer recording can produce a longer write. Such a write remains visible as a
recorder budget violation, and `RECORD LATENCY` is the measurement to collect for
each slow-card qualification. Enabling the M7 instruction cache or building the
evidence images with optimization would shorten conversion further; neither was
changed here.

The board ended the session running the `8lw22-splitwrite-ipcsmoke-20260930b`
IpcSmoke pair with no retained fault.

Constitution check at handoff: Principles I, V and VI were touched. Recording
reliability improved without weakening any budget or relying on queue headroom,
dirty image provenance was preserved, and the measurement's single-card scope is
stated. No departure is recorded.
