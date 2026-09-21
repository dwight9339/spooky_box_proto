# Prototype architecture

This is the maintained high-level architecture for the STM32H755 Spooky Box
prototype. It distinguishes **proven today**, **target direction**, and
**decisions still open**. The detailed [pivot reference](../reference/spooky_box_architecture_stm32h745_pivot.md)
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

The M4 currently wakes through the generated dual-core synchronization
sequence and then waits for interrupts. There is no application IPC or UI
workload on it yet. The UI board is pending. Scan engines, full session
lifecycle, and user-facing visual behavior are target features, not claimed
capabilities of this firmware.

## Responsibility boundaries

| Domain | Owner now | Target owner | Boundary |
| --- | --- | --- | --- |
| Audio DMA, DSP/activity, recording, SD filesystem | M7 | M7 | Recording-critical work must not wait on UI work. |
| Radio control, scan decisions, modes, session truth | M7 | M7 | Expose targets, state, and events rather than low-level UI actions. |
| Magnetometer/EMF and power interpretation | M7 | M7 initially | Publish interpreted values with validity and age. |
| USB CLI and diagnostics | M7 | M7 initially | CLI issues the same application commands as physical controls. |
| Buttons, encoders, button LEDs, matrix, display | Not integrated | M4 where hardware permits | M4 reports behavior-neutral input events and renders semantic state. |
| Inter-core transport | Boot synchronization only | Shared, versioned protocol | No arbitrary shared mutable application objects. |

The target split is a starting point, not a requirement to move every visual
peripheral immediately. Display ownership should follow the actual bus and
timing measurements. A separate RP2040-style coprocessor is not part of the
default plan.

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

The exact shared SRAM region, cache policy, memory barriers, hardware-semaphore
doorbell, and interrupt priorities are **not yet decided**. These must be
specified together with both linker scripts before implementation. The first
milestone is M4 boot, protocol/version handshake, heartbeat, and state
sequence acknowledgement visible through the CLI, with no UI board required.

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
incrementally into core-specific `App/` directories. `Common/` can then hold
only core-neutral models and IPC definitions. Refactor one service at a time
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

1. Extract M7 board, radio/audio, recorder, sensor, power, and CLI services
   without changing their observed behavior.
2. Define shared state/event/command types and host tests for packet and
   state-machine logic.
3. Prove M7-to-M4 heartbeat and version mismatch handling.
4. Integrate input/rendering drivers when the UI board is available, then
   rerun long recording and SD stress under maximal UI activity.

Open design decisions include the concrete IPC memory/cache scheme, display
core/bus ownership, session file/metadata format, recovery from interrupted
recordings, scan algorithms, and measured timing budgets. They should be
resolved with short decision records and tests, rather than silently embedded
in drivers.
