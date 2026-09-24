# Spooky Bench live observations: 2026-09-24

Phase-1A basic live checks passed; extended checks remain below. Runner source hash:
`810aa52a68e3eda3414e59f2ad632b08bd38f59f3d8e1063ee843f3ad8137398`.
Spookyprobe host source pin: `ce039cab6d15171aa069991dd743e1e14e64aeef`.
Host: Windows, Python 3.12.14, pyserial 3.5. Exact target and probe firmware
image hashes remain unknown.

## Discovery and configured selection

The user's discovery result identified target CDC on COM3 and Pico CDC on COM6.
A local profile was created using VID/PID plus each device's USB serial number,
then configured status passed without opening the ports. The profile is ignored
by Git; it does not depend on those COM numbers remaining fixed.

Configured status run: `2026-09-24T151022.718452_0000-af94df68`, 281 ms.
Discovery/selection success does not establish target health.

## First diagnostic query

The user ran `diag status` through Spooky Bench. The saved `test-results.json`
and `diagnostics.jsonl` were subsequently inspected and match the supplied
output, including session start, raw response bytes, decoded fields, and session
end. Run: `2026-09-24T151047.462369_0000-9abeb7e8`, 5641 ms, execution=hardware,
result=pass, evidence_complete=true.

```text
OK DIAG V=1 CORE=7 COUNT=3 OVERWRITTEN=0 SD_MAX_MS=0 LOOP_MAX_MS=0 RADIO_OVR=0 PDM_OVR=0 SD_ERR=0 AUDIO_ERR=0 HAS_FAULT=0
```

This validates the first live command/response and artifact path: the selected
target answered with supported schema/core, and the result was decoded and
archived. Its boot-lifetime diagnostic counters report no recorded faults,
queue overruns, SD errors, or audio errors. Zero SD_MAX_MS is not a storage
performance measurement; this response does not show any timed data writes.
The utility deliberately reports target_health=not_checked: successful querying
is not a full device health test.

Artifacts are under `%LOCALAPPDATA%/SpookyBench/runs/<run-id>/` on this PC.
No artifact directories or device-specific local profiles are committed here.

## Logger, latest fault, and complete dump

All three further commands passed with execution=hardware and complete evidence.
The corresponding saved diagnostic JSONL files were independently read to check
the raw responses, beyond the pasted CLI summaries.

| Command | Run ID | Duration | Result |
| --- | --- | --- | --- |
| LOG STATUS | `2026-09-24T151240.486101_0000-a9bae1bd` | 5640 ms | QUEUED=0, PEAK=1370, TX_BYTES=3734; DROP_WRITES, DROP_BYTES, TX_LOST, TX_ERRORS, CONTEXT and FLIGHT all zero |
| DIAG LAST | `2026-09-24T151255.533488_0000-14f19716` | 5656 ms | NONE |
| DIAG DUMP | `2026-09-24T151308.596727_0000-57045fdd` | 5641 ms | Complete: 3/3 events, no gaps, no decoder errors, matching END |

The dump contains BOOT at MCU tick 0, IPC_LINK waiting (A=0, B=0) at 928 ms,
then IPC_LINK up (A=1, B=0) at 1030 ms. This is startup transition evidence,
not a round-trip latency measurement. The logger peak is 1370 of 4096 bytes;
zero target-reported loss does not establish lossless probe/host reception.

## First UART capture: silent session

Run `2026-09-24T151652.914056_0000-7c4be881` completed a 30-second hardware
console capture in 30422 ms with result=pass and evidence_complete=true. Received
and archived bytes were both zero, with zero host drops. The saved raw.bin and
chunks.jsonl are empty; session.json reports end_reason=stopped.

The user confirmed that the Nucleo was not reset during this capture. No known
log stimulus was established, so this is a completed silent session, not evidence
of a broken UART path or successful end-to-end logging. Repeat with capture open
before a manual Nucleo reset (with recording stopped) to collect startup logs.

## UART startup capture

Run `2026-09-24T151950.271722_0000-b8f1390f` completed a 30-second hardware
capture in 30344 ms with result=pass and evidence_complete=true. It received and
archived 2791 bytes with zero host-dropped bytes or chunks. The saved raw file,
index, session metadata and result were inspected: six contiguous index entries
cover exactly 2791 bytes, monotonic host timestamps are ordered, and the session
ended normally with end_reason=stopped.

The readable log begins with the UART7 banner and includes codec/audio bridge,
fuel gauge, magnetometer and UI initialization, followed by successful target USB
CDC configuration. The radio reports valid=0/SNR=0 for the tuned station; the
audio bridge PASS establishes firmware-reported stream startup, not reception
quality or listening validation.

This establishes initial target UART7 -> Pico -> PC capture and archive operation.
Driver, probe and target loss remain unknown for this run; zero host drops alone
does not prove lossless transport at every stage. The command's automated
target_health remains not_checked.

## Probe disconnect during capture

For the requested probe-unplug check, run
`2026-09-24T152243.840052_0000-fa8bbe18` ended after 5172 ms rather than waiting
for the requested 30 seconds. It returned result=error, reason=io_error,
evidence_complete=false, with Windows ClearCommError reporting that the device
does not recognize the command. This is the expected failure response for this
test, not an artifact-directory permission failure.

Saved artifacts were inspected: all 2792 received bytes remain in raw.bin,
covered by six contiguous index entries with ordered monotonic timestamps and
zero host drops. The text includes startup through USB CDC configuration.
session.json records end_reason="disconnect or read failure". The partial
capture is usable and correctly marked incomplete.

## Probe reconnect recovery

The subsequent capture, run `2026-09-24T152439.761992_0000-fca1bfbc`, passed in
30391 ms with evidence_complete=true. Saved artifacts confirm all 2790 received
bytes were archived, with seven contiguous index entries, ordered monotonic
timestamps, and zero host drops. The log starts with the UART7 banner and ends
with successful target USB CDC configuration; session.json reports
end_reason=stopped.

This validates probe reconnect recovery in a new invocation and demonstrates
that the failed run did not leave the capture port or bench locks blocking it.
The Pico returned on COM6, so COM renumbering was not exercised. Concurrent
lock contention and Ctrl+C cleanup are covered separately below.

## Target CDC disconnect and recovery

Run `2026-09-24T152654.025605_0000-1661d477` returned error/io_error after
1594 ms with evidence_complete=false when target CDC was disconnected.
Windows reported the same ClearCommError device-disconnect failure as the Pico
test. Its saved diagnostics.jsonl contains only session_start: the disconnect
occurred during the initial synchronization period, before DIAG STATUS was
sent. This validates disconnect handling during an open diagnostic session,
not a mid-response or interrupted-dump test.

After reconnect, run `2026-09-24T152723.521997_0000-9ccd795b` passed in 5672 ms
with evidence_complete=true. The saved JSONL includes a complete OK DIAG V=1
CORE=7 response and session_end. COUNT=3; overwrite, SD/audio error, queue
overrun and fault fields are zero. Both JSONL files were inspected directly.
The successful subsequent query establishes target CDC recovery and that the
failed session left no blocking port or bench lock.

## Concurrent-operation rejection and Ctrl+C

While a 60-second console capture was active, the user's second-terminal
DIAG STATUS invocation returned error/bench_busy in 281 ms at 15:29:32 UTC,
with no run artifacts and detail="Another operation owns this board_id".
This validates cooperative rejection of a second operation on the same board.

Ctrl+C ended capture run `2026-09-24T152926.163759_0000-b1d38293` with
error/interrupted, cleanup=terminated, evidence_complete=false and
human_required=false. The reported 33563 ms is total run duration, not measured
Ctrl+C response latency. Inspection found the source/profile/device/metadata
files and empty raw/index files, but no final test-results.json or UART
session.json, consistent with the documented worker-termination behavior.
The CLI interruption result is supplied by the user; it is not a finalized
result inside that run directory.

## Post-interrupt recovery

DIAG STATUS run `2026-09-24T153156.691303_0000-77b129d8` passed in 5719 ms
with complete evidence. Its saved JSONL contains session_start, a complete
OK DIAG V=1 CORE=7 response with no recorded faults/errors, and session_end.
This verifies that the interrupted capture released the bench locks.

The subsequent five-second console run,
`2026-09-24T153217.799105_0000-a59a5113`, passed in 5406 ms with complete
evidence, verifying that the Pico serial port could be reopened. Its raw/index
files are empty, matching the reported zero received/archived bytes and zero
host drops; session.json records end_reason=stopped. This intentionally silent
cleanup check does not add UART data-delivery evidence. Both runs' saved
diagnostic/session artifacts were inspected.

Configured discovery, all four Phase-1A diagnostic command paths, nonempty
UART capture, and disconnect/reconnect handling on both serial paths have passed
initial live checks.
Concurrent-operation rejection and Ctrl+C worker termination also behaved as
expected, and subsequent queries/capture confirmed lock and port release.
The basic Phase-1A live sequence is complete. Extended checks remain:
mid-response/dump disconnect, timing and loss under sustained load, and hardware
COM renumbering. Phase 1B controls and the automated IPC runner remain
unimplemented.
