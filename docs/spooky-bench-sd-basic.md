# Spooky Bench: bounded SD scratch test

Implemented in version 0.6.0, 2026-09-24. This Phase 3 slice writes a dedicated
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

## Initial hardware results

Build `sd-basic-20260924-01` used dirty firmware snapshot SHA-256
`ca17e9176b848d2f0cc5ba99b3f54ee99044c1a687615bdf373f11254cdf21c3`,
CM7 SHA-256 `2cd3a923b90d3b5596d563db9a19b0f26f6effa2c92efac268fb757a5d75a375`,
and CM4 SHA-256 `3e92497cd71b3f827dd933488ea18e64ac6b9944e2d968fb2497099ed896b593`.
The paired boot-smoke gate passed before storage testing.

Run `2026-09-24T232713.310059_0000-04e1521a` wrote and verified 8,388,608
bytes in 27,725 ms. Maximum 16 KiB calls were 15 ms for writes and 7 ms for
reads. The 59,344 MiB SDHC/SDXC card retained the same 121,536,512-block
identity and 59,152 MiB reported free space. Scratch cleanup, diagnostics, and
logger checks passed.

Run `2026-09-24T232853.955391_0000-43ee7515` deliberately timed out a 64 MiB
request. It stopped after writing 11,993,088 bytes, removed the scratch file, and
reported no need for human recovery. Follow-up run
`2026-09-24T232941.048402_0000-cb0576a4` then wrote and verified 1,048,576 bytes,
confirming that no stale file or damaged ownership remained. Full records are in
the [live bench results](bench-results-2026-09-24.md#phase-3-bounded-sd-scratch-test).
