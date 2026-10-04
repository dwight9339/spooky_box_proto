# Recorder reply queue under USB CDC backpressure

Date: 2026-10-01
Beads issue: `full_spooky_proto-jjy.10`

This session repeats the jjy.6 USB backpressure case (host port closed for a whole
recording, see [logger saturation](2026-10-01-logger-saturation.md#usb-cdc-backpressure))
on an image with the new recorder reply queue policy. That policy keeps only the
newest progress line, and a reply that finds the queue full evicts a queued progress
line before it evicts the oldest reply.

## Setup and provenance

| Item | Value |
| --- | --- |
| Source revision | `48786e1` plus the jjy.10 change (`CM7/App/reply_queue.[ch]`, `CM7/Core/Src/radio_recorder.c`, `CM7/CMakeLists.txt`, `tests/`) |
| Bench image | `build/bench-jjy10-replyq-20261001a.json`, build ID `jjy10-replyq-20261001a`, Debug preset; patch `build/jjy10-replyq-20261001a.patch` (SHA-256 `38d9383488e606547e1edbe8b8c787313bbaeff063e19766de61044bfcce41c7`) |
| CM7 / CM4 SHA-256 | `9b9a352ffd0576c9fa4e0b409d3a20d7d9025dff86715c0994d0016eacdd3fc3` / `d5ab1687e85e3d5a975b5e5c98bdfa8ca445b7aa8bbabc11c6dd008f94a91809` |
| Flash | Spooky Bench `flash`, pass, both cores verified (run `2026-10-01T193008.189953_0000-31627685`); `DIAG IDENTITY` reported `BUILD=jjy10-replyq-20261001a` |
| Compiler | GNU Tools for STM32 14.3.1; Release pair also built |
| Committed source | Differs from the bench patch only in comments in `reply_queue.h` and the docs |
| Board | Nucleo prototype; target CDC `335A34763533` (COM3); probe `E66540F0A345382D` |
| SD | 64 GB SDHC/SDXC reference card |
| Stimulus | Ambient radio and microphone |
| Driver | `build/sd_failure_jjy4.py`; all CDC lines in `build/jjy10-session.jsonl` |
| Bench power, listening check, WAV inspection | Not performed |

The board was run remotely; nobody was at the bench.

## Host-side discard on open

pyserial's Windows `open()` raises DTR and then calls `PurgeComm`, which discards
received data. The target sends queued replies as soon as the host polls, so replies
that arrive in that window are lost on the host before the application reads.

To separate device behavior from that host behavior, the reopen step was run twice:
once with the stock open, and once through a wrapper that replaced
`serial.serialwin32.win32.PurgeComm` with a no-op during `open()` only. The wrapper is
a session script and was not retained in the repository.

## Results

Each recording was started with `RECORD START`. The port was closed once
`OK RECORD START` arrived, and reopened 20 s (20 s recording) or 15 s (60 s
recordings) after the recording should have ended. On reopen the host sent `RECORD STATUS`
immediately.

| Label | Recording | Reopen | Lines received before the `RECORD STATUS` reply |
| --- | --- | --- | --- |
| `bp60-start` / `bp60-reopen` | `REC068.WAV`, 60 s | Stock (purge) | None |
| `bp20-nopurge-*` | `REC069.WAV`, 20 s | No purge | `progress=4.9s`, `progress=19.9s`, `OK RECORD PASS`, `RECORD DIAG` |
| `bp60-nopurge-*` | `REC070.WAV`, 60 s | No purge | `progress=4.9s`, `progress=59.9s`, `OK RECORD PASS`, `RECORD DIAG` |

- Every recording completed with exact frame counts (`frames=2883584` for 60 s,
  `962560` for 20 s), radio/PDM queue high-water 1/8, no overrun, SD or audio error,
  and `HAS_FAULT=0`. `DIAG LAST` returned `NONE`: no `USB_BACKPRESSURE` fault.
- `RECORD LATENCY` reported `usb-superseded=10` after `REC068`, `12` after `REC069`
  and `22` after `REC070`, with `usb-lost=0` throughout. The counters are cumulative
  since boot. They match one superseded progress line every 5 s while the port was
  closed.
- `progress=4.9s` is the line the target handed to the USB stack before it found the
  host was no longer reading. That transfer stays pending in the IN endpoint, so it is
  delivered first and is stale.
- With the stock open, `REC068`'s queued lines (newest progress, PASS, DIAG) were
  transmitted (the queue was empty when `RECORD STATUS` was answered and nothing was
  counted as lost) but did not reach the application. This also explains jjy.6, where
  only some of four queued lines arrived.

Connected regression (`connected20`, `REC071.WAV`, 20 s, port open throughout):
progress at 4.9, 9.9, 14.9 and 19.9 s, then `OK RECORD PASS` and `RECORD DIAG`, in
order, unchanged from earlier images. Queues 1/8, max write 25 ms, `HAS_FAULT=0`.

`LOOP_MAX_MS` read 125-127 ms after the first recording, with `HAS_FAULT=0`, so no
recording-time foreground budget violation was recorded. The maximum most likely
includes `RECORD START` file open and preallocation outside the recording budget; it
was not attributed in this session.

## Verdict

Pass for the device policy: with the CDC port closed for a whole recording, the target
keeps and delivers `OK RECORD PASS` and `RECORD DIAG` after reconnect, coalesces
progress lines, and counts them.

Partial end to end: a host whose open discards received data (pyserial on Windows, as
used by the ad hoc drivers) can still lose the outcome line. Recovering the outcome
on such hosts is filed as `jjy.12`. Native host test:
`tests/reply_queue_test.c` (17/17 host tests pass in the documented Release build).
