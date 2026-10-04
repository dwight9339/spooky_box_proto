# CubeMX reconciliation register

`full_spooky_proto.ioc` is a configuration inventory and pin-map aid, not an
authoritative source for the firmware. STM32CubeMX generation is formally frozen until
every blocking entry below is resolved and the
[scratch-generation procedure](../procedures/cubemx-scratch-generation.md) completes
without an error or an unreviewed behavioral change. This boundary protects the proven
runtime configuration required by Constitution Principles III, V and VI.

The maintained source tree, not a fresh CubeMX export, defines the current image pair.
Do not generate into the repository, copy a generated directory over it, or accept a
CubeMX change solely because it compiles.

## Current generator baseline

| Input | Required value | Current result |
| --- | --- | --- |
| STM32CubeMX | 6.17.0 (the version recorded by `MxCube.Version`) | Loads the project, but quiet full-project generation reaches `project generate`, throws an internal `IocParser` null exception and does not terminate without intervention. |
| Device database | The database shipped with CubeMX 6.17.0 | The project loads as STM32H755ZIT6, dual core. |
| STM32CubeH7 | 1.13.0 | Recorded by `ProjectManager.FirmwarePackage` and used by the maintained build. |
| Project output | Dual-core CMake project generated only in scratch space | No complete scratch project is produced by the current CLI attempt. Generation therefore fails closed. |

The command-line failure is itself a freeze condition. A later CubeMX release may be
evaluated in scratch space, but changing generator or firmware-package versions is an
intentional migration: record both versions, inspect the complete diff, and rerun all
gates below.

## Reconciliation inventory

`Represented` means the `.ioc` describes the setting sufficiently. `Placeholder` means
CubeMX cannot serialize the proven value. `Runtime` means maintained code deliberately
replaces or augments generated initialization. `Blocking` means regeneration must not be
adopted until the entry is resolved.

| Area | `.ioc` / generated state | Maintained runtime state | Disposition and qualifying check |
| --- | --- | --- | --- |
| PLL3 and SAI1 codec clock | PLL3P is approximately 24.576 MHz and SAI1 requests 48 kHz, but CubeMX displays a 96 kHz real rate because its `NoDivider` serialization uses an oversampling enum. | The codec path starts the maintained SAI1 configuration and measures PE4 frame sync before declaring success. | **Runtime, blocking.** Preserve the PLL3 source and SAI1 setup. Require boot logs to measure 47.5-48.5 kHz on PE4 and a successful recording regression. |
| SAI2 radio receive | I2S stereo receive, 16-bit slots and circular halfword DMA1 Stream 4 are represented. CubeMX 6.17 limits the user-set master divider to 15 and displays 0 Hz; the valid hardware value is 16. | `AudioPath_Init()` installs master receive, divider 16, a native 32-bit stereo frame and DMA1 Stream 4 before capture. The earlier 64-bit compensation experiment captured at half rate. | **Placeholder plus runtime, blocking.** Require 47.5-48.5 kHz on PD12, approximately 1.536 MHz on PD13, nonconstant stereo radio channels and correct-pitch playback. |
| DFSDM microphone | Audio clock / 8, falling-edge Channel 0, right shift 9, continuous Sinc4/OSR64 Filter 0 and circular word DMA2 Stream 0 are represented. CubeMX locks the DMA interrupt at priority 0. | The preserved MSP code completes the DMA setup and assigns IRQ priority 4. | **Represented plus runtime, blocking.** Require a nonconstant microphone channel in a valid 48 kHz, 16-bit, three-channel WAV and no recorder/DMA fault. |
| SDMMC1 | Four-bit bus, hardware flow control and divider 2 are represented. Generated startup calls `HAL_SD_Init()` immediately. | SD initialization and mounting are deferred to the SD/recorder owner so absent or failing media cannot stop boot. | **Runtime, blocking.** Boot once without a card, then pass SD status/basic I/O and the recording regression with a card. |
| SPI6 / SSD1309 | The `.ioc` describes full-duplex, 4-bit, 16 Mbit/s SPI and includes MISO. | `UiDisplayConfigureSpi()` installs transmit-only, 8-bit, mode-0 operation at 8 MHz, disables NSS pulses and keeps I/O state. | **Placeholder plus runtime, blocking.** Require `UI DISPLAY TEST` to pass and visually inspect the mapped 128x64 image. |
| USB OTG FS | M7 device-only, HSI48, PA11/PA12 and PA9 GPIO VBUS sensing are represented. CubeMX locks OTG FS IRQ priority 0. | USB startup enables HSI48/CRS using USB2 SOF; the MSP preserves PA10 as radio reset, forces B-session valid, monitors PA9 and sets OTG FS IRQ priority 6. | **Represented plus runtime, blocking.** Require USB CDC enumeration, VBUS present/absent reporting, CLI round trip and no disturbance of PA10 radio reset. |
| General GPIO ownership | Most hand-maintained radio, power, jack, UI switch/LED and auxiliary GPIOs have `PinAttribute=Free`; CubeMX can omit them from a core's generated initialization. | The M7 `MX_GPIO_Init()` user section and service initialization establish the proven directions, safe levels and pulls. | **Runtime, blocking.** Review every `Free` application pin against `main.h`, the ownership decision and the hardware table; run the radio, jack, power and UI bring-up checks. |
| I2C2 domain | PB10/PB11 and I2C2 are represented as M7-owned. The `.ioc` timing `0x00707CBB` is standard mode (about 100 kHz from the 36 MHz D2PCLK1 kernel clock). | M7 owns the matrix, magnetometer and fuel gauge as one bus domain. The `I2C2_Init 2` user section replaces the timing with fast mode, `0x00F61F37` (about 360 kHz), for the matrix writer (`54w.8`). | **Represented plus runtime.** Preserve whole-domain M7 ownership and the fast-mode timing; require all three device checks; do not split the bus across cores. |
| PF6 / BTN0 LED | The `.ioc` assigns PF6 to M4 TIM16 PWM and generated M4 code configures it. | The current M7 UI bring-up driver also drives PF6 as a GPIO. Normal builds keep M4 asleep, but the image pair still contains conflicting ownership definitions. | **Unresolved ownership, blocking.** Do not enable M4 TIM16. Before M4 UI integration, remove or reassign the M4 mapping and prove exactly one owner in the paired images; then run the direct-LED chase. On `demo/halloween-2026` only, the `Demo` image returns early from the M4 `MX_TIM16_Init()` (user section) and the M7 drives PF6 and PF7 with TIM16 and TIM17 PWM for the button lights (`p04.3`); Debug and Release are unchanged. |
| Linker and cache layout | CubeMX owns the original linker templates but does not describe the maintained cross-core and DMA invariants. | Both linkers reserve the identical SRAM4 IPC `NOLOAD` region; M7 DMA buffers use the maintained DMA section and cache policy. | **Maintained, blocking.** Compare both linker scripts and maps, keep static/linker assertions, and run the IPC stale/reset gate plus recording regression before accepting generated linker changes. |

The sibling jumper test's 64-bit SAI2 compensation remains rejected: it produced
half-rate capture and octave-high playback. It is history, not an alternative setting to
restore.

## Acceptance gate for a future reconciliation

A candidate export is acceptable only when all of the following are true:

1. The scratch procedure completes, records exact CubeMX/database/STM32CubeH7 versions,
   and the complete generated-surface diff is reviewed.
2. Every row above becomes either accurately represented by the `.ioc` or an explicit,
   preserved runtime setting. No blocking row disappears merely because CubeMX emitted a
   different default.
3. Peripheral and pin ownership is single-core for each image pair. In particular, PF6
   is not initialized by both cores, and I2C2 remains one indivisible ownership domain.
4. Maintained CMake files, application services, USB glue, linker reservations and user
   sections are not overwritten by bulk copying.
5. Debug, Release, IpcSmoke and IpcMismatch configure and build from the repository root.
6. Bench checks exercise audio clocks and three-channel recording, SD absent/present
   behavior, USB enumeration, the SSD1309 display, direct LEDs, I2C2 devices and the IPC
   reset/staleness matrix. Results are preserved as dated evidence; a build alone is not
   hardware evidence.

Until the complete gate passes, the generation freeze remains in force and the current
generated shells may be edited only through the repository's normal reviewed workflow.
