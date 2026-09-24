# Prototype architecture

This is the maintained high-level architecture for the STM32H755 Spooky Box
prototype. It distinguishes **proven today**, **target direction**, and
**decisions still open**. The detailed [pivot reference](../reference/legacy_docs/spooky_box_architecture_stm32h745_pivot.md)
is useful design history but its bring-up status predates the current
three-track recording test.

## Proven today

The NUCLEO-H755ZI-Q, RF/backplane hardware, and Zio audio shield have a
working M7 firmware path:

```text
Si4735 stereo -> SAI2/DMA -> M7 radio buffer -> SAI1/SGTL5000 -> headphones
                                   |
                                   +-> WAV interleaver -> SDMMC1 -> 3ch WAV
SPK0641 PDM -> DFSDM1/DMA ----------+
```

The USB CDC CLI can tune the radio, inspect sensors and SD status, run SD
stress tests, and make 48 kHz/16-bit WAV recordings with radio-left,
radio-right, and mic channels. A recording was run on hardware and its WAV
inspected successfully; the user measured less than 130 mA peak through JP8
for that workload. See [BRINGUP.md](../BRINGUP.md) for commands, wiring, and
the limits of those tests.

In normal Debug/Release builds the M4 wakes through the generated dual-core
synchronization sequence and then waits for interrupts. An opt-in diagnostic
IPC build now exists but still requires bench validation; it is not application
IPC. M7 has UI input, LED, matrix and OLED bring-up support, including the
confirmed SSD1309 display mapping in BRINGUP. Product UI and M4 integration
remain pending. Scan engines, full session
lifecycle, and user-facing visual behavior are target features, not claimed
capabilities of this firmware.

## Responsibility boundaries

| Domain | Owner now | Target owner | Boundary |
| --- | --- | --- | --- |
| Audio DMA, DSP/activity, recording, SD filesystem | M7 | M7 | Recording-critical work must not wait on UI work. |
| Radio control, scan decisions, modes, session truth | M7 | M7 | Expose targets, state, and events rather than low-level UI actions. |
| Magnetometer/EMF and power interpretation | M7 | M7 initially | Publish interpreted values with validity and age. |
| USB CLI | M7 | M7 initially; transport placement revisited with services | CLI should issue the same application commands as physical controls. |
| UART7 logging and numeric diagnostics | M7 bounded logger/history | M4 service/diagnostic aggregator after a separate validated handoff | Preserve current APIs and nonblocking M7 producers; never give both cores physical UART ownership. |
| Buttons, encoders, button LEDs, matrix, display | M7 bring-up drivers | M4 where hardware permits | M4 reports behavior-neutral input events and renders semantic state. |
| Inter-core transport | Boot sync; opt-in diagnostic mailbox awaiting bench test | Shared, versioned protocol | No arbitrary shared mutable application objects. |

The target split is a starting point, not a requirement to move every visual
peripheral immediately. Display ownership should follow the actual bus and
timing measurements. A separate RP2040-style coprocessor is not part of the
default plan. The current matrix, magnetometer and gauge share M7 I2C2.
Assign the whole controller to one service/core before moving any of its
clients; two independent HAL drivers must not own the same controller.

The external Pico debugprobe is development-bench infrastructure, not an
application coprocessor. The [Spooky Bench audit and plan](spooky-bench-plan.md)
adds a Pi-hosted command surface around existing flashing, UART capture and
target USB diagnostics before trace or M4 ownership migration. Current UART7
is text output only; diagnostic commands use the target's separate USB CDC.

## Application model

The M7 is the source of truth. A future application layer should distinguish:

- **Persistent state:** current mode, band/frequency, scan engine and position,
  EMF level, radio activity, battery health, and session state.
- **One-shot events:** tuning/band transitions, signal spikes, calibration,
  recording starts/stops, and faults.
- **Input events:** decoded encoder movement, click, button press/release,
  without assigning mode-dependent meaning on the M4.
- **Commands:** requests to tune, change mode, calibrate, start/stop a session,
  or change presentation settings.

The CLI and physical UI should feed the same command handling and state
machines. Scan engines should produce structured tune targets within named
territories; the radio controller decides whether each target needs an
in-band tune or an expensive band/RF-path transition.

The current USB CLI and recorder are bring-up implementations of this model,
not yet the final command router or `SessionManager`.

## Inter-core contract to implement

Keep common types C-compatible and fixed-width. The first protocol should
cover a latest-state snapshot from M7 to M4, one-shot M7-to-M4 events,
behavior-neutral M4-to-M7 input events, and health/heartbeat in both directions.
Packets need a version, size, sequence number, and validity information.
Bounded queues need explicit overrun behavior; stale state must be detectable.

The first diagnostic experiment now reserves 256 bytes of SRAM4 in both
linkers, uses an M7 noncacheable MPU region, and exchanges two frames under
nonblocking HSEM 1 protection with memory barriers. It polls in foreground and
does not add a doorbell IRQ. See the [contract and bench test](ipc-smoke-test.md).
Normal builds leave it disabled. This must pass heartbeat, echo/acknowledgement,
staleness and version-mismatch tests before application IPC is added.

The product state/event/command ABI, bounded queues, restart behavior,
doorbells/priorities, and power coordination remain open. Do not treat the
diagnostic mailbox as the full UI transport.

## Reliability rules

1. Audio capture and SD writes take priority over display and animation.
2. DMA/ISR work remains bounded; filesystem operations do not run in audio
   callbacks.
3. All queue capacities, overrun counters, and longest SD-write latency are
   observable in diagnostics.
4. Band transitions and session start/stop have explicit state and failure
   outcomes. The UI cannot infer success solely from a command being sent.
5. A removed/full/failing SD card must yield a visible fault and, where
   possible, a finalized partial recording.

The present recorder is a successful workload proof, not a final guarantee
for recording under full UI/display load. That combined test is a milestone
after M4/UI integration.

## Repository migration

Keep CubeMX-generated startup, HAL integration, and linker material under
`CM7/Core` and `CM4/Core` while extracting hand-maintained application code
incrementally into core-specific `App/` directories. `CM7/App` now contains
console/battery diagnostics, charging-sleep policy and the IPC CLI adapter.
`Common` separates portable IPC protocol/health code from the STM32 transport.
Refactor one service at a time
from the large M7 `main.c`, building and rerunning the known hardware checks
after each extraction. Do not rename or move the working tree wholesale.

The `.ioc` currently disagrees with several proven runtime peripheral
settings. Its clocking, core/pin ownership, DMA, USB, and SD initialization
must be reconciled (or generation formally frozen) before it is treated as an
authoritative source. See the [CubeMX discrepancy list](../BRINGUP.md#cubemx-findings-to-fix-before-broader-bring-up).

## Next milestones and open decisions

The passing recorder now has a version-controlled baseline. Debug and Release
also have isolated output trees, with the generated/maintained file boundary
documented in [repository-layout.md](repository-layout.md).

1. Bench-check the first extraction, then prove the prepared M7/M4 IPC
   heartbeat and version-mismatch experiment.
2. Continue radio/codec/audio/CLI/storage extraction and decide I2C2 ownership.
3. Define product state/commands and host-test navigation, gestures and
   recording safety using the new experience docs.
4. Deliver Classic/Manual and monitor-only PTT, then measured rolling capture
   and session storage. Integrate M4 input/rendering with recording stress.

See [next-steps.md](next-steps.md) for evidence, dependencies and acceptance
criteria rather than treating the whole feature vision as one milestone.

Open design decisions include the product IPC queues/restart scheme, display
and I2C2 ownership, session file/metadata format, recovery from interrupted
recordings, scan algorithms, and measured timing budgets. They should be
resolved with short decision records and tests, rather than silently embedded
in drivers.
