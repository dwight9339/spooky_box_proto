# Logger and diagnostic design

M7 owns a bounded UART7 text logger and an independent numeric diagnostic
history. The logger uses a 4096-byte queue, copies at most 512 bytes per write,
and sends contiguous chunks of at most 128 bytes using UART interrupts. It keeps
each chunk allocated until the completion callback. Full/oversized/context-invalid
writes are rejected and counted; printf remains best-effort and never waits for
queue capacity. Formatting itself still costs foreground CPU time.

`target_logger.c` supplies the strong `_write`/`__io_putchar` hooks. BSP
`USE_COM_LOG` is disabled to prevent its blocking UART implementation from
competing for the port. UART7 has one owner, no DMA, and IRQ priority 15. Its
completion IRQ can start the next chunk even while foreground initialization or
storage work is busy. A foreground watchdog aborts and discards a chunk still
pending after 250 ms; it cannot run while that foreground is blocked elsewhere.

Sleep stops capture and uses a maximum 400 ms drain before suspending SysTick.
If the drain times out, it aborts UART TX and discards the remaining queue so
pending logger IRQs cannot keep waking the MCU. It repeats this after each gauge
report wake. Normal operation never uses this drain.

`diagnostics.c` records fixed-size numeric events in a 128-entry RAM ring. Normal
M7 IRQs may record events using a short interrupt-masked critical section;
printf is foreground-only. SD write timing, recording start/end, queue overruns,
audio errors, foreground gaps, IPC state changes in experiment builds, logger
loss/errors, and charging sleep entry/wake are instrumented. The latest fault
and cumulative counters are retained separately after history wraps. No heap,
USB, or text formatting occurs in event producers.

Use device USB CDC commands `LOG STATUS`, `DIAG STATUS`, `DIAG LAST`, `DIAG DUMP`,
and `DIAG STOP`. HELP is also streamed in short lines; its former combined reply
exceeded the USB sender's 256-byte limit. See the
[v1 contract](spookyprobe-v1.md) for exact formats, units, and decoder behavior.

## Load characteristics

At saturated 115200 baud, the current non-FIFO UART implementation may generate
roughly 11,520 transmit-data interrupts/second plus completion interrupts. Measure
audio behavior and CPU load; a later DMA/FIFO transport can replace the adapter
while preserving the producer API. The current recorder's blocking USB reports
and filesystem operations are unchanged. This slice exposes their effects; it
does not make the whole cooperative loop bounded.

Host tests and bench procedure: [logger and diagnostics bench](../procedures/logger-diag-bench.md).
Evidence: [2026-09-23 bench record](../evidence/2026-09-23-bench-results.md).
