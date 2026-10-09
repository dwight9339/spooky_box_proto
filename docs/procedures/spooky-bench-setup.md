# Spooky Bench: Windows setup and observation

Install and configure the `spookybench` CLI on a Windows bench PC, then run discovery,
UART capture and LOG/DIAG queries. Result fields, exit codes, time budgets, locking
and artifact layout are defined in the [Spooky Bench contract](../design/spooky-bench-contract.md).
SWD probe, reset and flash are in the [controls procedure](spooky-bench-controls.md).
No STM32 or Pico firmware change is needed to install the utility.

## Install in this repo

Use Python 3.10+ and Git from PowerShell at the repo root. No activation script or
system-wide Python installation changes are required. Skip the steps whose results
already exist on this PC (`host/.venv`, the probe export).

```powershell
python -m venv host/.venv
host/.venv/Scripts/python.exe host/tools/export_probe.py --repo ../spookyprobe --output build/spookyprobe-ce039ca
host/.venv/Scripts/python.exe -m pip install setuptools wheel pyserial==3.5
host/.venv/Scripts/python.exe -m pip install --no-build-isolation --no-deps ./build/spookyprobe-ce039ca
host/.venv/Scripts/python.exe -m pip install --no-build-isolation --no-deps -e ./host
host/.venv/Scripts/python.exe -m pip check
```

The export command requires a new output directory; reuse an existing verified
export for reinstalling rather than rerunning the export into it. It reads Git
objects at Spookyprobe commit `ce039cab6d15171aa069991dd743e1e14e64aeef`, verifies
their hashes, and does not use or modify the sibling's working tree. Once
installed, runtime needs no sibling checkout. Update the pin deliberately, with
adapter and regression tests, when changing it.

## Discover and configure

```powershell
host/.venv/Scripts/python.exe -m spookybench --json status
Copy-Item host/examples/bench.windows.example.json host/bench.local.json
```

The first command lists COM ports without opening them. Edit `host/bench.local.json`
to use the reported USB serial numbers. The probe is normally VID 11914/PID 12;
the target application CDC is VID 1155/PID 22336. Match serial numbers as well:
these VID/PID pairs alone do not identify your individual boards. `interface` can be matched
where the OS supplies it; it may be null on Windows.

Set `artifact_root` to an absolute local directory outside the source checkout.
`%LOCALAPPDATA%/SpookyBench/runs` is the example default. All profiles for the same
board must share `board_id`, regardless of artifact root. The local profile is
ignored by Git. Python's [Windows serial discovery](https://pyserial.readthedocs.io/en/stable/tools.html)
supplies the device metadata; Spooky Bench adds selection and ambiguity checks.

## Power up and down

Switch the battery on before connecting either USB cable (the target's or the Spooky
Probe's), and unplug both cables before switching it off. With a cable connected and
the battery switch off, the rig boots on limited USB power. Switching the battery on
during that boot hung an SD write and left the card unresponsive until it was reseated
([2026-10-08 trials](../evidence/2026-10-08-sd-power-up-order.md)).

## Run

```powershell
host/.venv/Scripts/python.exe -m spookybench --json --profile host/bench.local.json status
host/.venv/Scripts/python.exe -m spookybench --json --profile host/bench.local.json console --seconds 60
host/.venv/Scripts/python.exe -m spookybench --json --profile host/bench.local.json log status
host/.venv/Scripts/python.exe -m spookybench --json --profile host/bench.local.json diag status
host/.venv/Scripts/python.exe -m spookybench --json --profile host/bench.local.json diag last
host/.venv/Scripts/python.exe -m spookybench --json --profile host/bench.local.json diag dump
host/.venv/Scripts/python.exe -m spookybench --json --profile host/bench.local.json test boot-smoke --manifest build/ipc-build-info.json
host/.venv/Scripts/python.exe -m spookybench --json --profile host/bench.local.json test ipc-load --seconds 60
host/.venv/Scripts/python.exe -m spookybench --json --profile host/bench.local.json test sd-basic --size-mib 8 --passes 1 --timeout 180
host/.venv/Scripts/python.exe -m spookybench --json --profile host/bench.local.json wav inspect --file REC004.WAV --timeout 600
```

- Put global options before the command.
- Stop a running console capture before starting a separate bench invocation.
- Close other terminal applications before opening their COM ports here.
- Stop sensor/UI streams on the target if they prevent the diagnostic quiet interval.
- Keep the PC awake during active tests.
- Treat a run as incomplete unless its `test-results.json` exists; see
  [run artifacts](../design/spooky-bench-contract.md#run-artifacts).

## Simulation and tests

```powershell
host/.venv/Scripts/python.exe -m spookybench --json --simulate status
host/.venv/Scripts/python.exe -m spookybench --json --simulate --profile host/examples/bench.simulated.json diag dump
host/.venv/Scripts/python.exe -m spookybench --json --simulate --scenario incomplete --profile host/examples/bench.simulated.json diag dump
host/.venv/Scripts/python.exe -B -m unittest discover -s host/tests -v
```

Simulation uses virtual time and fake device identities, writes real bounded
artifacts, and never opens hardware ports. Scenarios include happy, discovery and
transport failures, malformed diagnostics, recording/IPC failures, WAV corruption,
and SD absence, retained data, mismatch, cleanup, timeout, disconnect, card change,
and recording-busy cases. A happy console simulation supplies one raw byte burst
rather than a real-time load. Tests also inject queue loss, disk errors,
quota/free-space failures, changed COM numbers, role collisions, lock contention,
and a hung worker. The worker-timeout test uses a real spawned process and checks
that termination releases its lock. No MCU timing, physical disconnect behavior, or
electrical acceptance is inferred from simulation or tests.

## Live acceptance

Use the working SYSOFF/power arrangement recorded in the
[2026-09-23 bench notes](../evidence/2026-09-23-bench-results.md). With the battery
switched on, connect both USB paths and:

1. Verify no-profile status lists both identities, then configure and run status.
2. Run a short console capture during known target logging. Inspect raw bytes and
   session/index files; a silent capture can complete but proves no target health.
3. Close capture and run LOG STATUS, DIAG STATUS, DIAG LAST and DIAG DUMP. Compare
   saved responses with manual terminal results; require a complete dump.
4. Run a bounded capture and unplug the probe; require a visible failure and
   usable incomplete evidence. Reconnect and start a new invocation.
5. Disconnect target CDC during a query; require error/incomplete evidence. After
   reconnect and the target timeout, a new query should work. No automatic reset.
6. Verify a second bench operation reports `bench_busy` and that Ctrl+C/timeout
   releases ports and locks.

Further acceptance cases (mid-response disconnect, COM renumbering, deliberate
transport overload) are tracked in Beads under `full_spooky_proto-jjy`. Then
validate the [controls](spooky-bench-controls.md) and run the
[boot smoke](spooky-bench-boot-smoke.md). Evidence:
[2026-09-24 bench results](../evidence/2026-09-24-bench-results.md).
