# IPC reset and mismatch observations

Date: 2026-09-29  
Beads issue: `full_spooky_proto-jjy.2`

This session exercised the supported reset and image-pair paths for the opt-in IPC
experiment. The operator confirmed the board was connected and free before the
session. Spooky Bench 0.7.0 ran on Windows with target CDC on COM3 and Spookyprobe
on COM6. Firmware sources matched clean Git revision
`c6d333dfbdc33717275ff441607839d1820c054a`.

An explicit power-removal cold start was not run because the operator was away from
the bench by the time that step was reached. The current OpenOCD/probe path could
program and verify both flash banks, but did not expose an independently controllable
M4 target. M4 halt/stale/resume was therefore unsupported in this session rather
than passed or failed.

## Image manifests

| Preset | Manifest | CM7 SHA-256 | CM4 SHA-256 |
| --- | --- | --- | --- |
| IpcSmoke | `build/bench-jjy2-ipcsmoke-20260929a.json` | `314048377b4b869476ed134dd1fdbf8f16b30323d8966685c88ee382fbfee025` | `7d9d1116cb20cd8367de3d86a2ec8f067b1cf0fa0ea9220279c6cf9492489584` |
| IpcMismatch | `build/bench-jjy2-ipcmismatch-20260929a.json` | `e2b0cc069cb57061854b88ac116872deb22c5c50f16319e5249c49ba725cb5aa` | `5f6e2567967cb2943fd3a65a9036280a7274ed8b7552bf004786a7f45aa6cd21` |

Both top-level preset builds configured and completed before the manifests were
created. Compiler identity and build flags are explicitly null in the manifests;
they were not inferred after the build.

## Normal-build sleeping or held-core observation

The initial non-resetting probe passed in run
`2026-09-29T195434.838500_0000-d37fc985`. It observed M7 running and reported M4
as `unavailable`, with the tool's qualification that this can mean sleeping,
boot-held, or an access failure. No M4 liveness conclusion is drawn from this
observation.

## Matching pair and paired reset

The first matching-pair boot smoke passed in run
`2026-09-29T195536.648210_0000-b1a75051`. Both declared images were programmed and
verified, the reset command completed, M7 was left running, target CDC was
rediscovered, diagnostics were healthy, and IPC progressed between samples:

| Field | First | Second | Delta |
| --- | ---: | ---: | ---: |
| TX | 109 | 171 | +62 |
| RX | 118 | 181 | +63 |
| ACK | 108 | 170 | +62 |
| ROUNDTRIPS | 108 | 170 | +62 |

Both samples reported `LINK=UP`, ABI 1/1, both SEEN fields set, and
`ERROR=0 PEER_ERROR=0 BUSY=0`.

A separate paired reset passed in run
`2026-09-29T195656.389196_0000-29b825fc`. The reset command completed and M7 was
left running. Direct M4 state remained `unknown`, as required by the control
contract when the debugger does not examine it.

The post-reset 10-second IPC-load run
`2026-09-29T195930.661634_0000-be7bfe94` passed. `REC040.WAV` finalized with
483,328 frames, 2,899,968 data bytes, 10.069 seconds of audio and 10,110 ms
elapsed time. Radio and PDM queue high-water marks were 1/8, maximum SD write was
50 ms, and recorder, SD and audio error counters were zero. IPC was UP with
matching versions and zero error/BUSY fields before, twice during recording, and
after recording; TX, RX, ACK and ROUNDTRIPS advanced at every sample.

One earlier invocation created incomplete run
`2026-09-29T195701.375647_0000-a6ed6808` when the host shell stopped retaining the
long-running process after its output-yield boundary. It contains no result object
and is not hardware evidence. The board lock was confirmed released by a later
passing probe; no lock file was deleted or overridden.

## Deliberate version mismatch

The mismatch pair flash passed in run
`2026-09-29T200054.282902_0000-b0354aae`: both declared banks were programmed and
verified and reset completed. The generic IPC-load runner then stopped at its
health precheck in run `2026-09-29T200117.640188_0000-0103b141` because
`DIAG STATUS` truthfully reported `HAS_FAULT=1`; it did not start a recording or
reach its IPC query.

A single manual `IPC STATUS` request was then sent over the selected target CDC,
using the same 5.5-second settle and 8-second response deadline as Spooky Bench.
The response was:

```text
OK IPC LINK=INCOMPATIBLE VERSION=1 PEER_VERSION=2 TX=559 RX=0 ACK=0 ROUNDTRIPS=0 PEER_SEEN=0 ACK_SEEN=0 RX_AGE=0 ACK_AGE=0 ERROR=3 PEER_ERROR=0 BUSY=0
```

This is the expected explicit bad-version rejection. The mismatched peer did not
appear healthy and no peer data or acknowledgements were accepted.

## Matching-pair restoration

The matching IpcSmoke pair was restored and reverified in run
`2026-09-29T200217.651440_0000-8f3ee70a`. Both banks verified, reset completed,
CDC rediscovered, diagnostics reported no fault, and IPC returned to `LINK=UP`
with ABI 1/1, zero errors/BUSY and counter deltas of TX +62, RX +63, ACK +62 and
ROUNDTRIPS +62.

## Remaining qualification

An explicit full power-removal cold start remains to be run, followed by a
post-start matching-pair IPC health check. Independent M4 halt/stale/resume remains
unsupported by the current debugger exposure and is recorded as unsupported, not
as success or failure.

Constitution check at handoff: Principles III, V and VI were touched. The session
used versioned paired images, preserved exact image provenance, distinguished
inferred IPC liveness from direct debugger visibility, kept the normal Debug image
unchanged, and reported partial or unsupported cases honestly. No departure is
recorded.
