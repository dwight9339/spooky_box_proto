# Spooky Bench Phase 2: paired boot smoke test

Implemented in version 0.3.0, 2026-09-24. The command combines the accepted
Phase 1B flash/reset path with UART evidence, target CDC rediscovery, M7
diagnostics, and M4 IPC progress under one board lock:

```powershell
host/.venv/Scripts/python.exe -m spookybench --json --profile host/bench.local.json test boot-smoke --manifest build/ipc-build-info.json
```

The manifest must describe a matched `IpcSmoke` CM7/CM4 pair. Hash, ELF,
provenance, and preset checks finish before hardware access. The 240-second
supervisor deadline includes validation, paired programming and verification,
reset/run, CDC recovery, structured requests, UART finalization, and forced
cleanup. A parent-owned Windows job terminates descendant OpenOCD processes if
the worker is interrupted or exceeds that deadline.

The test opens the Pico diagnostic UART before OpenOCD changes target state and
keeps it open through all post-reset checks. It then allows up to 45 seconds for
the target CDC identity to return, including a changed COM port. Each target
request uses a fresh serial session and the established quiet-settling rule.
All received bytes and decoded records are archived.

Pass requires all of the following:

- both ELF images were programmed and verified before reset/run;
- the target CDC identity returned and answered schema-v1 `DIAG STATUS` as core 7;
- diagnostic fault, audio, SD, radio, and PDM error counters were zero;
- two `IPC STATUS` snapshots at least one second apart reported LINK=UP, ABI 1/1,
  both seen flags, zero local/peer errors, and forward modulo-32-bit progress in
  TX, RX, ACK, and ROUNDTRIPS;
- logger loss/error counters were zero;
- `DIAG LAST` parsed and `DIAG DUMP` completed without gaps;
- the probe UART archive contained bytes and reported no host-side drops.

M7 liveness is established by the fresh structured response. M4 liveness is
explicitly labeled as inferred from echo/ack progress. The manifest is supplied
provenance and is not reported as target-asserted identity. Missing READY text
does not fail otherwise valid evidence, while an empty required UART archive
does. Default Debug and `IPC DISABLED` return `ipc_disabled` rather than a false
dual-core pass.

Success leaves the prototype running and does not start a recording. A failed
post-reset health criterion preserves the artifacts and does not retry flash or
reset. If M7 answered after reset, the final state is reported running; failure
before that proof leaves it unknown and sets `human_required`. Partial flash or
verification failure follows the Phase 1B rule: the utility does not resume the
partly updated pair.

Artifacts include the exact staged firmware pair and manifest, pinned OpenOCD
scripts/log, the complete UART archive, every target response in
`diagnostics.jsonl`, stage timings and host-nanosecond capture/tool/reset epoch
markers, IPC snapshots/deltas, and the authoritative
`test-results.json`. Simulation covers CDC absence and COM renumbering, empty
UART evidence, disabled/stale/error IPC, diagnostic faults, and the prior tool,
protocol, storage, disconnect, and process cleanup cases. Simulation is never
hardware acceptance evidence.

The first live invocation passed with complete evidence on 2026-09-24. See the
[bench results](../evidence/2026-09-24-bench-results.md). Repeated-reset and physical
failure-path acceptance remain separate gates.
