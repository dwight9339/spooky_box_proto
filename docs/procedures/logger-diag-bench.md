# Logger and diagnostics bench procedure

Validation of the M7 logger and diagnostic history defined in the
[logger and diagnostic design](../design/logger-diag.md) and the
[Spookyprobe v1 contract](../design/spookyprobe-v1.md). Status is tracked in Beads;
results belong in `docs/evidence/`.

## Validation before the bench

Build Debug, Release, IpcSmoke, and IpcMismatch as described in the main README.
Run the [native tests](../../tests/README.md). They exercise queue lifetime,
overload/error recovery, sleep quiescence, event wrap/fault retention, and USB
response backpressure. No test here establishes hardware timing or recording
reliability. No M4 logger, Pico firmware, or host serial capture tool is included.

## Bench acceptance

1. Flash a matched Debug image pair. Capture UART7 at 115200 8N1 and open the
   device's separate CDC port. Verify startup output and HELP, then save
   `LOG STATUS`, `DIAG STATUS`, `DIAG LAST`, and a complete `DIAG DUMP`.
2. Record a normal three-channel WAV while periodically reading status/dumps.
   Check the WAV and confirm no new queue overruns or audio errors. Save SD write
   timings and logger counters with the result. A history wrap is expected:
   about 11.7 SD_WRITE events/second gives roughly 11 seconds in the ring before
   accounting for other event types.
3. Generate a controlled foreground burst larger than the logger queue in a
   temporary bench build. Confirm that execution progresses, loss counters
   increase, and text output resumes afterward. Preserve counter deltas at the
   device, probe, and host independently. Merely unplugging the probe does not
   cause UART backpressure because there is no flow control.
4. Request a dump and disconnect CDC; reconnect after more than five seconds.
   Verify a new command works. During a slow dump under recording, verify GAP
   rows and matching END counts when entries are overwritten. Mark dumps without
   END incomplete. Concurrent sensor/UI streams can interleave with replies.
5. With recording stopped, use SLEEP START. Verify the final UART messages drain,
   the 10-second RTC report occurs, the five-minute cadence follows, and pending
   UART transmission does not cause repeated wakeups. Check current consumption.
6. Repeat recording and diagnostics in IpcSmoke; check IPC_LINK events and the
   independent IPC STATUS command. Use IpcMismatch for incompatible-peer evidence.
