# Foreground latency during recording

The M7 foreground loop gives capture and storage first priority while a recording
is active. Timing is measured with the millisecond HAL tick at every service
boundary and from one loop start to the next. A sample is included only when DMA
capture is active at both boundaries; for the aggregate loop, both boundaries are
loop starts, so the passes that prepare the file (open, name, create, search and
preallocate; `full_spooky_proto-jjy.9`) and the pass that starts capture are not
recording passes. Each preparation pass is bounded by
`SPOOKY_RECORD_PREPARE_STEP_MS`, and the always-on `LOOP_MAX_MS` diagnostic still
records any loop gap of 50 ms or more. This excludes file-open/start and the final
header/sync/close after DMA stops; startup, diagnostics and maintenance intentionally
have different latency characteristics.

## Budgets

The recorder produces one 4096-frame block every 85.33 ms at 48 kHz. The firmware
rounds that arrival interval up to 86 ms for reporting and sets a 75 ms aggregate
loop budget, leaving at least 10 ms before the next block at the measurement
resolution. The recorder service has a 70 ms budget and does one of two things in
a pass: it converts one queued block into the three-channel output buffer, or it
writes that buffer with one FatFs call. Separating them keeps the conversion time
(up to 15 ms measured in the unoptimized Debug-based images) out of the pass that
carries a slow card write, so the budget covers a write of up to about 69 ms while
leaving the loop's mandatory 5 ms yield inside the 75 ms aggregate bound. Each block takes
two passes; at typical pass times that drains the queue well within one 85 ms
block interval. Every other foreground service has a 10 ms budget and must be
incremental or recording-aware.

| Service class | Budget |
| --- | ---: |
| Complete foreground pass, including the 5 ms yield | 75 ms |
| Recorder: one block conversion or one FatFs write | 70 ms |
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

## Interrupt preemption

Service and loop timings include any interrupt work that preempts them. While
recording, the largest interrupt work on the M7 is the per-block copy in the audio
DMA interrupts: the DFSDM microphone interrupt (priority 4) copies one 16 KiB block
every 85.33 ms, and the radio receive interrupt (priority 0) copies 1024 samples
every 10.7 ms. At the current 64 MHz, cache-off configuration these hold the
foreground for up to about 4.3 ms per microphone block, well inside the budgets
above; see [M7 interrupt cost](../evidence/2026-10-01-isr-timing.md) for the
measured bound. The opt-in `SPOOKY_ISR_TIMING_QUALIFICATION` build times every
handler for such measurements.

## Recording-aware behavior

- Recorder USB replies make one nonblocking submission attempt, then enter a
  four-line queue drained one line per loop. Progress lines coalesce and give way
  to outcome replies; losing a reply is a visible `USB_BACKPRESSURE` fault instead
  of a 250 ms foreground spin (see [USB CLI](usb-cli.md)).
- Periodic fuel-gauge and magnetometer bus transactions pause during recording.
  Battery/charge status then uses the last successful fuel-gauge snapshot and reports
  its age; outside recording it reads the gauge on each request
  (`full_spooky_proto-8lw.19`).
- A matrix animation that was started before recording is disabled without I2C
  cleanup; new test patterns remain rejected by command policy.
- SD maintenance and WAV transfer remain rejected while recording. The recorder
  performs at most one filesystem write in a service pass.

One filesystem call per pass is a granularity rule, not a hard latency guarantee:
FatFs and the card can still block longer than 70 ms. The measured violation and
queue-overrun diagnostics are therefore required evidence for each slow-card and
concurrent-load qualification run.
