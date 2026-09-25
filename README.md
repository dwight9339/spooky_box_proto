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
  [control map](spec/product/spooky_box_control_map.xlsx) - intended experience,
  contextual controls, and open product decisions.
- [Architecture](docs/design/architecture.md) - current implementation, target M7/M4
  split, boundaries, and open decisions.
- [Hardware bring-up and USB CLI](BRINGUP.md) - wiring, known prototype bodges,
  power checks, commands, and recording test procedure.
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
[BRINGUP.md](BRINGUP.md#cubemx-findings-to-fix-before-broader-bring-up).

## Current direction

Keep the M7 as the source of truth for radio, audio, storage, session behavior,
and sensor interpretation. The intended M4 role is input scanning and UI
presentation, communicating semantic state and events across a controlled
inter-core boundary. Input, LEDs, matrix and OLED have M7 bring-up support;
product interaction and M4 UI ownership are not yet integrated. I2C2 is shared
by matrix and sensors, so its controller ownership must be resolved together.

The opt-in `IpcSmoke` build has passed initial boot and 60-second recording-load
checks; WAV inspection and bounded SD success/cancellation also have live
evidence. Longer recording, sleep, stale-peer/reset and failure-path acceptance
remain before broader ownership changes. Normal Debug/Release retain
sleeping-M4 behavior. Continue service extraction alongside a host-tested
interaction model, then deliver Classic/Manual and reliable Field capture
before expanding Instrument features. See the
[roadmap rationale](docs/history/2026-09-23-repository-review.md) and [host-test commands](tests/README.md).

## Development tracking

Beads is the source of truth for executable tasks, status and dependencies;
the docs explain design intent and preserve validation evidence. Start with
`bd ready`, inspect a task with `bd show <id>`, and claim it with
`bd update <id> --claim`. Use `bd ready --type task --priority 1` to focus on
near-term actionable tasks and `bd graph --all --open` for the current task graph.

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
