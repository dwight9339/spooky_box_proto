# Spooky Bench: Windows SWD controls

Configure OpenOCD for Spooky Bench, then probe, reset and flash a declared M7/M4 image
pair. What each command guarantees and reports (control semantics, manifest schema,
process supervision, artifacts) is defined in the
[Spooky Bench contract](../design/spooky-bench-contract.md). Complete the
[setup procedure](spooky-bench-setup.md) first. The combined automated verdict is the
[boot smoke](spooky-bench-boot-smoke.md).

## Setup

For each bench, add the `openocd` section from
[the control profile example](../../host/examples/bench.windows.control.example.json)
to the existing observation profile. Both serial fields must name the same Pico.
Keep the same board_id and artifact root across profiles for that board. Start with
`adapter_khz` 1000.

Install PlatformIO's `platformio/tool-openocd@3.1200.0`. The executable reports
`xPack Open On-Chip Debugger 0.12.0-01004-g9ea7f3d64-dirty (2023-01-30-15:04)`, which
matches `host/spookybench/openocd-lock.json`. A different build fails before attaching;
update and test that pin deliberately before using another OpenOCD package. Serial
observations keep working without OpenOCD configuration. `status` reports configured
control capabilities but does not attach or verify OpenOCD.

## Probe and reset

```powershell
host/.venv/Scripts/python.exe -m spookybench --json --profile host/bench.local.json probe
host/.venv/Scripts/python.exe -m spookybench --json --profile host/bench.local.json reset
```

- Stop any recording before `reset`.
- Do not run a separate `console` invocation concurrently; reset and flash capture
  UART themselves.
- A passing probe means M7 was observable, not that both cores are healthy. Check
  firmware boot and IPC progress separately, for example with the boot smoke.
- The target CDC port is rediscovered on the next diagnostic invocation.

## Prepare and flash a declared build pair

First build both cores from the same source snapshot/preset. The bench utility
consumes prebuilt files and does not invoke a compiler. Preserve that build's
revision, dirty snapshot if applicable, compiler identity and flags. Do not
substitute current Git HEAD for the provenance of older ELF files.

Choose one build token and pass it to CMake before building. Use the same token in
the manifest; quoting the complete `-D` argument is required for reliable PowerShell
argument handling. A default build reports `unidentified` and remains useful for
local development, but the manifest generator rejects it because it cannot detect a
stale image. Evidence builds therefore require an explicit unique token:

```powershell
cmake --preset IpcSmoke "-DSPOOKY_BUILD_ID=UNIQUE_BUILD_ID"
cmake --build --preset IpcSmoke
```

Create a manifest (replace the placeholders with the image build's values):

```powershell
host/.venv/Scripts/python.exe host/tools/make_manifest.py --cm7 build/IpcSmoke/firmware/CM7/full_spooky_proto_CM7.elf --cm4 build/IpcSmoke/firmware/CM4/full_spooky_proto_CM4.elf --preset IpcSmoke --source-revision FULL_40_CHARACTER_BUILD_SHA --build-id UNIQUE_BUILD_ID --output build/ipc-build-info.json
```

For a dirty build, add `--source-snapshot PATH_TO_ARCHIVED_PATCH_OR_SOURCE`;
the generator copies those bytes beside the manifest, stores their SHA-256 identifier
and relative path, and the bench run stages the verified copy with its evidence.
Optional `--compiler` and repeated `--build-flag=...` record toolchain provenance;
omitted values are explicitly null, not guessed. The generator refuses to overwrite
the manifest or its source-snapshot companion.

```powershell
host/.venv/Scripts/python.exe -m spookybench --json --profile host/bench.local.json flash --manifest build/ipc-build-info.json
```

## Recovery

A failure, timeout or interruption after a control attempt reports unknown paired
state and `human_required`. The utility never resumes a partly updated pair. Inspect
the run's evidence before deciding how to recover. Do not deliberately interrupt a
real flash merely to test cleanup on this prototype; partial-flash control flow is
covered offline.

## Simulation

Simulation runs the same manifest/ELF preflight and writes synthetic tool logs,
without opening hardware. Additional scenarios are `m4-unavailable`,
`tool-failure`, `tool-timeout`, and `partial-flash`. Integrated serial capture is
tested separately with a fake transport; simulated control logs are never live
acceptance evidence.

Evidence: [2026-09-24 bench results](../evidence/2026-09-24-bench-results.md).
Remaining control qualification (sleeping-M4 variants, direct M4 observation,
sustained traffic, real failure recovery) is tracked in Beads under
`full_spooky_proto-jjy` and `full_spooky_proto-5yv`.
