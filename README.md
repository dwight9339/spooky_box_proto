# Spooky Box prototype

Spooky Box is a handheld field instrument built around radio scanning,
microphone audio, magnetic-field sensing, session recording, and a responsive
visual/physical interface. This repository is the STM32H755 Nucleo prototype,
not yet the complete product firmware.

## Start here

- [Constitution](spec/.specify/memory/constitution.md) - the project's governing
  principles, documentation classes, and how authoritative documents are changed.
- [Product philosophy](spec/product/product-philosophy.md) - what the instrument should
  feel like and the meaning its controls and feedback should preserve.
- [Modes and interaction](spec/product/modes-and-interaction.md) and
  [control map](spec/product/control-map.md) - intended experience,
  contextual controls, and open product decisions.
- [Architecture](docs/design/architecture.md) - current implementation, target M7/M4
  split, boundaries, and open decisions.
- [Hardware bring-up](BRINGUP.md) - power checks, flashing, and the radio, USB, UI,
  recording, SD and sleep test procedures. Wiring and bodges are in
  [prototype hardware](docs/design/prototype-hardware.md); commands are in the
  [USB CLI contract](docs/design/usb-cli.md).
- [Documentation index](docs/README.md) - decision records, design contracts, bench
  procedures, dated evidence, and frozen history.
- [STM32 pivot reference](reference/legacy_docs/spooky_box_architecture_stm32h745_pivot.md)
  - detailed design history and proposed features. It predates some completed
  bring-up work, so use the maintained docs above for current status.
- [Earlier Teensy/RP2040 architecture PDF](reference/legacy_docs/Spooky%20Box%20Design%20Philosophy%20Requirements%20Architecture.pdf)
  - historical product and behavior reference; its compute topology is
  superseded by the STM32 pivot.

## Repository map

| Path | Purpose |
| --- | --- |
| `CM7/` | Active Cortex-M7 firmware: radio/audio, SD recording, sensors, power, and USB CLI. |
| `CM4/` | Cortex-M4 boot companion; sleeps in normal builds, services IPC in opt-in experiments. |
| `CM7/App/` | Board diagnostics, prototype power, bounded UART logger, diagnostic CLI, and IPC CLI adapter. |
| `Common/` | Shared startup, portable log/event buffers, diagnostic IPC contract, and STM32 IPC transport. |
| `tests/` | Native C tests that run without hardware. |
| `host/` | Spooky Bench Python CLI, profiles, dependency pin, and offline tests. |
| `Drivers/`, `Middlewares/` | STM32 HAL/BSP and USB middleware dependencies. |
| `full_spooky_proto.ioc` | CubeMX project/pin map; not yet authoritative for all working runtime settings. |
| `platformio/`, `platformio.ini` | VS Code task/deployment wrapper around the CMake firmware build and Picoprobe/OpenOCD flashing. |
| `spec/` | Spec Kit root: constitution (`.specify/memory/`), protected product intent (`product/`), and feature specs (`specs/`). |
| `docs/` | Decision records, design contracts, bench procedures, dated evidence, and frozen history; see the [index](docs/README.md). |
| `reference/` | Schematics and historical design documents. |

## Build and flash

The project uses a maintained dual-core CMake orchestration layer around the
STM32CubeMX-generated core projects, with an ARM GNU toolchain and Ninja. It
also expects STM32CubeH7 V1.13.0 for the FatFs source; see `CM7/CMakeLists.txt`
for `STM32CUBE_H7_ROOT` if it is not under the default STM32Cube repository
location.

From the repository root, with CMake, Ninja, and `arm-none-eabi-gcc` available:

```text
cmake --preset Debug
cmake --build --preset Debug
```

On Windows, see [Windows Ninja firmware builds](docs/procedures/windows-ninja-build.md)
if an automated or sandboxed build waits before launching its first command.

Each preset has its own complete child builds and firmware images:

```text
build/Debug/firmware/CM7/full_spooky_proto_CM7.elf
build/Debug/firmware/CM4/full_spooky_proto_CM4.elf
build/Release/firmware/CM7/full_spooky_proto_CM7.elf
build/Release/firmware/CM4/full_spooky_proto_CM4.elf
```

The PlatformIO **Deploy** task uses `custom_cmake_preset` (Debug by default),
then builds, flashes, and verifies both matching images through the configured
Picoprobe. It does not replace the CMake build. Follow
[BRINGUP.md](BRINGUP.md) for power, connection, terminal, and test instructions
before flashing hardware.

Do not regenerate the `.ioc` and assume the result matches the passing image:
several proven clock and peripheral configurations currently override or defer
the generated settings. The specific discrepancies are listed in
the [CubeMX reconciliation register](docs/design/cubemx-reconciliation.md).

## Current direction

The M7 is the source of truth for radio, audio, storage, session behavior, and
sensor interpretation. The M4's target role is input scanning and UI presentation,
exchanging semantic state and events with the M7 across a versioned inter-core
boundary. For the first product slice the whole I2C2 domain (matrix, magnetometer,
fuel gauge) stays on M7; see [decision 0001](docs/decisions/0001-initial-ui-and-bus-ownership.md)
and the [architecture](docs/design/architecture.md) for what is proven today and
what is still target. Normal Debug/Release builds keep the M4 asleep; the opt-in
`IpcSmoke` build carries the diagnostic IPC experiment.

Delivery order, milestone exit criteria, and the rules that keep Spooky Bench and
other tooling in service of the product are in the
[development roadmap](spec/product/roadmap.md): qualify the baseline and finish
service boundaries, deliver the Field Classic/Manual slice, then sessions and rolling
capture, and only then Instrument features. Status is tracked in Beads, not in this README;
dated validation lives in [`docs/evidence`](docs/evidence/). Host tests are
described in [tests/README.md](tests/README.md).

## Development tracking

Beads is the source of truth for executable tasks, status and dependencies;
the docs explain design intent and preserve validation evidence. Start with
`bd ready`, inspect a task with `bd show <id>`, and claim it with
`bd update <id> --claim`. Use `bd ready --type task --priority 1` to focus on
near-term actionable tasks and `bd graph --all --open` for the current task graph.

Product tasks carry a milestone label (`m1`-`m5`) from the
[roadmap](spec/product/roadmap.md); supporting tooling carries `track-tooling`;
time-boxed demo work carries `track-demo`.
The roadmap is grouped into baseline qualification (`full_spooky_proto-jjy`),
service boundaries (`full_spooky_proto-8lw`), the first Field experience
(`full_spooky_proto-54w`), capture/playback (`full_spooky_proto-hpq`), Instrument
(`full_spooky_proto-v7l`), and bench extensions (`full_spooky_proto-5yv`).
Each task records source documents and acceptance criteria. Parent epics group
work; blocking dependencies specify prerequisites. P1 is near-term foundation
work, P2 the next delivery slices, and P3/P4 later expansion. `bench-required`
identifies hardware work; `off-bench` means implementation can start without
hardware, though its acceptance may still require a bench regression.

Issue data lives in the local Beads database. Code commits and remote Beads
sync are separate, explicit operations; `.beads/issues.jsonl` is a passive
export, not the task database.
