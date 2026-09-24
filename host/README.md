# Spooky Bench host utility

Phase 1A provides Windows serial discovery, bounded Pico UART capture and
LOG/DIAG queries on the separate target CDC port. Phase 1B adds pinned Windows
OpenOCD probe/reset and manifest-based paired flashing with integrated UART
capture. Phase 2 adds a supervised `test boot-smoke` verdict for an IpcSmoke
pair. Phase 3 adds a supervised `test ipc-load` recording workload,
`wav inspect` for CRC-verified retrieval and local three-channel analysis, and
`test sd-basic` for bounded scratch-file write/read verification with cleanup. See
[controls](../docs/spooky-bench-controls.md),
[boot smoke](../docs/spooky-bench-boot-smoke.md), and
[IPC load](../docs/spooky-bench-ipc-load.md), and
[WAV inspection](../docs/spooky-bench-wav-inspection.md), and
[SD basic](../docs/spooky-bench-sd-basic.md). Run
`spookybench --json ...` for one machine-readable result.

See [installation and usage](../docs/spooky-bench-setup.md) and the
[implementation plan](../docs/spooky-bench-plan.md).

Tests use simulated devices; no board connection is required:

```powershell
host/.venv/Scripts/python.exe -B -m unittest discover -s host/tests -v
```

The environment must include the pinned Spookyprobe package. The setup procedure
exports its committed source without modifying the sibling repo. Tests do not
depend on that repo remaining present once the package is installed.
