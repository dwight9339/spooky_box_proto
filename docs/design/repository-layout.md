# Repository and generated-file policy

This project began as an STM32CubeMX dual-core export and now contains both
generated scaffolding and hand-maintained prototype firmware. This document
defines the boundary so a tool regeneration or cleanup does not discard
working behavior.

## Canonical build

Configure and build from the repository root:

```text
cmake --preset Debug
cmake --build --preset Debug
```

Debug and Release are isolated beneath their root preset directories:

```text
build/<Preset>/firmware/CM4/
build/<Preset>/firmware/CM7/
```

Each core directory contains its ELF, map, object files, and
`compile_commands.json`. The root build uses the maintained
`cmake/dual_core_firmware.cmake`; it deliberately does not use CubeMX's root
`mx-generated.cmake`, which puts different configurations into the same
`CM4/build` and `CM7/build` directories.

The opt-in `IpcSmoke` and `IpcMismatch` presets use the same isolated layout.
They enable a diagnostic workload on M4 and reserve the same shared mailbox
in both images; neither changes the default PlatformIO environment. See
[IPC smoke test](../procedures/ipc-smoke-test.md) before deploying either pair.

The per-core presets under `CM4/` and `CM7/` remain useful for isolated
experiments, but the root preset is the canonical dual-core build and the one
used by `platformio/deploy.py`.

## File ownership

| Path | Ownership | Policy |
| --- | --- | --- |
| `full_spooky_proto.ioc` | CubeMX project input | Track it. It is not yet authoritative for every proven runtime setting; consult `BRINGUP.md` before regeneration. |
| `.mxproject`, `.settings/`, `CM4/.settings/`, `CM7/.settings/` | STM32Cube metadata | Track them while STM32Cube tooling is part of the workflow. |
| `CM4/Core/`, `CM7/Core/`, `CM7/USB_Device/` | Mixed generated scaffolding and hand-maintained code | Track them. Do not overwrite them through CubeMX until the documented `.ioc` discrepancies are reconciled and the diff is reviewed. |
| `CM4/mx-generated.cmake`, `CM7/mx-generated.cmake` | CubeMX-generated core source lists | Track them; regeneration may update them. Review changes before accepting. |
| `mx-generated.cmake` | CubeMX-generated root orchestration | Track as tool output/reference, but the canonical build does not include it. |
| Root and per-core `CMakeLists.txt`, `gcc-arm-none-eabi.cmake` | Generated once, then maintained | Track and edit deliberately. CubeMX documents the CMake project files as one-time generated files. |
| `cmake/`, `platformio/`, `docs/`, `CM7/App/`, `tests/`, IPC/log/diagnostic modules in `Common/` | Hand-maintained | Track and review normally; source lists belong in maintained core CMake files. |
| Both core linker scripts | Generated originally, now maintained | Preserve the identical SRAM4 IPC reservation/NOLOAD section and M7 DMA placement during regeneration. |
| `Drivers/`, `Middlewares/` | Vendored STM32 dependencies | Track to keep the build reproducible; update as an intentional SDK change. |
| `build/`, `.pio/`, `*.su`, CubeMX PDF/TXT reports | Generated output | Ignore; regenerate locally as needed. |
| `.vscode/settings.json`, `.vscode/extensions.json` | Shared editor policy | Track only portable settings. |
| `.vscode/c_cpp_properties.json`, `.vscode/launch.json` | Tool-generated machine state | Ignore because the generated versions contain local paths. |

## Regeneration rule

Before running CubeMX code generation, commit or otherwise preserve a clean
working tree. Generate, inspect the complete diff, and restore any proven
runtime overrides that CubeMX cannot express. The known discrepancies are
listed in [BRINGUP.md](../../BRINGUP.md#cubemx-findings-to-fix-before-broader-bring-up).

The first board-diagnostics and prototype-power extraction now lives in
`CM7/App`; hardware initialization and IRQ wrappers remain in generated shells.
Portable IPC tests build independently through `tests/CMakeLists.txt` and never
inherit the firmware cross toolchain.

Longer term, more hand-maintained application code should move into `CM7/App`,
`CM4/App`, and core-neutral `Common` modules. That reduces the amount of
working behavior living inside generated shells without requiring a risky
one-time repository rewrite.
