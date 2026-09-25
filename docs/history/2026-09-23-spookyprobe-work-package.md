# Spookyprobe integration work package

> **Frozen historical snapshot (2026-09-25).** Extracted from
> [Spookyprobe v1](../design/spookyprobe-v1.md), where it was the original plan for the
> parallel device and probe work. It is not maintained. Current status is in Beads;
> the contract itself remains in the design document.

The capture/decoder integration below is the original work package, not a new
implementation backlog. The pinned host dependency and Windows Spooky Bench
now exercise these paths; see [setup](../procedures/spooky-bench-setup.md) and the
[September 24 live results](../evidence/2026-09-24-bench-results.md). Remaining sustained-load,
loss-accounting and mid-response failure checks are tracked in Beads under
`full_spooky_proto-jjy`. Probe firmware identity and trace resources remain
separate work under `full_spooky_proto-5yv`.

1. Inspect the probe repo's existing UART bridge and host tooling. Preserve the
   current debug/probe functions while adding bounded capture and explicit probe
   overflow accounting where supported by its architecture.
2. Implement raw log capture with host timestamps, reconnect/session boundaries,
   and bounded buffering. Preserve raw bytes even when text decoding fails.
3. Implement a separate device CDC client for LOG STATUS and DIAG STATUS/LAST/DUMP.
   Parse incremental lines and retain raw lines alongside structured records.
4. Add a dump reader/summary that handles interleaved text, GAP, missing END,
   sequence/tick wrap, reset, unknown fields/events, unsupported versions, and
   timeouts. Do not label host timestamps as MCU timestamps or infer UART loss
   from diagnostic sequence gaps.
5. Test offline using transcript fixtures and a fake serial transport, including
   arbitrary read splits, disconnect mid-line/dump, overload, and delayed input.

Keep the first shared milestone small: offline capture and decoding tests in
Spookyprobe, host and firmware build tests here, then one bench session to verify
both serial paths and compare device/probe/host loss counters under load.
