# Prototype architecture

This is the maintained high-level architecture for the STM32H755 Spooky Box
prototype. It distinguishes **proven today**, **target direction**, and
**decisions still open**. The detailed [pivot reference](../../reference/legacy_docs/spooky_box_architecture_stm32h745_pivot.md)
is design history only; where it disagrees with this document, this document wins.

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
radio-right, and mic channels. Recording, WAV inspection and a JP8 peak below
130 mA for that workload have hardware evidence in the
[2026-09-23](../evidence/2026-09-23-bench-results.md) and
[2026-09-24](../evidence/2026-09-24-bench-results.md) bench results. See the [USB CLI contract](usb-cli.md),
[prototype hardware](prototype-hardware.md) and the [bring-up procedure](../../BRINGUP.md)
for commands, wiring, and the limits of those tests.

In normal Debug/Release builds the M4 wakes through the generated dual-core
synchronization sequence and then waits for interrupts. An opt-in diagnostic
IPC build exchanges heartbeat frames between the cores; see the
[diagnostic IPC contract](ipc-diagnostic-contract.md). It is not application IPC,
and its qualification is tracked in Beads `full_spooky_proto-jjy`. M7 has UI input,
LED, matrix and OLED bring-up drivers, including the confirmed SSD1309 display
mapping in [prototype hardware](prototype-hardware.md#ui-board).

Product UI, M4 UI integration, scan engines, the full session lifecycle, and
user-facing visual behavior are target features, not capabilities of this firmware.

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
| Inter-core transport | Boot sync; opt-in diagnostic mailbox | Shared, versioned protocol | No arbitrary shared mutable application objects. |

### Initial UI and bus ownership decision

Recorded in [decision 0001](../decisions/0001-initial-ui-and-bus-ownership.md). In
summary:

- For the first product slice, M7 alone owns the whole I2C2 domain: the matrix
  (`0x30`), the magnetometer (`0x35`), the fuel gauge (`0x55`) and their enable and
  interrupt pins.
- After the UI driver split and product-IPC qualification, M4 takes the buttons,
  encoders, direct LEDs and the SPI6 SSD1309. This is a build-time transfer; a
  peripheral and its pins have exactly one owner in any image pair.
- M4 reports behavior-neutral press/release transitions and encoder detents. M7 alone
  resolves clicks, holds, Shift and chords. Restart, overflow or stale input causes a
  release-all reconciliation.
- Cross-core state uses the versioned IPC region and its barrier and cache policy, not
  shared driver objects. The M4 framebuffer stays core-local.

The current M7 bring-up isolates transferable electrical processing in
`ui_input_service` and hardware-neutral animation/framebuffer work in
`ui_render_service`. The diagnostic `ui_board_test` adapter remains the sole owner of
the proven GPIO, SPI6 and I2C2 mappings and preserves the existing USB commands. Input
calls inspect at most six switches or four encoders. Each render-service pass emits at
most one direct-LED action or five matrix pixels; the SSD1309 test image is a fixed
1 KiB framebuffer. This split is an ownership seam, not an M4 transfer: normal images
still have exactly one M7 hardware owner until product IPC and the PF6 reconciliation
gates in decision 0001 are complete.

The matrix mapping of [decision 0013](../decisions/0013-matrix-emf-radio-and-status-mapping.md)
is split the same way. Sensor interpretation stays on the M7: `emf_level` turns the
magnetometer's change from baseline into a bucket, or an explicit unknown state (no
sample, no baseline, stale, sensor fault), and `radio_activity` detects onsets in the
radio audio from one mean absolute level per half-buffer, holding its averages while
the radio is not measuring. The pixel decisions live in the portable
`Common/matrix_feedback` renderer, which takes only those semantic facts and the
recording state and composes a 9x9 frame. It can run on whichever core renders the
matrix. Host tests cover all three; how the matrix looks and what the renderer costs
under recording load need bench evidence.

The external Pico debugprobe is development-bench infrastructure, not an
application coprocessor. Spooky Bench provides a Windows-hosted command surface
around flashing, UART capture and target USB diagnostics; see
[decision 0002](../decisions/0002-windows-first-spooky-bench.md) and the
[setup procedure](../procedures/spooky-bench-setup.md). A Pi/Linux host is a later
option, after local qualification, rather than a prerequisite. UART7
is text output only; diagnostic commands use the target's separate USB CDC.

## Application model

The M7 is the source of truth. It owns the authoritative operating mode, active
engine, utility-space state, engine state, recording state, rolling audio and sensor
buffers, storage transactions, audio behavior, contextual gesture resolution, and
the mapping from resolved gestures to product actions. Mode state is versioned, so the cores cannot silently disagree about
the active operating mode, engine, view, parameter page, Shift state or utility space.
The M4 decides how to render published state on its OLED and direct LEDs. While I2C2
remains on M7, the bounded M7 matrix service renders the same authoritative state.

A future application layer should distinguish:

- **Persistent state:** current mode, band/frequency, scan engine and position,
  EMF level, radio activity, battery health, and session state.
- **One-shot events:** tuning/band transitions, signal spikes, calibration,
  recording starts/stops, and faults.
- **Input events:** decoded encoder movement, click, button press/release,
  without assigning mode-dependent meaning on the M4.
- **Commands:** requests to tune, change mode, calibrate, start/stop a session,
  or change presentation settings.

The CLI and physical UI should feed the same command handling and state
machines. Machines exchange commands and cross-region events only through one
bounded M7 event queue, fed from the foreground and dispatched run-to-completion
([decision 0007](../decisions/0007-m7-event-queue.md)). Scan engines should produce structured tune targets within named
territories; the radio controller decides whether each target needs an
in-band tune or an expensive band/RF-path transition.

The Classic engine core, `classic_scan`, follows this shape
([spec 001](../../spec/specs/001-classic-scan-engine/spec.md),
[decision 0016](../decisions/0016-classic-scan-motion.md)). It is portable code
that keeps only Classic's own parameters and run state: direction, jump rate, edge
behavior, a jump distance per band, and running or paused. Band and frequency stay
with the radio. Each service pass passes in the shared tuning (band, last completed
frequency, whether a tune is in flight) and whether Classic may move. When a jump is
due, the core answers with the target channel and frequency, and the caller issues the
tune. Sweep complete is derived from position and direction, not stored. All decision
0016 values are in one `ClassicScanConfig`. Host tests cover the landings, the edges
and the jump schedule. The firmware does not link the core yet: the M7 wiring through
the command policy and RadioSm, published state and events, and the unable-to-scan
reason belong to `full_spooky_proto-54w.33`, and the activity hold to
`full_spooky_proto-54w.32`.

The USB CLI ([contract](usb-cli.md)) and recorder are bring-up implementations of
this model, not the final command router or `SessionManager`.

The [behavior model](behavior/README.md) specifies these states, commands, events and
their transitions, one machine per region, as StateSmith diagrams that generate the
firmware ([decision 0006](../decisions/0006-statesmith-behavior-model.md)) or, for
regions not yet moved, as tables.

## Inter-core contract to implement

Keep common types C-compatible and fixed-width. The first protocol should
cover a latest-state snapshot from M7 to M4, one-shot M7-to-M4 events,
behavior-neutral M4-to-M7 input events, and health/heartbeat in both directions.
Packets need a version, size, sequence number, and validity information.
Bounded queues need explicit overrun behavior; stale state must be detectable.

The diagnostic experiment reserves 256 bytes of SRAM4 in both
linkers, uses an M7 noncacheable MPU region, and exchanges two frames under
nonblocking HSEM 1 protection with memory barriers. It polls in foreground and
does not add a doorbell IRQ. See the [contract](ipc-diagnostic-contract.md)
and [bench procedure](../procedures/ipc-smoke-test.md).
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
   possible, a finalized partial recording. The recorder samples filesystem free
   space once at open and accounts matched-block bytes locally against its
   configured reserve; it does not query FatFs on every block.
6. The M7 storage service is the sole owner of the FatFs volume lifecycle and
   SDMMC clock/deinitialization policy. Recorder, SD stress/status, and WAV
   transfer code retain their file-specific I/O, but only while holding one
   explicit exclusive owner lease; a client cannot unmount another client.

The present recorder is a successful workload proof, not a final guarantee
for recording under full UI/display load. That combined test is a milestone
after M4/UI integration.

## Repository migration

Keep CubeMX-generated startup, HAL integration, and linker material under
`CM7/Core` and `CM4/Core` while extracting hand-maintained application code
incrementally into core-specific `App/` directories. `CM7/App` contains
console/battery diagnostics, charging-sleep policy, the exclusive storage-volume
service, the electrical UI input and bounded rendering services, and the IPC CLI
adapter.
`Common` separates portable IPC protocol/health code from the STM32 transport.
Refactor one service at a time
from the large M7 `main.c`, building and rerunning the known hardware checks
after each extraction. Do not rename or move the working tree wholesale.

The `.ioc` disagrees with several proven runtime peripheral
settings. Its clocking, core/pin ownership, DMA, USB, and SD initialization
must be reconciled (or generation formally frozen) before it is treated as an
authoritative source. See the [CubeMX reconciliation register](cubemx-reconciliation.md).

## Roadmap and open decisions

Delivery order and milestone exit criteria are in the
[development roadmap](../../spec/product/roadmap.md); status is tracked in Beads
(`bd graph --all --open`). The roadmap epics are baseline qualification (`full_spooky_proto-jjy`), service
boundaries (`full_spooky_proto-8lw`), the first Field experience
(`full_spooky_proto-54w`), capture/playback (`full_spooky_proto-hpq`), Instrument
(`full_spooky_proto-v7l`), and bench extensions (`full_spooky_proto-5yv`). The
reasoning behind that order is in Constitution Principle VII and the
[2026-09-23 repository review](../history/2026-09-23-repository-review.md).

Open design decisions include the product IPC queues/restart scheme, session
file/metadata format, recovery from interrupted recordings, scan algorithms,
and measured timing budgets. Each is resolved with a record in
[`docs/decisions`](../decisions/README.md) and a test, rather than silently embedded
in drivers.
