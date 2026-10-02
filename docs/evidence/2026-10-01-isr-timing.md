# M7 interrupt cost during recording

Date: 2026-10-01
Beads issue: `full_spooky_proto-jjy.11`

jjy.6 found 0.5-2.04 ms outliers in DWT-timed `TargetLogger_Write` calls, but only
while recording ([logger saturation](2026-10-01-logger-saturation.md)). The logger write never
waits, so interrupt work was the suspect. This session times every M7 interrupt
handler during idle and during 60 s recordings, and records the bound.

## Method

The opt-in CMake option `SPOOKY_ISR_TIMING_QUALIFICATION` (off in every preset) adds
`CM7/App/isr_timing.c`. Early in `main`, after the clock is configured, it copies the
flash vector table to a 1 KiB-aligned RAM table and points SysTick and all 150
external interrupts at one trampoline. The trampoline reads the exception number
from IPSR, calls the original handler, and records per exception:

- count, maximum inclusive and exclusive duration (exclusive subtracts nested
  handlers), total exclusive time, and how many runs took at least 100 and 500 µs;
- bursts: outermost handlers separated by less than 2 µs of foreground time, with the
  longest burst, its handler count and the handler with the most exclusive time in
  it, and a histogram of finished bursts.

`ISR LIST`, `ISR GET <exception>`, `ISR BURST` and `ISR RESET` report and clear the
figures. Exception numbers are IRQ + 16. The trampoline adds a few dozen cycles per
interrupt, and briefly masks interrupts for its own bookkeeping. Faults, SVC and
PendSV are not routed through it.

The M7 runs at 64 MHz from the HSI, with I- and D-cache off (`SystemCoreClock`
reported `CLOCK_HZ=64000000`). All figures are at that clock.

## Setup and provenance

| Item | Value |
| --- | --- |
| Committed source | Differs from the bench patch only in a comment in `isr_timing.h` and the docs |
| Source revision | `98d7b42` plus the jjy.11 change (`CM7/App/isr_timing.[ch]`, `CM7/Core/Src/main.c`, `CM7/CMakeLists.txt`, `cmake/dual_core_firmware.cmake`); patch `build/jjy11-isrtime-20261001a.patch` (SHA-256 in the manifests) |
| Image A | `build/bench-jjy11-isrtime-20261001a.json`, IpcSmoke preset (Debug, `-O0`) with `SPOOKY_LOGGER_LOAD_QUALIFICATION=ON` and `SPOOKY_ISR_TIMING_QUALIFICATION=ON`, the jjy.6 configuration plus timing. Flash run `2026-10-01T194155.004025_0000-5060c88c` |
| Image B | `build/bench-jjy11-isrtime-rel-20261001a.json`, Release preset (`-Os`) with only `SPOOKY_ISR_TIMING_QUALIFICATION=ON`. Flash run `2026-10-01T194512.673473_0000-ac600856` |
| Flash | Spooky Bench `flash`, pass, both cores verified; `DIAG IDENTITY` matched each build ID |
| Board | Nucleo prototype; target CDC `335A34763533`; probe `E66540F0A345382D` |
| SD | 64 GB SDHC/SDXC reference card |
| Stimulus | Ambient radio and microphone; image A also ran IPC smoke traffic |
| Driver | `build/sd_failure_jjy4.py`; all CDC lines in `build/jjy11-session.jsonl` |
| Bench power, listening check, WAV inspection | Not performed |

The board was run remotely; nobody was at the bench.

## Handlers seen

| Exception | IRQ | Handler | Work |
| --- | --- | --- | --- |
| 15 | - | SysTick (priority 0) | `HAL_IncTick`, `UiBoardTest_Tick1ms` |
| 27 | 11 | DMA1_Stream0 (priority 0) | SAI1 DMA |
| 31 | 15 | DMA1_Stream4 (priority 0) | Radio receive half: `ProcessHalf` copies 1024 samples to the recorder queue (while recording) and to the monitor buffer |
| 72 | 56 | DMA2_Stream0 (priority 4) | DFSDM microphone half: `RecorderEnqueuePdm` copies one 4096-sample `int32_t` block (16 KiB) into the recorder queue |
| 98 | 82 | UART7 (priority 15) | Logger transmit, one interrupt per byte |
| 117 | 101 | OTG_FS (priority 6) | USB CDC |

## Results

### Image A (Debug, IPC traffic)

Idle for 30 s, no load: radio half max 249 µs (every run ≥ 100 µs), SysTick max 28 µs
(average 27 µs), USB max 64 µs; longest burst 249 µs. This matches jjy.6's 284 µs idle
maximum for a logger write.

60 s recording (`REC072.WAV`) under `LOG LOAD 40000 90`:

| Exception | Count | Max exclusive | Max inclusive | Total exclusive | ≥ 500 µs |
| --- | --- | --- | --- | --- | --- |
| 72 DFSDM DMA | 705 | 1907 µs | 2475 µs | 1341 ms | 704 |
| 31 radio half | 6137 | 487 µs | 487 µs | 2859 ms | 0 |
| 98 UART7 | 724382 | 31 µs | 2487 µs | 5050 ms | 0 |
| 15 SysTick | 64251 | 29 µs | 29 µs | 1710 ms | 0 |
| 117 OTG_FS | 140 | 68 µs | 547 µs | 5 ms | 0 |
| 27 SAI1 DMA | 6081 | 12 µs | 12 µs | 65 ms | 0 |

Longest burst 2487 µs: 7 handlers, led by exception 72 (1906 µs). Bursts of at least
2 ms: 704, one per microphone block. Bursts of 500-1000 µs: 377.

`LOG LOAD HIST` over the same run: 37 writes of 500 µs or more (`LT1000US=35`,
`GE1000US=2`), `MAX_WRITE_US=2516`, the same tail as jjy.6. The recording passed with
exact frames, queues 1/8, write max 54 ms, `HAS_FAULT=0`.

### Image B (Release, no load, no IPC)

Idle for 20 s: radio half max 426 µs, longest burst 469 µs.

60 s recording (`REC073.WAV`):

| Exception | Count | Max exclusive | Max inclusive | Total exclusive | ≥ 500 µs |
| --- | --- | --- | --- | --- | --- |
| 72 DFSDM DMA | 705 | 3353 µs | 4264 µs | 2359 ms | 704 |
| 31 radio half | 6053 | 844 µs | 844 µs | 4927 ms | 5634 |
| 15 SysTick | 63352 | 15 µs | 15 µs | 855 ms | 0 |
| 98 UART7 | 1153 | 9 µs | 856 µs | 4 ms | 0 |
| 117 OTG_FS | 136 | 33 µs | 33 µs | 3 ms | 0 |
| 27 SAI1 DMA | 5996 | 7 µs | 7 µs | 36 ms | 0 |

Longest burst 4264 µs: 8 handlers, led by exception 72 (3353 µs). Bursts of at least
2 ms: 704. Interrupts used about 13% of the M7 during the 62.5 s window. The
recording passed with exact frames, queues 1/8, write max 15 ms, `HAS_FAULT=0`.

The copy-bound handlers (72, 31) are slower in the Release image even though
CPU-bound SysTick is faster. Both images link the same 28-byte newlib-nano
byte-loop `memcpy`, and neither enables a cache, so the copy loop runs from flash
without an instruction cache. Code placement of that loop is a plausible cause; it was
not verified here and belongs with `8lw.23`.

## Attribution and bound

- The recording-only 0.5-2 ms foreground preemption is the DFSDM microphone DMA
  interrupt (DMA2_Stream0). It copies a 16 KiB block once per 4096 frames (about
  every 85 ms), so a foreground operation that overlaps it is delayed by the copy plus
  any radio half, SysTick or UART7 handlers that run with it.
- The bound at the current 64 MHz, cache-off configuration: microphone block copy
  ≤ 3.4 ms exclusive (≤ 4.3 ms inclusive), radio half ≤ 0.85 ms, longest
  interrupt burst seen by the foreground 4.3 ms, in the Release image. The Debug
  image measures 1.9, 2.5, 0.49 and 2.5 ms respectively.
- The radio half interrupt runs at priority 0 and preempts the microphone copy, so
  radio capture is not delayed by it. USB (priority 6) and the UART7 logger
  (priority 15) wait for it. SysTick, also priority 0, kept up: 63,352 ticks in about
  63 s.
- These bounds are small against the 75 ms recording-time foreground budget and the
  ~680 ms recorder queue headroom, and no recorder, IPC or USB impact was observed.
  They are not small against Principle IV's intent for bounded ISR work: the
  microphone copy is the largest single piece of interrupt work on the M7. Moving the
  per-block copies out of the DMA interrupts is filed as `jjy.13`.
- UART7 takes one interrupt per transmitted byte (724,382 in the 40 kB/s load run,
  about 8% of the M7). That cost scales with logger traffic, not with recording.

## Verdict

Pass for attribution and measurement: every M7 interrupt handler active during
recording is timed, the outlier source is identified, and the bound is recorded for
Debug and Release images at the current clock. No design change is made here.
