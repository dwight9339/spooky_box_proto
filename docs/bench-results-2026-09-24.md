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
unimplemented at the Phase-1A checkpoint (`35b855f`). Phase-1B work follows below.

## Phase 1B: initial SWD probe

Version 0.2.0 adds control commands; these results use a newer source snapshot
than the Phase-1A runs above. The local profile selects Pico serial
E66540F0A345382D at 1000 kHz and pins PlatformIO OpenOCD 3.1200.0's actual
executable, DLLs and board scripts.

First attach run `2026-09-24T154842.702272_0000-e594e184` failed after 516 ms
with tool_output_limit, correctly recording incomplete evidence. A burst of
short OpenOCD writes filled the initial nonblocking output queue. The reader was
changed to bounded pipe backpressure and a 2000-short-write regression was added.

Run `2026-09-24T154935.414833_0000-c283f525` subsequently passed in 578 ms,
with complete evidence, OpenOCD exit 0 and source hash
`31364c5f4688dfa958c0083fcdaec64bbad3c2e5ee48e536d5c07da407c221a6`.
The saved log identifies the intended CMSIS-DAPv2 serial, SWD DPIDR 0x6ba02477,
Cortex-M7 r1p1 and SB_M7 running. M4 examination reported unrecognized PARTNO
0x0 and SB_M4 unavailable. This is explicitly unavailable, not evidence that M4
is faulty or that both cores are running. Sleeping/boot-held state versus access
failure is unresolved by this check. No reset, halt or flash command was issued.
The CMSIS-DAP FW Version string 2.0.0 is not a verified Pico firmware source hash.

38 offline tests pass including installed-interpreter configuration/Tcl checks,
manifest/ELF failures, integrated capture readiness and Windows descendant
termination. All eight existing preset/core ELFs pass structural checks; no
claim about their current build provenance was made. Reset acceptance follows
below; paired flashing remains pending. No images were flashed in these tests.

## Phase 1B: reset with UART capture and CDC recovery

The user confirmed no recording or active sessions, both cables connected, and
the working SYSOFF position. Reset run
`2026-09-24T155741.727709_0000-f38e80ee` passed in 2766 ms using source hash
`24de7a5bf3f5491017e157acc19a757ad6a95067ae204a9b31a8217a1620e97f`.
OpenOCD exited 0, reported reset completion and final M7 state running.
M4 was not directly examined; the paired final state remains unknown.

Integrated UART capture received/archived 2792 bytes with zero host drops.
Seven contiguous index rows cover the raw file with ordered monotonic timestamps;
session metadata records a normal stop. The inspected text starts with the UART7
banner and continues through successful USB CDC configuration, including the
audio bridge and peripheral startup reports. This verifies capture was active
in time for startup. It does not establish losslessness at the target/probe or
full peripheral/audio health.

Follow-up DIAG STATUS run `2026-09-24T155801.447372_0000-c495615f` passed in
5734 ms with COUNT=3 and no recorded faults, overruns, SD or audio errors.
DIAG DUMP run `2026-09-24T155818.422507_0000-30d76c99` passed in 5765 ms;
the saved raw/decoded JSONL has BOOT at tick 0, IPC_LINK waiting at 689 ms,
and IPC_LINK up at 791 ms, followed by END COUNT=3 GAPS=0.

This passes the initial live reset/capture/CDC-recovery check and supplies an
IPC startup-link transition. It is not a two-snapshot IPC counter-progress test
or proof of every reset mode. Fresh paired-image flash/verify/reset acceptance
remains the next Phase-1B hardware gate. Existing firmware images were preserved.

## Phase 1B: fresh paired flash and IPC progress

A fresh IpcSmoke M7/M4 pair was built from the working-tree snapshot at commit
`35b855fc16d0119287d62e95dbd9d58e7f86816d`. Because the Phase-1B utility and
documentation were uncommitted, the build is explicitly dirty. The archived
source/dependency snapshot SHA-256 is
`dc3533e074fbb315c4c05f018db0790fd537f14ebe98f2b336a4e67956980f28`.
The compiler was GNU Tools for STM32 14.3.1; the manifest records the core flags,
including SPOOKY_IPC_SMOKE=1 and M4 IPC_SMOKE_VERSION=1. A first build attempt
stalled in sandboxed Ninja before compilation; only that build's verified
CMake/Ninja processes were stopped. The retry outside the sandbox completed.

Image hashes declared and rechecked against the staged run artifacts:

| Core | SHA-256 |
| --- | --- |
| CM7 | `f55fc717ee615ca297e5f97e374c1064c88cf187dd1f0a6e7c70f74199901219` |
| CM4 | `0d97e4b75bad4d2a5836a6cbec4d1efc8f583fa34bb9b73e27f3dd61d795a83c` |

Flash run `2026-09-24T160557.372148_0000-e639966b` passed in 9890 ms with
complete evidence. OpenOCD identified the configured Pico, attached to M7/AP0,
halted M7, programmed and verified CM7 bank 0, then programmed and verified CM4
bank 1. Only after both `verified_cm7` and `verified_cm4` markers did it issue
reset/run. OpenOCD exited 0 and reported final M7 state running; M4 was not
directly examined, so the aggregate final target state remains unknown.

UART capture was ready before OpenOCD started. Its seven contiguous timestamped
chunks cover all 2792 received/archived bytes, with zero host drops and a normal
session stop. The readable boot log reaches successful target USB CDC
configuration. Driver, Pico and target loss remain unknown independently.

The immediate post-flash diagnostic request was initially blocked by account
usage review and therefore not executed. When work resumed, DIAG STATUS run
`2026-09-24T174300.335831_0000-1f59e7e1` passed with COUNT=3 and zero faults,
overruns, SD errors or audio errors. A first one-off IPC acceptance invocation
overlapped that query and was correctly rejected by the board lock; it performed
no request and created no run evidence.

Sequential IPC acceptance run `2026-09-24T174317.590213_0000-2d720857` passed.
Both raw archived responses report LINK=UP, VERSION=1, PEER_VERSION=1,
PEER_SEEN=1, ACK_SEEN=1, ERROR=0, PEER_ERROR=0 and BUSY=0. Over 1.2 seconds:

| Counter | First | Second | Forward delta |
| --- | ---: | ---: | ---: |
| TX | 57327 | 57340 | 13 |
| RX | 58513 | 58527 | 14 |
| ACK | 57326 | 57339 | 13 |
| ROUNDTRIPS | 57326 | 57339 | 13 |

RX_AGE/ACK_AGE were 66/66 ms then 42/42 ms. This supports M4 liveness inferred
from validated peer/ack progress; it is not a direct M4 debugger observation.
The one-off acceptance helper is retained under the ignored build directory and
is not presented as the still-planned Phase-2 product command.

LOG STATUS run `2026-09-24T174359.664194_0000-fce2dd67` passed with queue idle,
peak 1371/4096, TX_BYTES=4274, and zero dropped writes/bytes, transport loss,
transport errors or invalid-context writes. DIAG DUMP run
`2026-09-24T174413.742172_0000-7179eda9` passed with BOOT followed by IPC waiting
at 689 ms and IPC up at 791 ms, then matching END COUNT=3 GAPS=0.

This completes the initial Phase-1B live gate for SWD attach, reset with UART
capture, fresh paired program/verify, target CDC recovery, logger/diagnostic
integrity and IPC counter progress. Longer load tests, direct M4 observation in
other power states, failure recovery on hardware, and the automated Phase-2
boot-smoke command remained separate work at this checkpoint.

## Phase 2: single-command boot smoke

Spooky Bench 0.3.0 implements the first-class command from the plan. All 42
offline tests passed, including the boot-smoke happy path, CDC COM renumbering,
missing CDC, empty UART, disabled/stale/error IPC, diagnostic fault, partial
flash, process-tree cleanup, and the earlier serial/storage/protocol cases.
The editable environment was reinstalled and `pip check` reported no broken
requirements.

Preflight status run `2026-09-24T180146.010803_0000-2b738a05` found the target
on COM3 and Pico on COM6 by serial identity. An initial live boot-smoke passed;
a cleanup-only hardening change was then made so its source hash was superseded.
The exact final implementation was rerun as
`2026-09-24T180428.411526_0000-da695dce` and passed in 42,281 ms using runner
source hash `90801225ef940e05ed0d3572c8172f35765ffa108ae0749792d0a7fb0e1dd6b5`
and the same declared IpcSmoke build pair and image hashes recorded above.

Manifest preflight completed within timer resolution. The paired
flash/verify/reset stage took 7437 ms;
both cores verified before reset/run, OpenOCD exited 0, and final M7 state was
running. UART capture was ready before tool start. Recorded host-nanosecond epoch
markers place reset completion before tool completion, and the archive continued
through all structured checks. It received and archived 2790 bytes in seven
contiguous chunks with zero host drops and a normal stopped session.

The target CDC serial identity returned on COM3 after five attempts and 1032 ms.
Fresh DIAG STATUS established M7/schema-v1 liveness with COUNT=3 and zero fault,
overrun, SD, or audio counters. One IPC pair reported LINK=UP, ABI 1/1, both seen
flags, zero local/peer errors, and these forward deltas:

| Counter | First | Second | Forward delta |
| --- | ---: | ---: | ---: |
| TX | 109 | 171 | 62 |
| RX | 118 | 181 | 63 |
| ACK | 108 | 170 | 62 |
| ROUNDTRIPS | 108 | 170 | 62 |

This supplies the command's labeled M4 liveness inference from echo/ack progress.
LOG STATUS was idle with peak 1369, TX_BYTES=2790, and zero loss/error/context
counters. DIAG LAST returned NONE. DIAG DUMP completed with three events, no
gaps, BOOT followed by IPC waiting/up, and 791 ms of MCU event time. The final
result reports target_health healthy, final_target_state running,
human_required false, and complete evidence with all seven checks passing.

This passes the initial Phase-2 hardware gate for one local PC invocation. It
does not replace the pending repeated-reset, physical absence/timeout, sustained
load, power-state, or deliberately interrupted hardware tests.

## Phase 3: IPC progress under recording load

Spooky Bench 0.4.0 adds `test ipc-load --seconds N`. The final implementation has
45 passing offline tests, including simulated record abort, queue overrun, stale
IPC, target disconnect, pre-existing recording, bounded cleanup, and a spawned
worker run. The editable installation and `pip check` pass.

Hardware run `2026-09-24T182232.516815_0000-9de48985` passed in 109,750 ms with
complete evidence, `target_health=healthy`, `final_target_state=running`, and
`human_required=false`. It archived exact runner source hash
`3f07821d21ebd59c968b19bd2f48afa531ac1b3ef3bcd9e3d231cb78338d39d0`.
The target was selected on COM3 and the Spookyprobe Pico on COM6 by their saved
serial identities.

The target was initially idle and created `REC004.WAV`. It reported 2,883,584
frames, 17,301,504 data bytes, 60.074 seconds of audio, and 60,116 ms elapsed.
Eleven progress records were observed through 55.1 seconds. Maximum current queue
occupancy was radio 1 and PDM 0; final high-water was radio 1/8 and PDM 1/8.
Maximum SD write time was 44 ms, and final channel peaks were 1643, 1640, and
672. No cleanup was needed.

All IPC snapshots reported LINK UP, ABI 1/1, both seen flags, zero errors, and
BUSY=0. Forward counter evidence was:

| Interval | TX | RX | ACK | ROUNDTRIPS |
| --- | ---: | ---: | ---: | ---: |
| Before to first in-load sample | +253 | +272 | +253 | +253 |
| First to second in-load sample | +185 | +200 | +185 | +185 |
| Second in-load sample to after | +225 | +239 | +225 | +225 |

The final DIAG status had COUNT=128, SD_MAX_MS=44, LOOP_MAX_MS=204, and zero
radio/PDM overruns, SD/audio errors, or fault. Its 583 overwritten history entries
reflect the fixed 128-entry diagnostic ring under sustained SD_WRITE events;
the archived dump itself was complete and gap-free from sequence 584, containing
127 SD_WRITE entries and one RECORD_END. Final LOG status retained zero loss,
transport, and context errors with empty queues. DIAG LAST returned NONE.

This passes the first automated Phase-3 load gate: IPC foreground progress and
target-reported recording health are established throughout one 60-second run.
The load command itself did not copy or inspect the WAV; the separate follow-up
inspection is recorded below. The ten-minute run, deliberate failure hardware
cases, and broader SD/peripheral tests remain pending.

## Phase 3: host-side WAV inspection

Spooky Bench 0.5.0 and target binary protocol v1 add bounded retrieval and local
analysis of `REC###.WAV`. The target uses 1008-byte acknowledged data frames with
per-frame CRC32, a whole-file CRC32 end frame, a 256 MiB limit, strict filename
selection, and a ten-second missing-ACK timeout. The host reserves the declared
artifact size, retains `.partial` evidence, calculates SHA-256, and requires
strict RIFF/PCM accounting plus nonconstant data on all three channels.

All four CM7 presets build with the new firmware. The paired IpcSmoke deployment
used build ID `wav-inspect-20260924-01`, dirty source snapshot SHA-256
`49045304b3c739313f51947b62e7333bfb0a90aa1798777be544ffd6f567ec0c`,
CM7 image SHA-256 `a70137aa11fb4a714e7f2adbdb4b25239c66ed700c137d19ee6af5a83efa7506`,
and the prior matching CM4 image SHA-256
`8f6e005ffefdf6896fcaf85d5a51554aa64e97ecae3672b412a94ed29ba377cc`.
Boot-smoke run `2026-09-24T190022.631255_0000-015546c3` passed in 42,422 ms
with all seven checks, clean logger/diagnostics, forward IPC progress, and zero
host UART drops.

An intentionally interrupted first retrieval left a bounded partial artifact.
The target's missing-ACK timeout released the file; a subsequent direct two-frame
check validated the frame CRC and exact-offset ACK, and a new fetch began without
reset or manual recovery. This supplies initial hardware evidence for abandoned
host cleanup, though deliberate cable removal and storage failure remain.

Final run `2026-09-24T190727.483624_0000-a961b406` passed with exact host source
hash `bd48fa4dcfdcc2842c3bec6d48c032295c019e756cc4df121b4cb91b04c83995`.
It transferred `REC004.WAV` in 17,165 frames and 267,469 ms:

| Evidence | Value |
| --- | --- |
| File bytes | 17,301,548 |
| Audio bytes / frames | 17,301,504 / 2,883,584 |
| PCM format | 48,000 Hz, signed 16-bit, 3 channels, 6-byte alignment |
| Duration | 60.074667 seconds |
| Transfer CRC32 | `eb92c866` |
| SHA-256 | `fdedb2b5d885afe37865b007d84890f64ca7c4b730025358142ea78f13798098` |

Channel results were:

| Channel | Range | Peak | RMS | Mean | Zero samples | Clipped |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Radio left | -1643..1513 | 1643 | 322.129 | -5.395 | 3,578 | 0 |
| Radio right | -1640..1514 | 1640 | 322.181 | -7.454 | 3,608 | 0 |
| Microphone | -672..62 | 672 | 8.514 | 0.0001 | 141,312 | 0 |

The peaks exactly match the earlier target `RECORD DIAG`. No channel was constant;
one-second follow-up windows had minimum peaks 1087, 1085, and 31 respectively,
with no constant window. Radio left/right correlation was 0.999971, consistent
with essentially mono program content in this capture; radio/microphone
correlations were near zero. This is reported evidence, not a stereo-separation
failure criterion. `ffprobe` independently confirmed `pcm_s16le`, 48 kHz, three
channels, 16 bits, 60.074667 seconds, and 17,301,548 bytes.

Post-transfer DIAG STATUS remained schema 1/core 7 with all fault, overrun, SD,
and audio counters zero. LOG STATUS had empty queues and zero drop, transport,
and context errors. The ten-minute recording, listening tests, known stereo
material, cable-removal transfer recovery, and SD/peripheral runners remain.
