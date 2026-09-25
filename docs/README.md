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
- [Development roadmap](../spec/product/roadmap.md): milestones, exit criteria,
  tooling-track rules, and the decisions each milestone waits on.
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
- [Prototype hardware](design/prototype-hardware.md): installed hardware, bodges,
  wiring, clocks and bus devices.
- [USB CLI contract](design/usb-cli.md): target commands, responses and the EMF
  metric definition.
- [Repository layout](design/repository-layout.md): canonical build and generated-file
  ownership.
- [CubeMX reconciliation register](design/cubemx-reconciliation.md): where the `.ioc`
  disagrees with the proven runtime configuration.
- [Diagnostic IPC contract](design/ipc-diagnostic-contract.md): opt-in M7/M4 mailbox
  experiment.
- [Logger and diagnostics](design/logger-diag.md): bounded UART logger and numeric
  history.
- [Spookyprobe v1](design/spookyprobe-v1.md): serial formats and the probe
  capture/decoder contract.
- [Spooky Bench contract](design/spooky-bench-contract.md): CLI results, exit codes,
  budgets, locking, manifests and artifacts.

## Procedures — `procedures/`

Kept in step with the current tooling. Hardware bring-up checks are in
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
[2026-09-23 repository review](history/2026-09-23-repository-review.md),
[2026-09-23 Spooky Bench plan](history/2026-09-23-spooky-bench-plan.md) and
[Spookyprobe work package](history/2026-09-23-spookyprobe-work-package.md). Design history
from before the STM32 pivot is in [`reference/legacy_docs`](../reference/legacy_docs/).
