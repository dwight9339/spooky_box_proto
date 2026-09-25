# Spooky Bench Phase 1B: Windows SWD controls

Implemented in version 0.2.0, 2026-09-24. Phase 1A observation commands remain
available. `probe`, `reset`, and `flash --manifest` use the pinned Windows
OpenOCD installation. The combined automated verdict is documented in the
[Phase 2 boot smoke guide](spooky-bench-boot-smoke.md).

## Setup

The local editable install and ignored `host/bench.local.json` have been updated
on this PC. For another bench, add the `openocd` section from
[the control profile example](../../host/examples/bench.windows.control.example.json)
to the existing observation profile. Both serial fields must name the same Pico.
Keep the same board_id and artifact root across profiles for that board.

Install PlatformIO's `platformio/tool-openocd@3.1200.0`. The executable reports
`xPack Open On-Chip Debugger 0.12.0-01004-g9ea7f3d64-dirty (2023-01-30-15:04)`.
`host/spookybench/openocd-lock.json` pins the executable, bundled DLLs, and four
required Tcl files. Drift fails before attaching. Scripts are copied into the
run directory after verification; tool identity and hashes are recorded.
This package-specific pin must be deliberately updated/tested for other builds.
Serial observations continue working without OpenOCD configuration.

The profile accepts only executable/scripts paths, serial_number and adapter_khz
(integer 50..4000; use 1000 initially). Paths are separate process arguments,
never interpolated into Tcl. Firmware files are staged as fixed names. Backend
is CMSIS-DAP USB bulk; GDB, Tcl and telnet listeners are disabled. There is no
arbitrary command/config-file escape hatch. `status` reports configured control
capabilities but does not attach or verify OpenOCD; tool checks run on controls.

## Probe and reset

```powershell
host/.venv/Scripts/python.exe -m spookybench --json --profile host/bench.local.json probe
host/.venv/Scripts/python.exe -m spookybench --json --profile host/bench.local.json reset
```

`probe` has a 15-second total bound. It examines M7/AP0 and attempts M4/AP3
without requesting halt/reset. AP2 is deferred and AP2-dependent examination
hooks are removed, matching the existing deployment workaround. An unavailable
M4 is explicit; sleep, boot gating and access failure cannot be distinguished
by this observation. A passing probe means M7 was observable, not both cores
are healthy. The selected Pico serial is passed to OpenOCD explicitly.

`reset` has a 15-second total bound and issues system reset/run through M7/AP0,
using the board script's existing SYSRESETREQ configuration. Stop recording
before using it. Reset/flash open receive-only Pico UART capture first, wait for
archive readiness, then keep capture active through the tool operation plus
up to two seconds afterward. This tail is bounded and is not a complete boot
test. A separate `console` invocation must not run concurrently.

Controls require the Pico identity, not an already-enumerated target CDC. The
target port is rediscovered on the next diagnostic invocation. A successful
reset means the reset command completed. `control.final_cm7_state` is the
OpenOCD observation; M4 and aggregate `final_target_state` remain unknown when
not observed. Firmware boot/IPC progress must be checked separately.

The configuration and state reporting use OpenOCD's documented
[target examination/state commands](https://openocd.org/doc/html/CPU-Configuration.html)
and [reset commands](https://openocd.org/doc/html/General-Commands.html), checked
against the actual pinned interpreter as well as offline tests.

## Prepare and flash a declared build pair

First build both cores from the same source snapshot/preset. The bench utility
consumes prebuilt files and does not invoke a compiler. Preserve that build's
revision, dirty snapshot if applicable, compiler identity and flags. Do not
substitute current Git HEAD for the provenance of older ELF files.

Create a manifest (replace the placeholders with the image build's values):

```powershell
host/.venv/Scripts/python.exe host/tools/make_manifest.py --cm7 build/IpcSmoke/firmware/CM7/full_spooky_proto_CM7.elf --cm4 build/IpcSmoke/firmware/CM4/full_spooky_proto_CM4.elf --preset IpcSmoke --source-revision FULL_40_CHARACTER_BUILD_SHA --build-id UNIQUE_BUILD_ID --output build/ipc-build-info.json
```

For a dirty build, add `--source-snapshot PATH_TO_ARCHIVED_PATCH_OR_SOURCE`;
the manifest stores its SHA-256 identifier. Keep that source archive with the
build evidence. Optional `--compiler` and repeated `--build-flag=...` record
toolchain provenance; omitted values are explicitly null, not guessed.
The generator refuses to overwrite an output manifest.

```powershell
host/.venv/Scripts/python.exe -m spookybench --json --profile host/bench.local.json flash --manifest build/ipc-build-info.json
```

The 120-second total includes preflight, UART capture, tool execution and cleanup.
Only manifest-based paired flashing is accepted. The originally proposed bare
`flash M7.elf --cm4 M4.elf` shorthand is omitted because it carries no shared
build declaration. Single images, raw BIN/address input and automatic retries
are not supported.

The manifest declares schema_version=1, build_id, preset, source_revision,
dirty, source_snapshot_sha256, and exactly CM7/CM4 images with path and sha256.
Image paths resolve relative to the manifest. Each ELF is limited to 16 MiB;
it must be ELF32 little-endian ARM executable with valid program headers,
nonoverlapping file-backed LOAD ranges inside its assigned 1 MiB flash bank,
and expected stack/reset vectors. Zero-file-size RAM sections are not flashed.
All hashes and ELF checks finish before hardware access, and the exact checked
bytes are staged under the run's firmware directory.

The manifest is a supplied build-provenance declaration, not cryptographic proof
that the cores are semantically compatible. No target-reported firmware identity
exists yet. Firmware hashes, declared provenance and load ranges are separate
from target health in the result.

OpenOCD resets/halts M7, writes and verifies CM7, then writes and verifies CM4.
It reaches reset/run only after both verifications succeed. Programming uses
M7/AP0 for both banks to avoid waiting for a sleeping M4. This is not a claim
that both cores can be halted/observed through AP0. On a Tcl failure, it attempts
M7 halt without another reset; it never intentionally resumes a partly updated
pair. Failure/timeout/interruption after a control attempt reports unknown paired
state and human_required. Inspect the evidence before deciding how to recover.

## Process and evidence guarantees

Each control worker is assigned to a parent-owned Windows kill-on-close job
before it can start OpenOCD. Forced worker termination or parent exit therefore
also terminates descendants. Unsupported job assignment refuses the operation.
Windows is the only supported Phase 1B control platform in this implementation.

OpenOCD uses argv without a shell, hidden process creation, merged raw stdout/
stderr capped at 1 MiB, and a 16 x 4096-byte queue with pipe backpressure.
Absolute deadlines bound the tool, including output draining. Two seconds of
the command total remain reserved for supervisor termination/reaping. Real
process-tree teardown is tested offline; OS failures are not timing guarantees.

Artifacts add `operation.cfg`, staged `scripts/`, `openocd-command.json`, and
`openocd.log`; reset/flash also include `uart/`. Flash adds `build-info.json`
and exact `firmware/CM7.elf`, `firmware/CM4.elf`. The run quota is reserved before
control; images and the full tool-log allowance must fit before tool launch.
No automatic artifact deletion occurs. Missing final result/session metadata
after forced termination continues to mean incomplete evidence.

Simulation runs the same manifest/ELF preflight and writes synthetic tool logs,
without opening hardware. Additional scenarios are `m4-unavailable`,
`tool-failure`, `tool-timeout`, and `partial-flash`. Integrated serial capture is
tested separately with a fake transport; simulated control logs are never live
acceptance evidence.

## Validation and remaining bench work

The current 51-test suite passes, including all prior Phase 1A cases, hostile path/serial
handling, malformed ELF/hash/provenance rejection, partial verification failure,
short-output bursts, timeouts, disk failures, integrated UART readiness, spawned
controls, and real Windows descendant termination. The installed OpenOCD parses
the generated configuration without attachment. A Tcl harness with mocked target
commands verifies that CM4 verification failure does not reach reset/run.
Existing ELFs for all four presets pass structural checks; their build provenance
was not inferred or promoted to a flash manifest.

Live probe, reset/capture/CDC recovery, and fresh paired flash/verify now pass;
see [bench results](../evidence/2026-09-24-bench-results.md). The post-flash checks include
two valid IPC status snapshots with forward TX/RX/ACK/ROUNDTRIPS progress and no
errors, plus clean logger counters and a complete diagnostic dump.

The initial Phase-1B live gate is complete. Remaining extended work includes
sleeping-M4 variants, direct M4 observation where possible, sustained traffic,
and real failure recovery. Do not
deliberately interrupt real flash merely to test cleanup on this prototype;
partial-flash control flow is covered offline.
Phase 2 boot-smoke automation now builds on this accepted flash/reset path.
