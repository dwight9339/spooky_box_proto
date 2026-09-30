# Foreground latency during recording

The M7 foreground loop gives capture and storage first priority while a recording
is active. Timing is measured with the millisecond HAL tick at every service
boundary and from one loop start to the next. A sample is included only when DMA
capture is active at both boundaries; for the aggregate loop, both boundaries are
loop starts, so the pass that opens and preallocates the file and then starts
capture is not a recording pass. This excludes file-open/start and the final
header/sync/close after DMA stops; startup, diagnostics and maintenance intentionally
have different latency characteristics.

## Budgets

The recorder produces one 4096-frame block every 85.33 ms at 48 kHz. The firmware
rounds that arrival interval up to 86 ms for reporting and sets a 75 ms aggregate
loop budget, leaving at least 10 ms before the next block at the measurement
resolution. The recorder service, including one interleaved FatFs write, has a
70 ms budget. That sub-budget covers the observed 66 ms conversion/write maximum
(52 ms inside FatFs) while leaving the loop's mandatory 5 ms yield inside the
75 ms aggregate bound. Every other foreground service has a 10 ms budget and must
be incremental or recording-aware.

| Service class | Budget |
| --- | ---: |
| Complete foreground pass, including the 5 ms yield | 75 ms |
| Recorder conversion plus one FatFs write | 70 ms |
| Each IPC, audio, fuel, USB, WAV, logger, diagnostics, UI, SD-test, power, magnetometer and event-dispatch service | 10 ms |

The two eight-entry audio queues reject the newest block on overflow. With the
measured recording high-water of one block, seven unused entries represented
about 597 ms of observed headroom. That observation is useful evidence, not the
contract: the 75 ms budget is deliberately below one producer interval so safety
does not depend on retaining seven empty queue entries.

`DIAG LATENCY` reports each fixed budget, maximum observed duration and violation
count. A violation also records `FOREGROUND_BUDGET` with the service ID in `A` and
the measured milliseconds in `B`, sets `HAS_FAULT=1`, and remains available via
`DIAG LAST`/`DIAG DUMP`.

## Recording-aware behavior

- Recorder USB replies make one nonblocking submission attempt, then enter a
  four-line queue drained one line per loop. Queue overflow is a visible
  `USB_BACKPRESSURE` fault instead of a 250 ms foreground spin.
- Periodic fuel-gauge and magnetometer bus transactions pause during recording.
  Battery/charge status uses the last successful fuel-gauge snapshot.
- A matrix animation that was started before recording is disabled without I2C
  cleanup; new test patterns remain rejected by command policy.
- SD maintenance and WAV transfer remain rejected while recording. The recorder
  performs at most one filesystem write in a service pass.

One filesystem call per pass is a granularity rule, not a hard latency guarantee:
FatFs and the card can still block longer than 70 ms. The measured violation and
queue-overrun diagnostics are therefore required evidence for each slow-card and
concurrent-load qualification run.
