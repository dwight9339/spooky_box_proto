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
IPC build has initial heartbeat, version-rejection and 60-second recording-load
evidence; longer runs and stale-peer/reset qualification remain. See the
[current IPC status](ipc-smoke-test.md). It is not application IPC.
M7 has UI input, LED, matrix and OLED bring-up support, including the
confirmed SSD1309 display mapping in BRINGUP. Product UI and M4 integration
remain pending. Scan engines, full session
lifecycle, and user-facing visual behavior are target features, not claimed
capabilities of this firmware.

## Responsibility boundaries

| Domain | Owner now | Target owner | Boundary |
| --- | --- | --- | --- |
| Audio DMA, DSP/activity, recording, SD filesystem | M7 | M7 | Recording-critical work must not wait on UI work. |
| Radio control, scan decisions, modes, session truth | M7 | M7 | Expose targets, state, and events rather than low-level UI actions. |
| I2C2, matrix, magnetometer and fuel gauge | M7 | M7 for the first product slice | One M7 service owns the controller and all three clients. Publish interpreted sensor values with validity and age. |
| USB CLI | M7 | M7 initially; transport placement revisited with services | CLI should issue the same application commands as physical controls. |
| UART7 logging and numeric diagnostics | M7 bounded logger/history | M7 until a separate handoff decision and test | Preserve current APIs and nonblocking M7 producers; never give both cores physical UART ownership. |
| Buttons, encoders and direct LEDs | M7 bring-up driver | M4 after product IPC and driver split | M4 reports debounced transitions and encoder detents; M7 resolves contextual gestures and commands. |
| SSD1309 display on SPI6 | M7 bring-up driver | M4 after product IPC and driver split | M4 renders versioned semantic state into a core-local framebuffer. |
| Inter-core transport | Boot sync; opt-in diagnostic mailbox with initial live evidence | Shared, versioned protocol | No arbitrary shared mutable application objects. |

### Initial UI and bus ownership decision

For the first product slice, M7 remains the sole owner of I2C2, PB10/PB11, the
matrix at `0x30`, the magnetometer at `0x35`, and the fuel gauge at `0x55`.
Matrix enable and magnetometer interrupt pins belong to that M7 service as
well. The matrix therefore remains a bounded M7 renderer driven directly from
authoritative semantic state. Moving only the matrix would create two HAL
owners for one controller; moving the whole I2C2 domain would also move sensor
acquisition across the core boundary before the product protocol is proven.

After the UI bring-up module is split and product IPC passes its restart and
staleness gates, M4 takes ownership of the button and encoder GPIOs, the direct
LED GPIOs, and the SSD1309 display including SPI6, chip select, data/command and
reset. This is a staged build-time transfer, not a runtime handoff: a peripheral
and its pins have exactly one owner in any image pair. Until that milestone,
the current M7 bring-up driver owns all of them. The generated M4 TIM16/PF6
configuration currently overlaps the M7 BTN0 LED bring-up and must be removed
or reassigned as part of CubeMX reconciliation before M4 UI integration.

M4 owns electrical input processing only: 1 kHz sampling, switch debounce,
quadrature decoding, monotonic event sequence/time, and current held-state
reporting. It emits press/release transitions and signed encoder detents. M7 is
the sole gesture resolver. It uses the context in which a press began to decide
clicks, holds, Shift and multi-button chords, then routes the resulting product
command. Peer restart, queue overflow or stale input must cause a release-all
reconciliation so Shift or PTT cannot remain latched.

I2C2 currently uses polling HAL calls from the foreground and has no IRQ or DMA
ownership to transfer. Its D2PCLK1 clock selection, peripheral reset and GPIO
alternate functions remain M7 responsibilities. The M4 UI build must keep a
1 kHz time base active instead of the normal sleeping-M4 tick policy. SPI6 uses
D3PCLK1 and blocking foreground transfers today; its M4 renderer must bound
update work and lower frame rate before it can affect recording. The framebuffer
stays local to M4. Cross-core state and events use the versioned IPC region and
its explicit barriers/cache policy rather than shared driver objects or buffers.

A later measurement may justify moving the entire I2C2 domain to M4, including
sensor acquisition and matrix output, but that is a new ownership decision with
sensor-publication and recovery tests. A separate RP2040-style coprocessor is
not part of the default plan.

The external Pico debugprobe is development-bench infrastructure, not an
application coprocessor. The [Spooky Bench audit and plan](spooky-bench-plan.md)
provides a Windows-hosted command surface around flashing, UART capture and
target USB diagnostics. A Pi/Linux host is a later option, after local
qualification, rather than a prerequisite. Current UART7
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

1. Extend the initial recording/IPC evidence with ten-minute recording,
   normal-build sleep, stale-peer recovery and explicit reset-state checks.
2. Continue radio/codec/audio/CLI/storage extraction and implement the staged
   UI ownership decision above.
3. Define product state/commands and host-test navigation, gestures and
   recording safety using the new experience docs.
4. Deliver Classic/Manual and monitor-only PTT, then measured rolling capture
   and session storage. Integrate M4 input/rendering with recording stress.

See [next-steps.md](next-steps.md) for design rationale and `bd graph --all --open`
for current dependencies. Beads tasks carry acceptance criteria and status;
the dated bench results preserve the evidence behind completed checks.

Open design decisions include the product IPC queues/restart scheme, session
file/metadata format, recovery from interrupted recordings, scan algorithms,
and measured timing budgets. They should be
resolved with short decision records and tests, rather than silently embedded
in drivers.
