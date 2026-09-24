# Spooky Bench host utility

Phase 1A provides Windows serial discovery, bounded Pico UART capture and
LOG/DIAG queries on the separate target CDC port. Run `spookybench --json ...`
for a single machine-readable result. A successful query means the operation
completed, not that the target passed a hardware test.

See [installation and usage](../docs/spooky-bench-setup.md) and the
[implementation plan](../docs/spooky-bench-plan.md).

Tests use simulated devices; no board connection is required:

```powershell
host/.venv/Scripts/python.exe -B -m unittest discover -s host/tests -v
```

The environment must include the pinned Spookyprobe package. The setup procedure
exports its committed source without modifying the sibling repo. Tests do not
depend on that repo remaining present once the package is installed.
