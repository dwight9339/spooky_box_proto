# Documentation index

Every document belongs to one class, which sets where it lives and when it may change.
The rules are in the Documentation Classes section of the
[constitution](../spec/.specify/memory/constitution.md). Status is tracked in Beads, not
in these documents.

## Protected (change only by approved proposal)

- [Constitution](../spec/.specify/memory/constitution.md): principles, documentation
  classes, governance.
- [Product philosophy](../spec/product/product-philosophy.md): what the instrument should
  feel like, and the stable meaning of its signals.
- [Modes and interaction](../spec/product/modes-and-interaction.md) and the
  [control map](../spec/product/spooky_box_control_map.xlsx): mode hierarchy, controls
  and open product decisions.
- Feature specs, once created, live in `spec/specs/`.

## Decisions — [`decisions/`](decisions/README.md)

Accepted records are immutable; a changed decision gets a new record that supersedes
the old one.

- [0001 Initial UI and bus ownership](decisions/0001-initial-ui-and-bus-ownership.md)
- [0002 Windows-first Spooky Bench host](decisions/0002-windows-first-spooky-bench.md)

## Design and contracts — `design/`

Updated in the same change as the code they describe.

- [Architecture](design/architecture.md): proven path, responsibility boundaries,
  application model, reliability rules.
- [Repository layout](design/repository-layout.md): canonical build and generated-file
  ownership.
- [Diagnostic IPC contract](design/ipc-diagnostic-contract.md): opt-in M7/M4 mailbox
  experiment.
- [Logger and diagnostics](design/logger-diag.md): bounded UART logger and numeric
  history.
- [Spookyprobe v1](design/spookyprobe-v1.md): serial formats and the probe
  capture/decoder contract.

## Procedures — `procedures/`

Kept in step with the current tooling. Hardware bring-up is in
[BRINGUP.md](../BRINGUP.md).

- [Spooky Bench setup](procedures/spooky-bench-setup.md) and
  [controls](procedures/spooky-bench-controls.md)
- [Boot smoke](procedures/spooky-bench-boot-smoke.md),
  [IPC recording load](procedures/spooky-bench-ipc-load.md),
  [SD basic](procedures/spooky-bench-sd-basic.md),
  [WAV inspection](procedures/spooky-bench-wav-inspection.md)
- [IPC smoke test](procedures/ipc-smoke-test.md)
- [Logger and diagnostics bench](procedures/logger-diag-bench.md)

## Evidence — `evidence/`

Dated and immutable once the session ends. Corrections are added as dated addenda.
Files are named `YYYY-MM-DD-topic.md`.

## History — `history/`

Frozen snapshots of superseded reviews and plans:
[2026-09-23 repository review](history/2026-09-23-repository-review.md) and
[2026-09-23 Spooky Bench plan](history/2026-09-23-spooky-bench-plan.md). Design history
from before the STM32 pivot is in [`reference/legacy_docs`](../reference/legacy_docs/).
