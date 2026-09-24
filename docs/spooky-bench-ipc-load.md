# Spooky Bench: IPC progress under recording load

Implemented in version 0.4.0, 2026-09-24. This is the first Phase 3 runner. It
starts a bounded three-channel recording on an already running `IpcSmoke` pair
and proves that the two-core diagnostic exchange keeps advancing before, during,
and after the workload.

## Preconditions and invocation

Use the target application CDC cable and a configured bench profile. The matched
`IpcSmoke` firmware must already be running, the SD card and radio/PDM audio path
must be installed, and no recording may be active. The command does not flash or
reset the target:

```powershell
host/.venv/Scripts/python.exe -m spookybench --json --profile host/bench.local.json test ipc-load --seconds 60
```

`--seconds` must be a whole number from 10 through 600. The total supervisor
budget is the requested duration plus 60 seconds. Only one operation may own the
profile's `board_id` at a time.

## Sequence and verdict

The runner acquires the board lock and records baseline `DIAG STATUS`, `LOG
STATUS`, and `IPC STATUS`. It then opens one target CDC session, waits for a quiet
boundary, and issues `RECORD STATUS`. An active recording returns
`recording_busy`; the runner leaves that recording alone and reports
`human_required=true`.

From an idle state it sends `RECORD START N` and retains the same CDC session for
asynchronous recorder progress. It requests `IPC STATUS` near one-third and
two-thirds of the recording, offset from the recorder's five-second progress
cadence. Both samples must report LINK UP, ABI 1/1, both peer-seen flags, zero
local and peer errors, and forward modulo-32-bit TX, RX, ACK, and ROUNDTRIPS
relative to the preceding sample.

The recording must provide its START, periodic progress, PASS, and final DIAG
records. The filename and requested duration must stay consistent; bytes must
equal frames times six for 48 kHz, 16-bit, three-channel samples; audio duration
must cover the request within one 4096-frame block; elapsed time must be
plausible; and both queue high-water marks must remain below capacity. Too few
progress records, an abort, an overrun, malformed accounting, lost IPC evidence,
or a deadline is a failed test.

After recording, a fresh IPC sample must advance again. Fresh DIAG and LOG status
must have no fault, overrun, audio/SD, logger-loss, transport, or invalid-context
counters. `DIAG LAST` and a complete, gap-free `DIAG DUMP` are archived. A pass
reports `target_health=healthy`, `final_target_state=running`, complete evidence,
and `human_required=false`.

If the runner may have started a recording but cannot finish its evidence, it
sends one bounded `RECORD STOP`. Failed or incomplete cleanup sets
`human_required=true`. The runner never retries, resets, flashes, or stops a
recording that was already active when it began.

## Evidence and limits

The run directory contains `metadata.json`, the exact bench source archive and
hash, profile/device records, `diagnostics.jsonl`, and the authoritative
`test-results.json`. The JSON result includes each IPC snapshot and delta,
recorder progress and high-water summaries, cleanup disposition, baseline/final
health, and parsed diagnostic history. Original device lines are preserved as
base64 in the JSONL evidence.

This test validates target-reported recording accounting and continued
foreground IPC service. It does not copy, decode, listen to, or inspect the WAV
file on the PC, and it does not establish audio quality or channel correctness.
Those remain part of the broader audio-basic acceptance work.

Simulation covers successful execution plus `record-abort`, `record-overrun`,
`record-ipc-stale`, `record-disconnect`, and `record-busy` failure semantics.
Simulation is never hardware acceptance evidence.

## Initial hardware acceptance

The exact version 0.4.0 source passed a 60-second run on 2026-09-24. It produced
`REC004.WAV`, reported 2,883,584 frames and 17,301,504 bytes over 60.074 seconds,
kept recorder queue high-water at 1/8 for both sources, and advanced every IPC
counter in both in-load intervals and after recording. Full measurements and the
run identity are in the
[live bench record](bench-results-2026-09-24.md#phase-3-ipc-progress-under-recording-load).
