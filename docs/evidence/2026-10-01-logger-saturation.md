# Logger saturation, UART faults and USB backpressure during recording

Date: 2026-10-01
Beads issue: `full_spooky_proto-jjy.6`

This session tests recording while the M7 logger is overloaded, while its UART
transport faults, and while the USB CDC host stops reading. It also checks the
diagnostic dump contract under load. Probe-side and host-side loss attribution
is out of scope (`5yv.10`). The device counters here are the target's own view.

## Setup and provenance

| Item | Value |
| --- | --- |
| Source revision | `b818c13` (jjy.4/jjy.7 commits) plus the bench-only load generator below |
| Bench image | `build/bench-jjy6-logload-20261001b.json`, build ID `jjy6-logload-20261001b`, IpcSmoke preset configured with `SPOOKY_LOGGER_LOAD_QUALIFICATION=ON`; patch `build/jjy6-logload-20261001b.patch` (SHA-256 `c9f9af1671698387112ac48e51ffb9bcb37bcf46ca7cd9386638e303ca2401ce`) |
| CM7 / CM4 SHA-256 | `b2e79a25b9b0b4b653299bcfcb36b7c20c131675bba5bcb61a27270b1eacf22d` / `7d9d1116cb20cd8367de3d86a2ec8f067b1cf0fa0ea9220279c6cf9492489584` |
| Compiler | GNU Tools for STM32 14.3.1; Debug and Release pairs (option off) also built |
| Board | Nucleo prototype; target CDC `335A34763533`; probe `E66540F0A345382D` |
| SD | 64 GB SDHC/SDXC reference card |
| Stimulus | Ambient radio and microphone; IPC smoke traffic running throughout |
| Driver | `build/sd_failure_jjy4.py` and `build/jjy6_load.sh`; all CDC lines in `build/jjy6-session.jsonl` |
| Bench power | Not measured |
| Listening check | Not performed |

The first load run used image `a`, which is identical except it lacked the
latency histogram. Its results agree with the image `b` rerun and are not used
below.

### Load generator

The opt-in option adds `CM7/App/logger_load.c`. It is off in every normal preset.

- `LOG LOAD <bytes/s> <seconds>` writes fixed 64-byte lines through
  `TargetLogger_Write` from the foreground, at most 2 KiB per pass. It times
  each write with the DWT cycle counter.
- `LOG LOAD STATUS` and `LOG LOAD HIST` report writes, rejected writes and
  bytes, the worst write and pass time, and a write-latency histogram.
- `LOG FAULT` masks UART7's transmit interrupts while a chunk is in flight. The
  logger's 250 ms watchdog must then abort that chunk, as it would for a
  transmitter that stopped completing.

The UART line rate is 11,520 B/s, so 40,000 B/s is about 3.5 times overload. At
200,000 B/s the 2 KiB pass cap limits actual production to about 154 kB/s.

## Baseline

The paired-flash recording regression passed every stage with no load
(`2026-10-01T183150.229063_0000-73ce08c9`). `REC055.WAV` had 2,883,584 frames,
exact accounting, queues 1/8 and a worst write of 25 ms.

## Overload during recording

Each 60 s recording ran with the load started 3 s earlier. IPC, logger and load
counters were polled about every 10 s.

| Run | Load | Recording | Queues | Worst write | Budget violations | IPC per 10 s | Worst `RX_AGE` |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `load40k-b` | 40,000 B/s | `REC057.WAV` PASS, 2,883,584 frames | n/r | 26 ms | 0 | 93--94 | 108 ms |
| `load200k-2` | 200,000 B/s | `REC062.WAV` PASS, 2,883,584 frames | 1/8 | 25 ms | 0 | 92--95 | 101 ms |
| `fault40k` | 40,000 B/s + 5 UART faults | `REC063.WAV` PASS, 2,883,584 frames | 1/8 | 32 ms | 0 | 72--95 | 90 ms |

`n/r`: the `RECORD DIAG` line was not captured, because of a host-script
buffering bug fixed before the later runs. Progress samples showed queues 0/8.

In every run, radio and PDM overrun, SD error and audio error counters stayed
at zero, and the IPC link stayed `UP` with `ERROR=0`. During recording, the
foreground loop peaked at 34--59 ms against its 75 ms budget. Logger service
including load generation peaked at 3--5 ms against its 10 ms budget. The UART
sustained about 10.6--10.8 kB/s, roughly 93% of line rate.

`REC062.WAV`, from the 200 kB/s run, was retrieved with CRC and inspected
(`2026-10-01T185917.173422_0000-94fc1667`, pass, target healthy). The file is
17,301,548 bytes, CRC32 `d79050bc`, SHA-256
`31bb64c5fdd4daa97ac49172c187075c05a82f2fd05abc2bf3daaefc2faf3620`. It is
PCM16 at 48 kHz with three channels and 2,883,584 frames, exactly the recorder's
count. Radio peaks were 1,528 and 1,531 with RMS 326; the microphone peak was
120 with RMS 7.8. No samples were clipped.

### Producer latency

| Condition | Writes | <10 µs | 10--50 µs | 50--100 µs | 100--500 µs | 0.5--1 ms | >=1 ms | Max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 40 kB/s, recording (`load40k-b`) | 45,525 | 29,735 | 15,770 | 1 | 0 | 17 | 2 | 2,019 µs |
| 200 kB/s, recording (`load200k-2`) | 178,873 | 135,921 | 42,734 | 13 | 35 | 157 | 13 | 2,019 µs |
| 40 kB/s, recording + faults (`fault40k`) | 45,031 | 28,741 | 16,244 | 3 | 1 | 38 | 4 | 2,018 µs |
| 40 kB/s, not recording | 12,498 | 8,214 | 4,269 | 0 | 15 | 0 | 0 | 284 µs |

At least 99.8% of writes complete in under 50 µs. A write never waits for queue
space; a full queue rejects it. The 0.5--2 ms outliers appear only while
recording, so they are most likely interrupt work preempting the foreground
between the two timestamps, not logger behavior. Attributing them is `jjy.11`.
The worst full pass of 32 writes was 5.4 ms.

### Rejected-write accounting

After the first 40 kB/s run stopped and the queue drained:

| Counter | Load generator | Logger | Difference |
| --- | ---: | ---: | --- |
| Rejected writes | 40,744 | `DROP_WRITES` 40,748 | 4 other rejected lines |
| Rejected bytes | 2,607,616 | `DROP_BYTES` 2,607,868 | 252 B in those 4 lines |
| Accepted bytes | 992,256 | `TX_BYTES` delta 993,202 | 946 B of normal output |

`QUEUED=0` and `TX_LOST=0`, so every accepted byte was transmitted, and every
rejected write and byte is counted. Logger loss does not raise `HAS_FAULT`; it
is recorded as `LOG_LOSS` diagnostic events.

## UART transport faults

Five `LOG FAULT` injections during `fault40k` each produced exactly one
transport error and lost exactly the stalled 128-byte chunk:

```text
TX_LOST=128  TX_ERRORS=1
TX_LOST=256  TX_ERRORS=2
...
TX_LOST=640  TX_ERRORS=5
```

Transmission resumed after every abort: `TX_BYTES` grew by about 106 kB per
10 s throughout. The latched fault is `DIAG LAST ... EVENT=LOG_ERROR A=5 B=640`,
with `HAS_FAULT=1`.

## USB CDC backpressure

The host port was closed immediately after `RECORD START` and reopened after
the recording ended. Neither recording was affected.

| Recording | Closed for | Dropped lines (`USB_BACKPRESSURE`) | Received after reopen |
| --- | --- | --- | --- |
| `REC064.WAV`, 20 s | 40 s | `RECORD DIAG` (72 B) | `OK RECORD PASS` |
| `REC066.WAV`, 60 s | 75 s | three progress lines (53 B), then `OK RECORD PASS` (117 B) and `RECORD DIAG` (73 B) | two stale progress lines (19.9 s, 24.9 s) |

The recorder queues four lines and drops the newest when full. In the 60 s case
it therefore discarded the outcome line and kept stale progress lines. `RECORD
STATUS` (`frames=2883584`) and the `RECORD_END` event still give the true
outcome, but a host waiting for PASS would time out. This conflicts with
Principle II and is filed as `jjy.10`. Each drop was counted. pyserial purges
the port on open on Windows, so host-side loss of individual progress lines
cannot be separated from device drops.

## Diagnostic dump semantics

A `DIAG DUMP` during a recording under 40 kB/s load returned the header
`COUNT=128`, 127 event rows and one `DIAG GAP SEQ=4501` row for an entry
overwritten during the dump, then `OK DIAG END COUNT=128 GAPS=1`. This matches
the v1 contract: END counts event and gap rows.

The port was then closed 0.3 s into a second dump and reopened after 7 s. The
first new command (`DIAG STATUS`) was answered immediately, with no
reply-active rejection. The recording running underneath (`REC067.WAV`, 40 s)
completed with queues 1/8.

## Verdict

Pass for the logger saturation and UART fault gates; partial for USB
backpressure.

- Logger overload up to about 13 times line rate, and repeated transport
  faults, did not delay recording. There was no overrun, no budget violation,
  queues stayed at 1/8 and IPC kept steady progress.
- Producer writes are non-blocking and almost always under 50 µs.
- Rejected writes and bytes, and transport losses, reconcile exactly with the
  logger counters.
- Dump GAP/END semantics hold under load, and a dump abandoned by the host does
  not block later commands.
- USB backpressure never affects audio, but it can drop the recording outcome
  line (`jjy.10`).

Not covered: concurrent sensor or UI streams interleaving with dumps, the
5-second dump timeout observed directly, probe and host loss attribution
(`5yv.10`), and a listening check.
