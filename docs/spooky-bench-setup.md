# Spooky Bench: Windows setup and observation

Implemented 2026-09-24. The utility provides serial discovery/status, finite UART
capture, and LOG/DIAG requests. Version 0.2.0 also
implements [Phase 1B SWD probe, reset and paired flash](spooky-bench-controls.md).
Version 0.3.0 adds the [paired boot smoke](spooky-bench-boot-smoke.md), and
version 0.4.0 adds the supervised
[IPC recording-load test](spooky-bench-ipc-load.md). Power, trace, and crash
collection still return `unsupported` and exit 3. No STM32 or Pico firmware
changes are needed to install the utility.

Software tests run on Windows with spawned workers and fake serial transports.
Configured discovery, all four LOG/DIAG command paths, and UART startup capture
have passed an initial live check, including a complete dump and raw/index/session
inspection. Probe unplug also produced a prompt error with usable incomplete
evidence, followed by successful capture after reconnect; see
[live results](bench-results-2026-09-24.md). Target CDC disconnect during initial
synchronization and a query after reconnect also passed. Concurrent-operation
rejection and Ctrl+C termination behaved as expected, and subsequent diagnostic
and console runs confirmed lock and port release. Basic live checks are complete;
mid-response disconnect, COM renumbering, and deliberate transport-overload
testing still require bench acceptance. A 60-second recording-load test has
passed with IPC progress throughout. Enumeration
alone does not establish a working target.

## Install in this repo

Use Python 3.10+ and Git from PowerShell at the repo root. No activation script
or system-wide Python installation changes are required. On this development
PC, `host/.venv` has already been created and these packages installed.

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
installed, runtime needs no sibling checkout. `probe-lock.json` checks the five
installed package modules on each operation, normalizing CRLF/LF. A different
source snapshot fails explicitly, even if its package version remains 0.1.0.
Update the pin deliberately with adapter/regression tests when changing it.

An editable bench install is convenient for development. Every configured run
archives the bench module sources and their combined hash; it does not label
dirty files as a clean Git revision. Installed probe sources are identified by
the verified commit. Firmware identities remain unknown in Phase 1A.

## Discover and configure

```powershell
host/.venv/Scripts/python.exe -m spookybench --json status
Copy-Item host/examples/bench.windows.example.json host/bench.local.json
```

The first command lists COM ports without opening them. Edit `host/bench.local.json`
to use the reported USB serial numbers. The probe is normally VID 11914/PID 12;
the target application CDC is VID 1155/PID 22336. Match serial numbers as well:
these VID/PID pairs alone do not identify your individual boards. The Nucleo
ST-LINK VCP is not the target application CDC port. `interface` can be matched
where the OS supplies it; it may be null on Windows.

Alternatively a selector can be `{"port":"COM9"}` for a controlled setup.
COM names can change; an identity-based selector discovers the current port on
each new standalone observation. Boot smoke also rediscovers it after reset.
Missing/ambiguous matches and probe/device role collisions are explicit errors.
Status with a profile requires both devices; console requires the probe;
diagnostic commands require the target. Boot smoke initially requires the probe
and rediscovers the target identity after reset. No command chooses the first available
COM port or silently ignores a supplied selector field.

Set `artifact_root` to an absolute local directory outside the source checkout.
`%LOCALAPPDATA%/SpookyBench/runs` is the example default. All profiles for the same
board must share `board_id`, regardless of artifact root. The local profile is
ignored by Git. Python's [Windows serial discovery](https://pyserial.readthedocs.io/en/stable/tools.html)
supplies the device metadata; Spooky Bench adds selection and ambiguity checks.

```powershell
host/.venv/Scripts/python.exe -m spookybench --json --profile host/bench.local.json status
host/.venv/Scripts/python.exe -m spookybench --json --profile host/bench.local.json console --seconds 60
host/.venv/Scripts/python.exe -m spookybench --json --profile host/bench.local.json log status
host/.venv/Scripts/python.exe -m spookybench --json --profile host/bench.local.json diag status
host/.venv/Scripts/python.exe -m spookybench --json --profile host/bench.local.json diag last
host/.venv/Scripts/python.exe -m spookybench --json --profile host/bench.local.json diag dump
host/.venv/Scripts/python.exe -m spookybench --json --profile host/bench.local.json test boot-smoke --manifest build/ipc-build-info.json
host/.venv/Scripts/python.exe -m spookybench --json --profile host/bench.local.json test ipc-load --seconds 60
```

Put global options before the command. Only one operation may own a board at a
time, including console capture. Stop console before running a separate bench
diagnostic invocation. Boot smoke owns capture, controls, and diagnostics under
one lock. IPC load owns one target CDC session for recording progress and its
in-load IPC requests, then uses bounded diagnostic sessions before and after it.
Close other terminal applications before opening their COM ports here;
Spooky Bench does not terminate unrelated applications or steal their handles.

Console is receive-only at 115200 8N1, without flow control. It saves arbitrary
bytes, including invalid UTF-8 and partial lines. It never sends a command to
the Pico UART bridge. Diagnostic commands use the separate target CDC, wait
at least 5.2 seconds plus a quiet interval to discard no evidence but avoid
stale replies, and send exactly one whitelisted command. Boot smoke sequences
these one-request sessions internally while retaining the board lock. Stop sensor/UI streams
if they prevent the initial quiet interval. Original diagnostic bytes are saved
as base64 alongside the existing Spookyprobe decoder output. Missing END stays
incomplete; an error ends the session. No retry, automatic reset, or reconnect.

## Limits and results

`--json` produces exactly one result object, including invocation errors. Help
remains ordinary CLI help. Fields are `schema_version`, `command`, `result`,
`reason`, `execution`, `metrics`, `artifacts`, and `timestamps`; errors also have
bounded `detail`. `execution=simulated` is never hardware acceptance evidence.
Successful simulations carry `reason=simulated`; simulated failures preserve
their failure reason. Query success does not mean HAS_FAULT=0: inspect the
returned response. Target health and final target state remain explicitly
not_checked/unknown instead of inferred from serial enumeration or silence.

| Exit | Meaning |
| --- | --- |
| 0 | Requested operation completed (not a target-health verdict) |
| 1 | Invalid protocol response, OpenOCD/verify failure, capture loss, or evidence cap reached |
| 2 | Invocation/configuration, dependency, missing/ambiguous device, port, storage, or timeout error |
| 3 | Unsupported operation |
| 130 | Interrupted |

Status has a 10-second total budget; diagnostics 15 seconds including settling,
request and cleanup. A request gets at most 8 seconds, clipped to the remaining
budget. Console requires finite `0 < seconds <= 3600`; its total budget is that
duration plus 12 seconds for startup and cleanup. Serial reads have 100 ms and
writes 1-second timeouts. Two seconds of each total are reserved for forced
termination/reaping if needed. Capture archival uses the existing bounded
64 x 4096-byte queue in the supervised process; a stuck writer cannot hold the
supervisor waiting on Archive.close indefinitely.

IPC load accepts a whole number of seconds from 10 through 600 and has that
duration plus 60 seconds for setup, health queries, result collection, and
cleanup. It refuses to disturb an already active recording. If a recording that
it started fails or loses evidence, it issues one bounded `RECORD STOP`; an
incomplete cleanup is reported with `human_required=true`.

Worker supervision uses Windows-compatible `spawn`, bounded shared result memory,
and terminate/kill/reap rather than unbounded thread joins in the CLI process.
Python documents that [forced process termination skips cleanup](https://docs.python.org/3/library/multiprocessing.html#multiprocessing.Process.terminate).
On forced termination, the CLI reports incomplete evidence and any known run
directory; it does not attempt further writes to a possibly stuck filesystem.
`metadata.json` alone is not a completed run. Missing `test-results.json`, a
temporary result file, or a supervisor timeout requires treating the run as
incomplete. A killed capture may also lack session metadata or contain an
unindexed raw tail. OS failure to terminate is reported HUMAN_REQUIRED via
`metrics.human_required`; normal deadlines are not a guarantee against a hung OS.

The per-user OS lock is released when its process exits. Its stale file is
harmless. A second lock serializes quota accounting for a shared artifact root.
External tools do not participate in those cooperative locks. Artifact roots
must be private to this tool/user: symlinks/junctions are rejected, but these
checks are not a security boundary against another process changing directories
during a run.

Default storage policy: 256 MiB/run reserved at admission, 2 GiB/root total,
512 MiB free-space reserve, 64 MiB UART bytes and 8 MiB diagnostic JSONL.
Unknown and incomplete files count toward quota. Index rows and source/metadata
also count; UART reservations are conservative, so a run can stop below its raw
byte cap. Limits are configurable in the profile (`run_bytes`, `total_bytes`,
`min_free_bytes`, `uart_bytes`, `diag_bytes`). No automatic deletion occurs.
Other processes can still consume free disk after admission; resulting write
errors are surfaced. Memory, streams, discovery inventory, and result sizes
have independent bounds.

A configured run contains:

```text
<artifact_root>/<timestamp-id>/
  metadata.json, profile.json, devices.json
  bench-source.json             # exact normalized runner module sources
  test-results.json             # authoritative completed operation result
  uart/raw.bin, chunks.jsonl, session.json    # console only, under uart/
  diagnostics.jsonl             # diagnostic query only
```

No-profile discovery returns its evidence only in the CLI result. OpenOCD is
reported not_checked for serial-only operations; optional PyUSB probe counters
remain unsupported. Neither blocks serial operations. Phase 1B archives supplied
build manifests separately from target-reported firmware identity, which is
still unavailable.

## Simulation and tests

```powershell
host/.venv/Scripts/python.exe -m spookybench --json --simulate status
host/.venv/Scripts/python.exe -m spookybench --json --simulate --profile host/examples/bench.simulated.json diag dump
host/.venv/Scripts/python.exe -m spookybench --json --simulate --scenario incomplete --profile host/examples/bench.simulated.json diag dump
host/.venv/Scripts/python.exe -B -m unittest discover -s host/tests -v
```

Simulation uses virtual time and fake device identities, writes real bounded
artifacts, and never opens hardware ports. Scenarios include happy, missing,
ambiguous, disconnect, incomplete dump, invalid schema, and no response. A happy
console simulation supplies one raw byte burst rather than a real-time load.
Tests also inject queue loss, disk errors, quota/free-space failures, changed COM
numbers, role collisions, lock contention, and a hung worker. The worker-timeout
test uses a real spawned process and checks that termination releases its lock.
No MCU timing, physical disconnect behavior, or electrical acceptance is inferred.

## First live acceptance

Use the working SYSOFF/power arrangement recorded in the
[bench notes](bench-results-2026-09-23.md). Connect both USB paths and:

1. Verify no-profile status lists both identities, then configure and run status.
2. Run a short console capture during known target logging. Inspect raw bytes and
   session/index files; a silent capture can complete but proves no target health.
3. Close capture and run LOG STATUS, DIAG STATUS, DIAG LAST and DIAG DUMP. Compare
   saved responses with manual terminal results; require a complete dump.
4. Run a bounded capture and unplug the probe; require a visible failure and
   usable incomplete evidence. Reconnect and start a new invocation.
5. Disconnect target CDC during a query; require error/incomplete evidence. After
   reconnect and the target timeout, a new query should work. No automatic reset.
6. Verify a second bench operation reports bench_busy and that Ctrl+C/timeout
   releases ports and locks. Keep the PC awake for active tests.

After Phase 1A acceptance, validate the implemented
[Phase 1B controls](spooky-bench-controls.md), then run the
[combined capture + IPC boot smoke](spooky-bench-boot-smoke.md). Linux/Pi
deployment remains later work; its fallback locking path has not been bench-tested.
