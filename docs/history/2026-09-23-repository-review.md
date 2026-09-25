# Repository review and next steps

> **Frozen historical snapshot (2026-09-25).** This document is kept as a record of its
> date and is not maintained. Current status is in Beads (`bd ready`,
> `bd graph --all --open`), decisions are in [docs/decisions](../decisions/README.md), and
> current design and procedures are listed in the [docs index](../README.md). Links
> were updated when the docs were reorganized; plain-text paths reflect the original
> layout.

Review date: 2026-09-23. Inputs: [modes and interaction](../../spec/product/modes-and-interaction.md),
all four sheets of the [control map](../../spec/product/spooky_box_control_map.xlsx), maintained
architecture/bring-up docs, current firmware, linker maps, and build/deploy
configuration. This is an implementation sequence; proposed product decisions
below do not override the input documents.

Status reconciled with the 2026-09-24 evidence during the Beads audit. This
document preserves the implementation rationale; Beads now owns executable
tasks, dependencies and status (`bd ready`, `bd graph --all --open`). See the
[README tracking guide](../../README.md#development-tracking) for epic IDs.
The original review's validation section below is historical, not a summary of
everything subsequently tested.

The [Spooky Bench plan](2026-09-23-spooky-bench-plan.md) now defines the parallel development
infrastructure sequence: current-state audit, host command surface, unattended
boot smoke test, incremental target tests, recovery, trace, then remote/agent
automation. Flash/reset/capture and boot smoke now have local Windows evidence;
a Pi/Linux host is an optional later port. Extend local qualification before CI
or trace.
This uses the existing M7 logger/diagnostics and opt-in IPC experiment; it does
not require moving UART or UI ownership to M4 first.

## Assessment

The new design is coherent: operating mode, engine, view, page, temporary
selector, and utility context have distinct jobs. The most valuable early
implementation is a small Field path through these boundaries, followed by
capture and only then Instrument DSP. Implementing all workbook screens first
would bury unresolved audio, memory, and inter-core questions under UI code.

Do not make completing the whole refactor a prerequisite for IPC. Prove the
transport now with a bounded diagnostic experiment, while extracting one
service at a time and retaining the known recording workload as a regression
test. Both can proceed without assigning product behavior to M4 yet.

## Findings that change the order of work

| Finding in current code | Consequence |
| --- | --- |
| M7 `main.c` mixes radio/codec, DMA, volume, CLI, board setup, and power policy. | Extract boundaries incrementally; directory moves alone will not remove coupling. |
| `ui_board_test.c` already handles switches/encoders, LEDs, matrix and OLED on M7. BRINGUP confirms the SSD1309 mapping. | Reuse these drivers as evidence; older claims that the UI board is entirely pending are stale. Product interaction and M4 UI integration remain unimplemented. |
| UI matrix, magnetometer, and fuel gauge are all initialized with `hi2c2`. | A core split must assign **the I2C2 controller** to one owner. Moving the matrix to M4 while both cores directly run HAL on I2C2 is not a valid split. |
| CLI rejects BAND/TUNE/UP/DOWN during recording; band switching stops/restarts the radio path. | Scanning while recording needs a deliberate radio/audio transition contract and a bench test before removing this guard. |
| Mic DMA starts with `RECORD START`; radio DMA is already running. | Today's three-channel WAV proof is not proof of a common sample-zero for synchronized rolling streams. Establish sample indices, start alignment, and drift behavior. |
| Recorder has 8 blocks of 4096 frames per source queue. | Nominal capacity is about 683 ms at 48 kHz, before accounting for blocks being filled/drained. Foreground stalls must be budgeted. |
| Recorder USB reporting can wait 250 ms; display startup waits 100 + 100 ms; SD operations can wait 30 seconds. | The current cooperative loop is a bring-up workload, not a bounded real-time service layer. Nonessential commands and logging need recording-aware policy. |
| Radio DMA fans raw samples to the recorder before copying them to headphone TX. | This is a useful boundary for PTT: preserve raw capture, then alter only the monitored mix. The current bridge does not yet mix microphone audio into monitoring. |

### Rolling-buffer budget

At the proven format, radio stereo plus mono mic costs
`48,000 frames/s * 3 channels * 2 bytes = 288,000 bytes/s` before events/EMF.
Five seconds requires 1.44 MB and ten seconds 2.88 MB. The current link map
already allocates 320 KiB of the 512 KiB AXI/DMA bank, leaving 192 KiB there
(about 0.68 seconds of this format if it were all available). Other RAM banks
have their own owners/access constraints and do not make a multi-second
window free. A copy-based snapshot can require a second window as well.

Before promising a duration, choose a short internal-RAM proof, external
memory, or a measured SD-backed rolling design. Investigate immutable blocks
with ownership/reference tracking so saving pins existing blocks while capture
continues. Define what happens when a second save arrives, storage is slow,
or space runs out. UI states should distinguish snapshot secured, writing,
saved, and failed; switching modes must not imply that saving succeeded.

## Ordered milestones

### 1. Preserve the baseline and prove IPC

Off-bench: compile normal and experiment images, test portable packet/health
logic, verify shared placement in both ELFs, and write the bench procedure.
These are prepared in [ipc-smoke-test.md](../procedures/ipc-smoke-test.md).

Bench exit: normal recording/sleep regression passes; both cores exchange
heartbeat/challenge/acknowledgements; missing/stale/version-mismatched peers
are visible; recording still passes with IPC active. Do this before moving
input sampling, UART, or buses to M4.

Current evidence: initial heartbeat/version rejection, paired flash/reset and
boot smoke have passed, as have IPC progress during one 60-second recording,
CRC-verified WAV inspection and bounded SD success/cancellation. See
[September 24 results](../evidence/2026-09-24-bench-results.md) and
[IPC status](../procedures/ipc-smoke-test.md). Remaining gates include ten-minute recording,
normal-build sleep, M4 halt/stale/resume, explicit cold starts and failure cases.
These are tracked under `full_spooky_proto-jjy`; do not recreate the implemented
bench runners as pending features.

### 2. Finish service boundaries and ownership decisions

The initial extraction moves console/battery diagnostics and prototype sleep
policy to `CM7/App`. Remaining extraction order:

1. Codec/volume service with explicit I2C4, ADC3, clock, mute and jack inputs.
2. Radio control service with tune targets/results, band transitions and fault
   state; separate it from the audio DMA bridge and USB formatting.
3. Audio capture/monitor service owning DMA buffers and callbacks. Expose raw
   capture and monitor mixing separately; keep ISR work bounded.
4. CLI adapter invoking service/application commands, with one shared command
   safety policy for CLI and eventual physical controls.
5. Recorder/storage service separating capture queues, WAV writing, media
   lifecycle and diagnostics. Give the SD/FatFs volume one owner; avoid two
   independent mount lifecycles becoming concurrent storage services.
6. UI test module split into input scanner, LED/matrix/display drivers and
   diagnostic commands. Transfer ownership only after the IPC proof.

Use narrow init/service/status interfaces instead of exporting private globals
or passing one giant main-context object. Preserve peripheral setup, callback
cadence and buffer placement during each extraction. Build Debug and Release,
then rerun the relevant bench regression before stacking timing-sensitive work.

The initial ownership decision is now explicit: keep the whole I2C2 domain on
M7, including matrix, magnetometer and fuel gauge. After the UI driver split
and product IPC qualification, transfer buttons, encoders, direct LEDs and the
SPI6 OLED to M4 as a build-time ownership change. Do not dynamically hand off
peripherals or let both cores configure the same pins. The current generated
M4 TIM16/PF6 setup overlaps the M7 BTN0 LED driver and must be reconciled before
that transfer. See [architecture](../design/architecture.md#initial-ui-and-bus-ownership-decision)
for clock, input-event, gesture and cache boundaries. Keep UART7 on M7 until a
separate handoff decision and test.

### 3. Build the interaction model on the host

Represent operating mode, engine, performance view, parameter page, utility
return context, selector/edit state and transient gesture state separately.
Keep recording/capture state independent of navigation. M7 owns mapping and
state transitions; M4 initially sends debounced physical transitions and
encoder deltas with sequence/time information.

Test page wrap, Manual entry/return, utility return context, selector
commit/cancel, click-versus-hold exclusion, Shift release after a command, and
both orders of the Shift+0+1 chord. Include lost release/queue overflow and
stale UI state: a dropped event must not leave PTT or Shift latched forever.
M4 owns debounce and quadrature decoding and sends physical press/release plus
encoder-detent events. M7 alone resolves clicks, holds and chords using the
press-origin context, with configurable timing until bench trials. Event epochs,
held-state reconciliation and release-all recovery must prevent a dropped event
or M4 restart from leaving PTT or Shift latched.

Agree only the decisions needed for the next slice:

| Decision | Source / proposed next action |
| --- | --- |
| Manual shortcut and engine selector | Workbook C-011..C-015 resolves gestures left open in prose: encoder 0 hold and encoder 2 hold/encoder 0 turn/release. Reconcile the prose before treating both as independent specifications. |
| Utility entry | D-001: Field Shift+encoder 1 versus Instrument Shift+encoder 0. Prefer one consistent utility-root gesture, but retain as a proposal for bench review. |
| Encoder 3 hold priority | D-002: determine behavior from the context where the press began; consume the gesture so returning to an engine cannot turn the same hold into Shift/page advance. |
| Popover commit/cancel | D-003/D-005: a held selector and a selector entered by a short press need explicit exit rules; release-to-commit does not cover both. |
| Shift/chord timing | D-006: delay component actions until the chord is resolved; test both press orders and release permutations before tuning timings physically. |
| Recording safety | D-015 and prose open decision 12: classify mode/engine/preset/playback transitions as allowed, deferred, or rejected. Navigation alone must preserve capture. |
| Recording controls | The map covers PTT/capture but lacks a settled physical session start/stop binding. Keep the CLI path and decide this before declaring a coherent Field recording UI. |

Preset asset references, FX preset scope, Macro deletion, sequencer editing and
page colors can follow. The workbook's Defined/Proposed/Open status is useful;
do not silently promote proposals into implemented requirements.

### 4. Deliver a narrow Field vertical slice

Boot into Field/Classic; support one territory, explicit run/pause, one page,
Manual quick-jump/return, meaningful status, and recording through the common
command path. Add monitor-only PTT with an explicit mic-monitor path and
sample-timestamped press/release metadata. First prove in-band tuning while
recording, then characterize band transitions. Preserve the existing guard
until each workload is safe.

Bench exit: controls remain responsive under recording; raw radio stays
present during PTT; recorded events align with audio; no unexpected engine
reset or recording stop occurs on navigation; queue/latency counters stay
within measured budgets. Seek and Orbit wait until Classic/Manual are solid.

### 5. Add capture/session transactions, then Instrument

Create a common sample timeline and a versioned session manifest with stream
identities, semantic events, incomplete/error state, and recovery rules. The
current interleaved 3-channel WAV can remain the regression format; separate
logical tracks do not require immediately rewriting every storage path.
Choose whether the product uses separate files/stems or a container explicitly.

Implement and stress rolling save while recording before save/switch/load.
Then reuse the same immutable capture asset in one limited Granular engine.
Only after reliable playback and capture should Effects, modulation editors,
presets, Macros, and Sequencer expand. This keeps the central Field-to-Instrument
workflow credible without requiring the full product to exist at once.

## Historical 2026-09-23 review changes

- Extracted roughly 300 lines of board diagnostics/power policy from M7 main.
- Added opt-in matching and deliberately mismatched IPC builds and CLI status.
- Added native C protocol tests and explicit shared-memory/linker policy.
- Updated documentation entry points and stale implementation-status claims.

No firmware was flashed and neither new product document was rewritten.
Builds/host checks are software evidence only; the next bench session supplies
the hardware evidence before adopting the experiment as the normal runtime.

Validation performed this pass:

- Debug, Release, IpcSmoke and IpcMismatch dual-core builds passed with ARM GNU
  14.3.1. Existing unused bring-up/generated-function warnings remain.
- Native C11 protocol tests passed on MSVC 19.29 in Release with warnings
  treated as errors and test checks enabled.
- Both IpcSmoke ELFs contain a 64-byte, 32-byte-aligned, non-loadable mailbox
  at `0x38000000`; normal builds report zero IPC allocation.
- Seven extracted function bodies match the original baseline, allowing only
  public API renames and the radio-shutdown callback parameter.
- PlatformIO configuration resolves both experimental environments to their
  matching presets and keeps the default on Debug. Deployment was not run.
