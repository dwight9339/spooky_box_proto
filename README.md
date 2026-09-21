# Spooky Box prototype

Spooky Box is a handheld field instrument built around radio scanning,
microphone audio, magnetic-field sensing, session recording, and a responsive
visual/physical interface. This repository is the STM32H755 Nucleo prototype,
not yet the complete product firmware.

## Start here

- [Product philosophy](docs/product-philosophy.md) - what the instrument should
  feel like and the meaning its controls and feedback should preserve.
- [Architecture](docs/architecture.md) - current implementation, target M7/M4
  split, boundaries, and open decisions.
- [Repository and generated files](docs/repository-layout.md) - canonical build
  outputs and which files may safely be edited or regenerated.
- [Hardware bring-up and USB CLI](BRINGUP.md) - wiring, known prototype bodges,
  power checks, commands, and recording test procedure.
- [STM32 pivot reference](reference/spooky_box_architecture_stm32h745_pivot.md)
  - detailed design history and proposed features. It predates some completed
  bring-up work, so use the maintained docs above for current status.
- [Earlier Teensy/RP2040 architecture PDF](reference/Spooky%20Box%20Design%20Philosophy%20Requirements%20Architecture.pdf)
  - historical product and behavior reference; its compute topology is
  superseded by the STM32 pivot.

## Repository map

| Path | Purpose |
| --- | --- |
| `CM7/` | Active Cortex-M7 firmware: radio/audio, SD recording, sensors, power, and USB CLI. |
| `CM4/` | Cortex-M4 boot companion; currently sleeps after synchronization. |
| `Common/` | Shared STM32 startup code; future home for core-neutral models and protocol definitions. |
| `Drivers/`, `Middlewares/` | STM32 HAL/BSP and USB middleware dependencies. |
| `full_spooky_proto.ioc` | CubeMX project/pin map; not yet authoritative for all working runtime settings. |
| `platformio/`, `platformio.ini` | VS Code task/deployment wrapper around the CMake firmware build and Picoprobe/OpenOCD flashing. |
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
inter-core boundary. The UI board has not yet been integrated.

The next software milestone is to preserve this working bring-up baseline,
extract application code from the large M7 `main.c`, define the shared
state/event types, and prove a minimal M7-to-M4 heartbeat before UI integration.
