# Spooky Bench: bounded SD scratch test

Requires Spooky Bench 0.6.0 or later. This test writes a dedicated
scratch file, reads every byte back against a deterministic pattern, removes the
file, and checks target health before and after the operation.

## Command and limits

Use an inserted, known card and stop any recording or WAV transfer:

```powershell
host/.venv/Scripts/python.exe -m spookybench --json --profile host/bench.local.json test sd-basic --size-mib 8 --passes 1 --timeout 180
```

The host accepts 1..64 MiB, 1..4 passes, and no more than 128 MiB across
`size-mib * passes`. The target retains its broader manual `SD STRESS` limits,
but the automated command deliberately uses the smaller envelope. The deadline
must be 30..1800 seconds and includes CDC settling, health checks, the test, and
cleanup.

The command requires an idle recorder, numeric free-space evidence at least as
large as the requested file, and no pre-existing `SDTEST.BIN`. It never removes
an existing scratch file. Such a file requires deliberate inspection and manual
`SD CLEAN` before a later run.

## Target execution and cancellation

`SD STRESS` creates only `SDTEST.BIN` with `FA_CREATE_NEW`. Each pass writes a
changing pseudorandom pattern in 16 KiB chunks, syncs it, seeks to the beginning,
and compares every byte. The target performs one filesystem call per foreground
service iteration, so CDC, IPC, recorder, and other foreground services run
between chunks. Recording start and WAV fetch are rejected while this operation
owns the card.

The final response reports exact written and verified bytes, elapsed milliseconds,
maximum individual write/read call times in milliseconds, aggregate read-plus-
write throughput, and `file-removed=1`. The host requires the phase sequence and
all accounting to match its request. It compares type, capacity, and logical
block count before and after the run and requires free space to return within one
reported MiB.

The host reserves eight seconds of its deadline for cleanup. If the work has not
completed, it sends `SD STRESS STOP`. A successful stop closes and removes the
scratch file and reports partial byte counts. The overall result remains
`sd_timeout`, because the requested verification did not finish, while
`cleanup=stopped_and_removed` and `human_required=false` establish bounded
recovery. Transport loss or a target-reported retained file sets
`human_required=true`.

## Evidence and failure coverage

The run archives every CDC line in `diagnostics.jsonl`, the exact bench source,
device/profile identities, and authoritative `test-results.json`. Simulation
covers missing card, pre-existing scratch data, byte mismatch, cleanup failure,
deadline cancellation, disconnect, changed card identity, and active recording.
Simulated success is not hardware acceptance evidence.

## Evidence

[2026-09-24 SD scratch results](../evidence/2026-09-24-bench-results.md#phase-3-bounded-sd-scratch-test),
including a deliberate timeout with verified cleanup.
