# Spooky Bench: current-state audit and implementation proposal

Audit date: 2026-09-23 (America/Denver). Implementation update: 2026-09-24.
**Phase 1A is implemented under `host/` and passes 22 offline tests on Windows.**
The repo-local environment is installed. Both CDC identities were discovered and
selected, and all four LOG/DIAG command paths passed with archived evidence,
including a complete dump. UART startup capture also passed with 2791 bytes and
verified raw/index/session artifacts; see [live results](bench-results-2026-09-24.md).
Probe unplug produced a prompt error with preserved incomplete evidence, and
capture after reconnect passed. Target CDC disconnect during synchronization and
a query after reconnect also passed. Concurrent-operation rejection and Ctrl+C
termination behaved as expected, and post-interrupt query/capture verified lock
and port release. Basic live checks are complete; mid-response disconnect,
hardware COM renumbering, and sustained-load checks remain pending. See
[setup and usage](spooky-bench-setup.md). Phase 1B controls and Phase 2 boot-smoke
remain planned. Raspberry Pi/Linux deployment is deferred. The original audit
below records the state before implementation; its inventory and validation
claims should be read in that historical context. This plan sets the
absentee-debugging sequence; the product roadmap remains in
[next-steps.md](next-steps.md).

## Decision in brief

Build a native Windows-first Python `spookybench` command surface in this repository,
with portable test logic and explicit operating-system adapters.
Wrap the existing CMSIS-DAP/OpenOCD deployment recipe and reuse the sibling
Spooky Probe capture/decoder package. Accept prebuilt, matched M7/M4 artifacts;
compiling is a separate concern even when builds and bench runs share this PC.
Start with device discovery/status, bounded UART capture, and existing diagnostic
queries. Then add probe, paired flash and reset, and automate the existing
`IpcSmoke` experiment after those operations pass hardware acceptance.
Do not require trace, power switching, CI, or a core-ownership migration first.

**The prompt's claimed current M4 diagnostic ownership is not the repository's
current implementation.** UART7, diagnostic history, and device USB CDC are on
M7. M4 sleeps in normal builds. Preserve these implementations for Phase 1/2;
the requested long-term M4 service/diagnostic aggregator remains the destination,
with a separately validated ownership handoff. Never initialize UART7 from both
cores or make M7 wait for a diagnostic consumer.

The initial bench runs directly on this PC, with both USB data connections:

```text
developer / agent -> Spooky Bench CLI on Windows PC
                    |-- OpenOCD -> Pico CMSIS-DAP -> H755 SWD
                    |-- capture -> Pico CDC -> H755 UART7 text
                    `-- diagnostic client -> H755's own USB CDC
```

No diagnostic command receiver exists on target UART7. A Pico-only USB cable
cannot currently run `DIAG STATUS` or `IPC STATUS`.

Use a local Python environment and a PC bench profile; no Pi, WSL, SSH, daemon,
or custom agent integration is required initially. The agent invokes the same
CLI as a developer and consumes JSON results and saved artifacts. A Raspberry Pi
is a later option for a permanently connected bench independent of the desktop,
using the same command/result contract. Initial unattended runs depend on this
PC staying awake and connected; host sleep/disconnect is not a target failure.

## Audit provenance and files inspected

The revisions and working-tree descriptions in this section record the original
audit state. Subsequent source checkpoints and user-reported hardware evidence
are recorded in [bench results](bench-results-2026-09-23.md). Those hardware
results were collected manually before the Phase-1A utility was implemented.

The target repository HEAD is `75ff3a0d59bb452d66a4477c7a42e10be711a00b`.
The audit includes the **dirty working tree**, including untracked App/Common
modules, docs, and tests. HEAD alone does not reproduce this state. No applicable
`AGENTS.md` was found in the repository or its ancestor directories.

The sibling `../spookyprobe` is a separate repository, not a subtree/submodule.
Its HEAD is `3fff5b240ca8200c7ad538cb61c02dfc39bda831`, also with modified and
untracked files. It was inspected read-only. Neither repository was reset,
cleaned, staged, committed, or flashed. Other sibling projects were only listed.

Files inspected (source inspection includes focused reads/searches):

| Area | Files / scope |
| --- | --- |
| Maintained target documentation | `README.md`, `BRINGUP.md`, `docs/architecture.md`, `docs/repository-layout.md`, `docs/next-steps.md`, `docs/logger-diag.md`, `docs/ipc-smoke-test.md`, `docs/spookyprobe-v1.md`, `tests/README.md` |
| Earlier plan | `reference/legacy_docs/spooky_probe_trace_implementation_plan.md` |
| Target build/deploy | Root/core `CMakeLists.txt`, root `CMakePresets.json`, `cmake/dual_core_firmware.cmake`, `cmake/ipc_smoke.cmake`, `gcc-arm-none-eabi.cmake`, core `mx-generated.cmake`, `platformio.ini`, `platformio/deploy.py`, `.gitignore`, local ignored `.vscode/launch.json` |
| Core ownership and integration | `CM7/Core/Src/main.c`, `CM4/Core/Src/main.c`, both flash linker scripts, both `stm32h7xx_it.c` fault-handler sections |
| Logging/diagnostics | `CM7/App/target_logger.{h,c}`, `diagnostics.{h,c}`, `board_diagnostics.c`, `prototype_power.c`, `ipc_smoke_cli.c`; `Common/Inc/log_buffer.h`, `diag_history.h`, `ipc_smoke.h`, `ipc_smoke_protocol.h`; `Common/Src/ipc_smoke.c` |
| Existing workload/transport seams | `CM7/Core/Src/radio_recorder.c`, `sd_test.c`, `ui_board_test.c`, `CM7/Core/Inc/ui_board_test.h`, `CM7/USB_Device/Src/usb_test.c`, `usbd_desc.c` |
| Target tests/output evidence | `tests/CMakeLists.txt`, `tests/logger_diag_test.c`, local `build/run-host-tests.cmd`, `build/host/CMakeCache.txt`, inventory of eight preset ELFs |
| Sibling probe documentation/build | `../spookyprobe/docs/logger-diagnostics.md`, `CMakeLists.txt`, `host/pyproject.toml`, local `build/CMakeCache.txt`, generated `probe.pio.h` / `autobaud.pio.h` |
| Sibling probe firmware | `src/main.c`, `probe.c`, `probe_config.h`, `cdc_uart.c`, `autobaud.c`, `probe.pio`, `autobaud.pio`, `usb_descriptors.c`, `tusb_config.h`, `FreeRTOSConfig.h`, `tusb_edpt_handler.{h,c}`, `include/board_pico_config.h`, `include/DAP_config.h` |
| Sibling host package | `host/spookyprobe/__main__.py`, `client.py`, `capture.py`, parser entry points in `protocol.py`; test execution of `tests/test_host.py` |
| Local SDK/RTOS resource checks | Pico SDK 2.3.1 `src/boards/include/boards/pico.h`, `src/common/pico_time/include/pico/time.h`; sibling FreeRTOS RP2040 `port.c` SysTick setup |

The `.ioc`, schematics, full SDK dependency closure, historical architecture PDF,
and control-map workbook were not independently audited. Maintained documentation
reports `.ioc`/runtime discrepancies; this session did not regenerate CubeMX or
certify physical wiring.

## Actual architecture and existing public interfaces

| Responsibility | Current implementation | Requested direction |
| --- | --- | --- |
| Audio DMA, radio, recording, SD, authoritative behavior | M7 bring-up firmware and recorder | Remain M7; avoid diagnostic waits in critical paths |
| UART7 physical peripheral | M7 `BoardDiagnostics_StartConsole()` and `TargetLogger_*` | M4, after an explicit handoff milestone |
| Diagnostic event history and commands | M7 `Diagnostics_*`, commands over target USB CDC | M4 aggregation with bounded M7 event forwarding |
| Inputs, LEDs, matrix, OLED | M7 `UiBoardTest_*` bring-up | M4 services where bus ownership permits |
| M4 normal Debug/Release | Boot synchronization, minimal init, suspended SysTick, WFI | UI/service/diagnostic workload later |
| M4 IpcSmoke/IpcMismatch | Foreground mailbox service, SysTick active, WFI between interrupts | Diagnostic experiment only; not application IPC |

I2C2 is shared by matrix, magnetometer, and fuel gauge on M7. Moving one client
does not transfer the controller safely. There is no established product command
router or public decoded-input injection interface yet. `UiBoardTest_Tick1ms()`
and `UiBoardTest_Service()` decode controls and produce bring-up messages; the
prompt's clean application/input boundary is a design direction, not a completed
interface.

### UART logger

`BoardDiagnostics_StartConsole()` configures PE8 TX / PE7 RX, UART7, 115200 8N1,
no flow control, FIFO disabled. PE8 connects to Pico GP5; PE7 to GP4. It uses the
BSP COM handle but the strong `_write`/`__io_putchar` hooks in `target_logger.c`
replace blocking BSP logging (`USE_COM_LOG` is disabled in the documented build).

Public API: `TargetLogger_Init`, `Write`, `Service`, `GetStats`, `Quiesce`.
Writes/printf are foreground-only; IRQ/masked-IRQ writes are rejected and counted.
The 4096-byte queue accepts at most 512 bytes per write, rejecting the entire
write on insufficient capacity. UART IRQ priority 15 transmits at most 128 bytes
per chunk and retains buffer ownership until completion. A foreground 250 ms
watchdog aborts/discards a stuck chunk. It cannot execute during another blocked
foreground operation. Only power transitions use a bounded 400 ms drain/abort.

Preserve this behavior and counters. A printf can span multiple writes, so logs
can contain partial lines. No wire sequence, CRC, target timestamp, or delivery
guarantee exists. Formatting still consumes M7 foreground CPU. At full line rate,
per-byte IRQ load needs measurement during audio; nonblocking does not mean free.

### Numeric diagnostics and target CDC

Public API: `Diagnostics_Init`, `Record`, `HandleCommand`, `Service`.
`Diagnostics_Record(type,a,b)` records a fixed 20-byte event with sequence and MCU
tick, under a short interrupt-masked critical section. Normal IRQ producers do
not allocate, print, access USB, or wait on UART. It is not a HardFault/NMI API.
`DiagHistory_*` and `LogBuffer_*` are portable, host-tested data structures.

The 128-event history overwrites oldest entries; latest fault and cumulative
counters are retained separately. Instrumented events cover boot, recording,
SD writes/errors, audio/queue errors, foreground gaps, logger loss, sleep, and
experimental IPC state. History is roughly 11 seconds at the documented recorder
SD event rate, less with other events. All history/counters reset on reboot.

The existing machine-readable line protocol is documented in
[spookyprobe-v1.md](spookyprobe-v1.md): `LOG STATUS`, `DIAG STATUS`, `DIAG LAST`,
`DIAG DUMP`, `DIAG STOP`, plus streamed HELP. Diagnostic schema is `V=1 CORE=7`.
Dump header/EVENT/GAP/END rows preserve sequence/count semantics. One response
line is attempted per foreground service; busy USB retries until five seconds
without an accepted line. Immediate error/STOP replies can be lost when busy.
One request at a time, no request IDs; unrelated output can interleave.

Underlying USB bounds: 256-byte RX ring (255 usable), 64-byte command buffer
(63 characters plus terminator), 256-byte TX buffer. RX overflow keeps an accepted
prefix, drops the rest of that packet, and increments a packet-drop counter;
it does not guarantee that a partially lost command is rejected as a whole.
Overlong lines are discarded through newline and attempt an error response.
Bench must serialize short whitelisted commands. Production protocol work should
add discard-to-delimiter on RX loss before enabling destructive test commands.

Target CDC advertises `0483:5740`, product `Spooky Box USB Test`, with UID-derived
serial. Match a configured identity, not this shared example VID/PID alone.
`Spooky Box USB CLI ready` indicates CDC service, not all peripherals passing.
The UART startup banner precedes `Bringup_Run()`; it is not a full boot verdict.
`DIAG_BOOT` is recorded even earlier and can roll out of the history.

### IPC experiment

`IpcSmoke_Init`, `Service`, `GetDiagnostics`, portable validation/health functions,
and `IpcSmokeCli_HandleCommand` already exist. Both linkers reserve 256 bytes at
`0x38000000`; two 32-byte frames occupy 64 bytes. M7 initializes the NOLOAD mailbox
before releasing M4, with MPU region 7 noncacheable/shareable; HSEM 1 attempts are
nonblocking and HSEM 0 remains boot synchronization. Foreground publishes every
100 ms; lack of peer/ack progress becomes stale after two seconds.

`IPC STATUS` reports versions, TX/RX/ACK, round trips, seen flags, ages, errors,
busy attempts and LINK. `OK` only acknowledges the command; require `LINK=UP`
and progress to declare IPC health. Normal builds return `DISABLED`; mismatch
build intentionally uses M4 ABI 2 against M7 ABI 1. Existing sibling Client does
**not** accept `IPC STATUS` yet. Its response is sent best-effort once, unlike
the deferred DIAG response, so host polling must have finite limits/deadlines.
Blocking SD/bring-up work can stall this heartbeat without a core crash.

### What is not implemented

No `spookybench` scaffold, Pi appliance setup, matched-image manifest, automatic
hardware test runner, persistent crash record, external power control, trace
capture, target test command family, or synthetic input API was found here.
There is no need to complete an existing Phase-1 scaffold in this audit.

## Build and flash evidence

Root CMake (3.22 minimum), Ninja, and ARM GNU build both children using
`cmake/dual_core_firmware.cmake`. Canonical outputs are:

```text
build/<Preset>/firmware/CM7/full_spooky_proto_CM7.elf
build/<Preset>/firmware/CM4/full_spooky_proto_CM4.elf
```

`<Preset>` is Debug, Release, IpcSmoke, or IpcMismatch. All eight ELF paths exist
locally. Each child also produces a map and compilation database. Existence is
not proof of fresh source correspondence or hardware validation; no firmware
rebuild/flash was done for this documentation change. There is no maintained
combined image or deployment manifest. Linker flash ranges are M7
`0x08000000..0x080FFFFF`, M4 `0x08100000..0x081FFFFF`.

FatFs is external, from STM32CubeH7 V1.13.0. The M7 default uses `USERPROFILE`;
the root ExternalProject currently does not forward `STM32CUBE_H7_ROOT`. A Linux
build needs deliberate dependency/path configuration; this is not a bench-host
prerequisite when delivering artifacts built elsewhere. Per-core/old generated
`CM7/build` and `CM4/build` paths are not canonical root-preset outputs.

`platformio/deploy.py` is a SCons/PlatformIO task adapter, not a reusable standalone
library. It supplies `firmware`, `deploy`, `deploy_cm7`, `probe`. Deploy builds,
programs/verifies M7 then M4, resets and exits. `platformio.ini` selects
`tool-openocd@3.1200.0`, CMSIS-DAP SWD, speed 1000 kHz; there is no probe serial
selector. Its subprocess calls have no timeout or structured run artifact capture.

Preserve the important OpenOCD recipe:

- `interface/cmsis-dap.cfg`, SWD, `target/stm32h7x.cfg`, `DUAL_BANK=1`.
- Flash through CM7/AP0 with `DUAL_CORE=0`, including both flash banks. This avoids
  waiting for sleeping/boot-held M4 during reset-halt. Probe uses `DUAL_CORE=1`.
- Defer AP2 examination and remove AP2-dependent examine-end hooks, because D2
  sleep can make AP2 unavailable. These settings must be tested against the PC's
  exact OpenOCD version/scripts, not copied as universally applicable assumptions.
- Existing flash uses `program ... verify`; only successful verification of both
  images plus process success constitutes a paired-flash success.

The script keeps the target config's default reset behavior, with no requirement
for an NRST wire merely to attach. That does not establish recovery from every
sleep/fault or prove the physical reset wiring. The ignored local editor launch
file has an ST-LINK GDB configuration referencing old per-core build paths, plus
generic PlatformIO native `program.exe` entries. These are machine-local/stale
debug conveniences, not a portable bench contract. A bounded standalone reset
command is new work.

## Sibling Spooky Probe inventory and resource ledger

This is already a debugprobe extension, not an empty future fork. Preserve its
CMSIS-DAP and USB CDC functionality. Its logger slice adds a 2048-byte UART queue,
partial USB acceptance handling, disconnect/overflow/error counters, and a
read-only DAP-v2 vendor control request (`0x53`, 40-byte version-1 stats). It does
not add GPIO trace or replace the DAP request implementation. Fixed-baud capture
does not depend on the stats extension and should work with stock compatible
debugprobe; unavailable stats must be reported as unknown/unsupported.

`host/pyproject.toml` packages Python >=3.10 `spookyprobe` 0.1.0, pyserial, optional
PyUSB. It provides `capture`, `diag`, `summary`, `probe-status`. Reuse
`capture.Archive/capture_session`, `protocol.Lines/parse_line/Dump`, and
`client.Client` through a pinned, tested dependency/adapter; do not fork a second
decoder. These files are currently uncommitted, so an immutable distribution or
source snapshot and provenance must precede reproducible installation. A sibling
relative path is useful for local development but cannot be the reproducible
installation contract on either this PC or a later Pi host.

Known gaps to address in an adapter: explicit serial paths only; no port
rediscovery; capture can run/reconnect forever without `--seconds`; archive
thread join has no deadline; disk storage has no quota; client supports only
the four LOG/DIAG commands and rejects producers other than CORE=7. Diagnostic
synchronization drains for at least 5.2 seconds plus a quiet interval, with a
finite request deadline. Preserve raw evidence and that synchronization behavior.

Preliminary ledger below is **observed usage, not permission to allocate the
remainder**. It applies to the local Pico 1 / `DEBUG_ON_PICO=ON` build. SDK and
runtime allocation/teardown require another audit before trace changes.

| Resource | Evidence / occupied use | Constraint or uncertainty |
| --- | --- | --- |
| PIO0 / SM0 | `board_pico_config.h` fixes SWD at SM0; `probe.c` loads `probe.pio` | SWD code does not visibly claim SM0 through SDK allocation API; autobaud dynamically claims an unused SM. Potential collision to investigate, not silently fix during bench work. |
| PIO0 instruction RAM | Generated Pico program length 11; autobaud length 8, loaded dynamically | 19 instructions if resident together; offsets/fragmentation/variant and SM ownership still matter. `probe_oen.pio` is a different board variant, not a second Pico allocation. |
| PIO1 | No direct allocation found in audited application sources | SDK/runtime closure not certified; no trace reservation made. |
| DMA | Autobaud claims data + control channels dynamically and uses shared DMA IRQ0 | Autobaud is activated by special CDC baud setting. No fixed free channel numbers may be assumed. |
| Timers/alarms | FreeRTOS 20 kHz SysTick on core 0, software timer service; SDK time interop enabled | SDK default alarm pool uses hardware alarm 3 unless overridden; effective allocations and USB/SDK consumers require confirmation. |
| Cores/tasks | Core 0 UART priority +3, USB and RP2040 USB watchdog +2; core 1 DAP and autobaud +1; RTOS timers/idle also exist | Neither core is unused. Preserve task scheduling and DAP responsiveness. |
| GPIO | Pico GP2 SWCLK, GP3 SWDIO, GP4 UART1 TX, GP5 RX, GP1 target reset, GP25 USB LED | GP10..13 in old trace plan are proposals only. `stdio_uart_init()` plus enabled SDK UART0 defaults (GP0/GP1) creates a possible GP1 reset/stdio ownership conflict; resolve effective muxing before relying on NRST. Wiring is unverified. |
| USB | VID/PID 2E8A:000C; DAP interface 0, CDC interfaces 1/2, Pico reset interface 3; CDC endpoints 81/02/83, DAP 04/85, EP0 | The USB reset interface resets the **probe**, not target power. No new endpoint allocated for stats (EP0 control transfer). |
| Buffers/RAM | UART queue 2048 bytes; CDC TX 4096 / RX 64; DAP request and response arrays each 8 x 64 bytes plus metadata; vendor buffer macros 8192 each; autobaud 4096-byte aligned sample ring and dynamic 500-entry hash table; RTOS heap 64 KiB, task stacks, RAM-resident firmware | Macros are not a linked RAM budget. Inspect ELF/map and runtime stack/heap peaks before sizing trace history. Do not double-count unused TinyUSB vendor buffers in the custom DAP driver. |

UART queue overflow drops new bytes; disconnected/session-discard bytes are
separately counted. Probe acceptance is not host receipt. Host capture uses 64
slots of 4096-byte payload (256 KiB), drops/counts new chunks on overload, saves
raw bytes and host receive timestamps, and surfaces disk errors. These are
different loss domains from target log drops and DIAG history GAPs.

## Delta from the earlier Spooky Trace plan

1. UART logging, numeric diagnostics, IPC experiment and host capture/decoder now
   exist; do not recreate them or add a competing telemetry protocol first.
2. M4 ownership in the old plan is still future work. Current diagnostics are
   M7 + target USB CDC; initial bench operation needs both serial paths.
3. The probe uses SMP FreeRTOS, autobaud PIO/DMA, and existing reset support.
   Old assumptions about unused cores/PIO/DMA/pins are unsafe.
4. Move unattended paired flash/reset/boot evidence ahead of one-pin trace.
   Preserve the later one-pin -> multi-pin -> PIO -> DMA -> triggered history
   progression and CMSIS-DAP regression gate.
5. Add a local PC orchestration layer, stable CLI/JSON results, image provenance,
   bounded artifacts, locking and finite recovery. Compilation stays independent.
6. Future trace markers remain direct BSRR writes, compile-time removable, with
   no HAL/printf/lock/allocation in M7 hot paths. Actual pins are still unassigned.

## Proposed host layout and Phase 1

```text
host/
  pyproject.toml                 # spookybench console entry point
  spookybench/
    __init__.py, __main__.py
    cli.py                      # argparse; no hardware work during parsing
    config.py                   # validated JSON bench profile, identities, limits
    result.py                   # schema v1 and exit mapping
    openocd.py                  # fixed operation templates, subprocess deadlines
    serial_io.py                # identity discovery and spookyprobe adapter
    platform_io.py              # Windows discovery/locking/process lifecycle;
                                # explicit Linux adapter later
    artifacts.py                # manifests, caps, atomic completion
    runner.py                   # initially command dispatch/lock; tests in Phase 2
    fake.py                     # deterministic transports and injected clock
  tests/                        # Python tests, transcripts, fake process outputs
  examples/bench.windows.example.json
docs/spooky-bench-plan.md
docs/spooky-bench-setup.md        # added with implementation, validated Windows setup
```

Keep native C tests under existing `tests/`. Run directories live in configurable
bench-owned storage outside the source checkout. No Docker, database, web server,
elevated runtime, or daemon is necessary. Start with a local Python environment,
pyserial, the pinned Spooky Probe package, and the existing Windows OpenOCD
installation. Record and validate the actual executable and script versions.
Optional PyUSB probe counters can follow; unsupported counters must not block
ordinary serial capture or diagnostic queries. Validate their backend/driver
requirements separately without replacing working CDC/CMSIS-DAP drivers merely
to obtain statistics. Document any required one-time driver setup; normal bench
commands should run as the current non-administrator user.
No network listener, public exposure or GitHub-specific logic.

Keep profiles, manifests, protocol parsing, test orchestration, JSON results,
deadlines, and artifact formats portable. Isolate COM-port discovery, locking,
and process lifecycle behind a small platform adapter. Do not assume `fcntl`,
POSIX signals, fork inheritance, or Linux device paths in shared code. Test
Windows process spawning, bounded termination, handle release, and paths with
spaces. Linux/udev/SSH setup belongs to a later deployment milestone, not Phase 1.

Phase 1 is delivered in two independently useful slices:

- **1A: observe.** Profiles, identity-based discovery/status, bounded UART capture,
  and LOG/DIAG queries through the existing Spooky Probe client. Establish the
  JSON contract, run artifacts, limits, locking, and fake transport tests here.
- **1B: control.** Bounded SWD probe, paired-image validation/flash, and reset,
  reusing the existing OpenOCD recipe. Validate flash/reset/capture together before
  Phase 2 introduces automated boot/IPC verdicts.

Complete Phase-1 command set (proposed CLI syntax):

| Command | Initial contract | Proposed bound |
| --- | --- | --- |
| `status` | Validate configuration, list selected devices/tool versions and capabilities; do not claim target boot or power from USB enumeration | 5 s tool calls, 10 s total |
| `console --seconds N` | Receive-only probe CDC capture with raw bytes and timestamp index, finite duration, loss/truncation metadata | Require finite 0 < N <= 3600, <=100 ms reads, 2 s bounded shutdown |
| `diag status\|last\|dump` / `log status` | Whitelisted LOG/DIAG requests on the separate target CDC port; preserve raw responses, synchronization and incomplete-dump semantics | 15 s total including synchronization and cleanup; <=8 s request clipped to the total, <=1 s writes; no automatic retry |
| `probe` | Bounded SWD attach and target examination using the existing AP2 workaround; report which cores were actually observable | 15 s total; sleeping M4 is explicit, not fabricated failure/success |
| `flash M7.elf --cm4 M4.elf` or `flash --manifest build-info.json` | Require a matched pair, validate ELF load ranges and hashes, program/verify both, reset/run on success | 120 s total; one attempt initially |
| `reset` | Paired system reset/run through the validated OpenOCD recipe; success means reset command completed, not boot passed | 15 s total |

Limits are initial configurable policy with hard maxima, not measured host timing.
Use monotonic absolute deadlines, bounded stdout/stderr readers, and a finite
terminate/kill/reap sequence (2 s grace). A stuck archive/filesystem must not make
shutdown wait forever; supervise capture in a killable subprocess if retaining
the current Archive implementation. Mark forcibly stopped evidence incomplete.
No automatic retry in Phase 1. Power/trace/crash capabilities report unsupported;
do not provide a successful placeholder command. Bare single-image flashing is
rejected initially to prevent an accidental mixed pair; raw BIN/address support
can wait. Existing manual `deploy_cm7` remains outside the bench test contract.

Profiles contain board ID, expected probe serial, probe CDC selector, separate
target CDC selector, executable/scripts paths, adapter speed, artifact root and
limits. On Windows, discover ports through pyserial and match configured USB
serial plus interface identity where available; resolve the current COM name
again after reconnect/reset. Permit explicit COM paths for controlled setups,
but never assume a COM number is a persistent identity. Zero matches is missing,
multiple matches is ambiguous; never select the first available serial port.
Keep probe CDC and target CDC selectors distinct and reject a profile that
resolves both roles to the same port. A later Linux adapter can support
`/dev/serial/by-id` alongside the same identity selectors.
Stock probe counters/version may be unavailable. Record
unknowns as null/reasons, never infer firmware revision from USB descriptor alone.

Hold a per-bench OS-backed lock for each action and entire test run, including
capture/reset/flash. Implement and test Windows locking first; a stale pathname
alone must not leave the bench permanently locked after a process exits.
External debugger/terminal contention must fail clearly;
never kill unrelated owners. Allow only enumerated backend operations. Use argv
without a shell, validated numeric values and trusted board templates. Tcl is
another interpreter: stage images under bench-generated safe basenames and test
path quoting; copying `_tcl_path()` alone does not establish hostile-input safety.
Disable OpenOCD GDB/Tcl/telnet listeners for batch operations. Verify actual
installed syntax/scripts during implementation and pin the validated version.

### JSON and simulation

`--json` emits exactly one result object to stdout; progress goes to stderr/raw
artifacts. Version 1 fields: `schema_version`, `command`, `result`, `reason`,
`execution` (`hardware` or `simulated`), `metrics`, `artifacts`, `timestamps`
(`started_at`, `ended_at`, `duration_ms`). `result` is pass/fail/error/unsupported;
reason is a stable code or null. Checks under metrics distinguish pass/fail,
not_checked, disabled and unknown rather than converting absent evidence to true.

Exit codes: 0 requested operation succeeded; 1 measured test/operation failure;
2 invalid invocation/configuration, missing/ambiguous device, transport/tool or
artifact error; 3 unsupported capability; 130 interrupted. JSON mode must cover
argument-validation failures too. Nonzero OpenOCD exit and verification failure
must never become pass. Simulation uses identical schema with
`execution=simulated` and explicitly simulated reason/artifacts; it is no hardware
acceptance evidence. No raw OpenOCD-command escape hatch in the agent API.

Phase-1 tests exercise parsing, NaN/infinite/negative limits, JSON/exit mapping,
device ambiguity, manifests/hash mismatch/wrong core ranges, Tcl-safe staging,
process timeout/nonzero/partial-flash failures, capture overload/disconnect/disk
failure, COM renumbering, role collisions, Windows lock contention, process
spawning/termination, and deadline cleanup using fakes. Test serialized
results and failure semantics, not decorative human output.

## Phase 2: smallest end-to-end smoke test

Proposed invocation: `spookybench --json test boot-smoke --manifest build-info.json`.
Require a known matched **IpcSmoke** pair, PC-to-target CDC cable, validated flash
and reset, and baseline peripheral wiring. This tests foreground boot + diagnostic
service + two-core IPC, not full SD/audio/radio/display correctness.

1. Acquire lock, validate inputs, reserve disk budget, write run-start metadata.
2. Open probe UART capture **before** flash/reset; wait for capture-ready signal.
   Preserve pre-reset bytes separately. Current probe deliberately discards UART
   bytes while no CDC terminal is active, so opening after reset loses boot logs.
3. Flash/verify both images (120 s total), then reset/run (15 s). Record stage
   timing. Flash may itself reset: preserve those epoch boundaries.
4. Rediscover target CDC after final reset (initial 45 s boot window, to calibrate
   on hardware). Preserve boot bytes; synchronize one command client using the
   existing quiet/settling rule. UART banner is supporting evidence, not verdict.
5. Query `DIAG STATUS` with schema/core checks and `IPC STATUS` twice at least one
   second apart. Require LINK UP, ABI 1/1, both seen flags set, error fields zero,
   and forward modulo-32-bit TX/RX/ACK/ROUNDTRIPS progress. Bound each request to
   8 s and IPC acquisition to at most three pairs / 30 s; late replies cannot
   extend the overall deadline. Missing READY text alone must not defeat valid
   structured evidence; absent UART evidence remains an explicit failed check
   when the profile requires a working diagnostic UART.
6. Save LOG/DIAG STATUS/LAST and, within remaining budget, DUMP. Incomplete dump
   stays incomplete. Finalize result and capture; release ports/lock. Use a total
   run deadline of 240 s including cleanup, subordinate stages clipped to it.

M7 liveness comes from a fresh structured response after reset; M4 liveness is
inferred from validated echo/ack progress, labeled as such. Default Debug cannot
pass the dual-core criterion; return `ipc_disabled`, not a false M4 pass. A later
separate basic-boot profile may explicitly report M4/IPC not_checked.

Start without target firmware changes: extend the existing host parser/client
whitelist for IPC and collect two snapshots. Maintain the no-ID single-request
discipline; on ambiguous response/reset, fail the session rather than reuse it.
The minimum optional target extension, if bench evidence needs stronger identity,
is a bounded `DIAG INFO` response through existing deferred diagnostics containing
protocol version, build ID, enabled capabilities and readiness state. M4 image ID
would require a deliberately versioned IPC extension. Do not report a manifest
build ID as a target-reported revision. Request IDs become appropriate when
asynchronous RUN_TEST work is added, not by inventing a second command system.

Success leaves the ordinary prototype running (audio can be active), recording
not started by this test; this is not a power-off state. On timeout, collect
bounded diagnostics/SWD evidence first, then at most one reset/run cleanup only
if that reset behavior has passed its hardware gate. On flash/verify failure,
do not intentionally run a partly updated pair. Record actual final state as
halted/running/unknown; unknown or failed cleanup means HUMAN_REQUIRED. Never
destroy crash evidence with an automatic reset before attempting collection.

## Artifacts, retention and finite recovery

Each run has a generated UTC timestamp + random ID, with revision/test metadata
in files rather than relying on directory names:

```text
runs/<run-id>/
  metadata.json                 # board/probe/host identity, tool versions, stages
  build-info.json               # preset, source SHA + dirty snapshot ID, hashes
  firmware/CM7.elf, CM4.elf      # exact verified artifacts
  flash.log, reset.log           # bounded OpenOCD stdout/stderr
  uart/raw.bin, chunks.jsonl, session.json
  diagnostics.jsonl
  test-results.json             # authoritative final schema result
  crash.json, trace.spookytrace  # only when supported and actually collected
```

Record both firmware hashes, supplied and target-reported revisions separately,
compiler/build flags, Spooky Bench/Probe package revisions, probe firmware identity
when known, OpenOCD/Python versions, profile hash, USB serials, start/end UTC and
monotonic duration, stage attempts, counters/loss, timeout and final target state.
Dirty code requires an archived patch/source snapshot identifier; SHA alone is
insufficient. The bench runner consumes prebuilt images and needs no compiler,
whether it shares this development PC or later runs on a dedicated Pi.

Initial bounded-storage design: configurable 256 MiB/run, 2 GiB total bench
quota and 512 MiB minimum free-space reserve, with small per-stream caps. These
are proposed defaults to tune for the selected PC artifact volume (and later a
Pi's storage). Reserve worst-case budget
before flashing; count incomplete runs and staging files. Stop capture visibly
at cap, set truncation/incomplete evidence, and fail a test whose required evidence
is lost. Bound in-memory queues and output readers independently of disk limits.
No automatic deletion in Phase 1: refuse new work when capacity is insufficient.

Later retention policy: latest 20 successes and 50 failures, maximum age 30 days,
all subject to global byte cap. Failed runs are preferentially retained, never
unbounded. Cleanup must first offer a dry-run inventory, lock against active runs,
accept only completed manifests created by this tool beneath the canonical run
root, reject symlinks/junctions/path escapes and never follow manifest paths for
recursive removal. Pinned/active/unknown directories are excluded; inability to
free space stops admission. Test these rules before implementing deletion.

Phase 4 may add one SWD reconnect retry, then one verified power-cycle and one
final attempt only if supported. Each stage and whole job retain hard deadlines.
Store attempt history without overwriting the first failure. UART silence, SWD
failure, incompatible firmware, disk-full, and observed target failure are distinct
reasons. Exhaustion returns HUMAN_REQUIRED; no watchdog loop repeatedly reflashes.

## Later capabilities and decision gates

**Phase 3:** IPC smoke reuses the handshake measurements. SD basic adapts existing
`SD STRESS` write/verify work with a dedicated scratch filename, capacity check,
bounded size/passes, known card and no active recording; never blanket `SD CLEAN`.
Report bytes written/verified, mismatch offset, duration and measured write
latencies where available; do not relabel existing millisecond maxima as
microseconds or claim CRC metrics that are not implemented. Audio basic requires
installed radio/codec/mic hardware, bounded recording, queue/error deltas and WAV
inspection. Peripheral tests require explicit wiring/expected IDs and restore
mute/display/radio state as documented. Existing blocking bring-up calls require
service-budget work before treating them as cancellable target tests.

Test-only injection waits for a decoded-event API above GPIO/debounce and below
M7 behavior mapping. Use compile-off-by-default `SPOOKY_TEST_INPUT`, capability
advertisement, explicit runtime enable, bounded event queue with reject-new/full
response and overflow counters, source=test tagging and release-all cleanup.
Validate encoder/button IDs and deltas. Both physical and injected events enter
one behavior interface; do not add test branches throughout application logic.
No production injection receiver is proposed for Phase 1.

**Phase 4 crash investigation:** fault handlers currently loop; DIAG LAST is a
volatile runtime fault summary, not a retained crash record. The IPC mailbox is
NOLOAD but explicitly cleared by M7 on every initialization. It cannot be reused
as persistent crash storage. NOLOAD alone proves neither hardware reset retention
nor protection from other startup/debugger writes.

Candidate regions to investigate are a separately reserved shared SRAM4 area and
backup SRAM. ST identifies backup SRAM in its
[STM32H755 description](https://www.st.com/en/microcontrollers-microprocessors/stm32h755xi.html).
The applicable primary reference is
[RM0399](https://www.st.com/resource/en/reference_manual/rm0399-stm32h745755-and-stm32h747757-advanced-armbased-32bit-mcus-stmicroelectronics.pdf).
Its full reset/retention tables were not retrievable through the web reader in
this audit (document size limit); **no SRAM location or survival guarantee is
selected**. Complete that primary-document review, board VBAT/power wiring and
startup/cache/access audit before reserving memory.

| Reset/power class | Current crash-survival contract | Required evidence |
| --- | --- | --- |
| Core-only debugger reset | Unsupported/unknown | Core reset versus domain reset effects, peer writer behavior |
| Paired software reset, NRST, watchdog | No retained record implemented; survival unknown | RM reset-domain table, actual OpenOCD reset mode, before/after sentinel and fault records |
| STOP / Standby / D3 power change | Unknown, not interchangeable with reset | Selected SRAM supply/retention bits and prototype sleep behavior |
| POR/BOR or removal of all target supplies | Ordinary RAM evidence must not be relied upon | Rail measurements; backup SRAM requires verified backup supply/configuration |
| Backup-domain reset / VBAT loss / tamper | Backup retention not promised | Applicable RM behavior, board wiring and invalid-record handling |

Future per-core fixed records need magic/version/length, validity committed last,
reset reason, boot epoch, core ID, stacked PC/LR/xPSR, fault registers and selected
event/counters; no allocation/formatting in fault capture. SWD fallback reads only
a known symbol/address/length tied to the exact image manifest, validates the
record and records halting side effects. Read before reset when UART is dead;
inaccessible memory is an error, never evidence of no crash.

**Power:** reserve capability `{supported:false,state:"unknown"}` now. Future
states on/off/transitioning/fault/unknown require feedback and finite settle
timeouts. A future external controller (for example Pico, or Pi GPIO in a later
deployment) commands a load switch/MOSFET/USB switch/relay,
never sources target load. The current external 5 V, battery, babysitter USB,
ST-LINK and signal connections can create multiple feed/back-power paths. Verify
the entire power tree and discharge/isolation before claiming a cold cycle.
No power hardware or control command is implemented in this change.

**Phase 5:** retain the old trace milestones, with a fresh resource ledger and
CMSIS-DAP/UART regression at each step. Pre-trigger duration derives from buffer
capacity and observed event rate, not a promised fixed number of seconds. Capture
overflow, timing accuracy, trigger/post-trigger window and re-arm must be measured.

**Phase 6:** only after local Windows commands pass, consider a dedicated
Raspberry Pi/Linux bench host, remote client, or private runner. Reuse the Python
core, profiles and JSON/artifact contracts; add and test Linux discovery, locking,
process cleanup, USB permissions and storage configuration. Validate locally on
that host before adding SSH under a restricted bench user. No public listeners/port-forwarding,
unrelated credentials, arbitrary OpenOCD strings or raw GPIO API. Future forced
SSH commands/service policy must also restrict config paths/tool paths, not just
the top-level CLI name. GitHub Actions is one possible client, never a core dependency.

## Risks and unanswered questions

1. Which target/probe working-tree snapshots are the intended baseline? Existing
   hardware success documented for recording does not validate these new slices.
2. Which exact Pico image is physically installed, and are its UART changes and
   vendor counters hardware-tested? USB descriptor version is insufficient.
3. Which Windows Python environment, OpenOCD executable/scripts, USB drivers and
   device identities will the PC profile use, and where will bounded run artifacts
   live? Existing tooling is a starting point, not bench acceptance evidence.
4. Is the target's separate application CDC cable available on this PC? If not,
   Phase 2 needs a target transport extension; forwarding over UART is not present.
5. Is GP1 physically wired to H755 NRST, and does SDK UART0 setup interfere?
   Which reset method works in normal, sleeping, held-core and faulted states?
6. Does SWD SM0's lack of SDK claim collide with autobaud allocation? Fixed 115200
   avoids invoking autobaud but does not resolve this regression risk for trace.
7. What are effective alarm allocations, PIO lifetime ownership, linked RAM usage,
   stack/heap peaks and USB/DAP behavior under capture load? Ledger is preliminary.
8. What boot deadline covers real peripheral startup? Is a live M7 loop + IPC
   enough for the desired first smoke test, or must individual peripherals pass?
9. How should target-reported image IDs and boot epochs be introduced without
   breaking v1 clients? Current protocol has neither request IDs nor firmware ID.
10. Are firmware builds reproducible with the external FatFs/toolchain versions,
    and how will both dirty source snapshots be archived/distributed?
11. M4 diagnostic/UART ownership, product IPC/input queues, I2C2 ownership and
    independent core recovery remain unimplemented decisions, not Phase-1 fixes.
12. Crash SRAM reset survival, board VBAT and true target power isolation are
    unresolved; no guaranteed post-power-cycle postmortem is possible yet.
13. Existing host capture's unbounded close and optional reconnect, target USB
    partial-command overflow, and blocking foreground work must be accounted for
    before unattended operation. Native tests do not prove hardware scheduling.

## Validation performed and explicit hardware checklist

Offline evidence this session:

- Reconfigured/built native target tests with installed STM32Cube CMake 4.3.1,
  MSVC and Ninja; CTest **2/2 pass** (logger/diagnostics and IPC protocol).
- Sibling `python -B -m unittest discover -s ../spookyprobe/tests -p test_host.py -v`:
  **14/14 pass**, without writing Python bytecode in the sibling.
- The ignored `build/run-host-tests.cmd` initially failed because `vcvars64`
  selected bundled CMake 3.20, below required 3.22; explicit CMake paths resolved
  it. This helper is local generated material, not a portable installation recipe.
- No STM32/Pico firmware build, flash, reset, serial capture, electrical test or
  real hardware smoke test executed. Existing ELF presence is inventory only.

Every unchecked item below **requires hardware validation**; human supervision is
required for initial wiring/power checks. Later validated bench operations may
execute the repeatable portions unattended.

- [ ] Verify target/probe/common-ground voltages, SWD and PE8->GP5 UART wiring,
      both USB data paths, all board bodges/supply jumpers from BRINGUP.
- [ ] Record the PC/Python/OpenOCD environment and actual probe/target serials and
      firmware versions; confirm non-administrator access, distinct CDC roles,
      correct selection with multiple devices, and rediscovery after COM changes.
- [ ] Validate the installed OpenOCD recipe/adapter speed: M7/AP0 attach, optional
      M4 visibility, D2 sleep/AP2 behavior, then matched dual-bank flash/verify.
- [ ] Confirm bad image/verify/disconnect failures remain nonzero, preserve logs,
      never report boot success and do not intentionally run a mixed image pair.
- [ ] Establish paired reset semantics, NRST wiring/muxing and both-core restart
      in normal/sleep/fault/held-core conditions; record unsupported cases.
- [ ] Capture before reset; verify UART boot bytes, raw fidelity, host timestamps,
      session boundaries, target CDC re-enumeration and no wrong-port commands.
- [ ] Run normal Debug regression: radio/headphones, volume/jack mute, sensors,
      short/long three-channel recordings and WAV checks; save diagnostic counters.
- [ ] Exercise logger overload, UART errors and target/probe/host queue loss;
      confirm continued M7/audio progress, recovery and distinct loss accounting.
- [ ] Check HELP/LOG/DIAG responses, slow/overwritten dumps, missing END and
      disconnect recovery with actual USB backpressure and interleaved streams.
- [ ] Verify normal charging-sleep drain, 10-second wake, five-minute cadence and
      current draw; experiment builds must continue to reject SLEEP START.
- [ ] Run IpcSmoke handshake/progress/reset/cold-start and recording-load tests;
      halt/resume M4 for stale/recovery; IpcMismatch must fail as incompatible.
- [ ] Prove one local PC `boot-smoke` invocation returns correct JSON/exit code and
      complete evidence across repeated resets, absent probe/CDC and boot timeout.
- [ ] Run SWD attach/halt/resume/program while capturing UART; exercise USB
      reconnect, CDC line coding/break/autobaud with CMSIS-DAP regression checks.
- [ ] On this PC, simulate slow/full storage in a bounded test area; verify queue/drop
      metrics, cap/admission refusal, interrupted-run evidence and finite shutdown.
- [ ] Validate Windows lock contention, process timeout/termination, released
      serial handles, and interrupted-run reporting; account for host sleep and
      USB disconnects without misclassifying them as measured target failures.
- [ ] Validate future SD scratch cleanup/card-full/removal failures; audio and
      peripheral tests only with their prerequisites and measured counter criteria.
- [ ] Before ownership migration, validate M4 UART service and M7 nonblocking
      forwarding, saturation/restart behavior, and whole-controller ownership.
- [ ] Before enabling injection, test physical/injected equivalence, full-queue
      rejection, release cleanup and production compile-out on real controls.
- [ ] Before crash support, fault each core deliberately; read via diagnostics and
      SWD with UART disabled; test the full reset-survival matrix above.
- [ ] Before power control, verify external switching/feedback, discharge, every
      alternate supply/back-power path, stuck-switch faults and safe default state.
- [ ] Before each trace increment, measure known pulses, ordering/overflow,
      PIO/DMA under DAP load, BSRR overhead/audio regression, trigger history and re-arm.
- [ ] Before any Pi/Linux deployment, validate its platform adapter, USB access,
      tool versions and storage limits locally, then test the same CLI over SSH.
- [ ] Before unattended jobs, verify bounded recovery exhaustion,
      HUMAN_REQUIRED, actual final target state, permissions and artifact provenance.

## Implementation status and next slice

**Host-only Phase 1A is implemented on this Windows PC**: profiles, device
discovery/status, bounded console capture, LOG/DIAG queries, JSON/exit contract,
supervised deadlines, Windows locking, capped artifacts, and fake transport
tests. The installed Spooky Probe package is verified against the committed
`ce039ca` source snapshot. Configured runs archive the actual bench module
sources/hash, including local edits. See [setup](spooky-bench-setup.md) for exact
commands, result semantics, limits, and first live acceptance.

The implementation adds no target/Pico firmware changes, Pi setup, power control,
trace or CI. No-profile status performs discovery without opening serial ports;
with a profile, status validates both CDC selections. OpenOCD checks are deferred
to Phase 1B and are explicitly not_checked. Phase 1A holds one board lock per
operation; combined capture/diagnostic orchestration belongs to the later runner.

Follow with **Phase 1B** for SWD probe, paired-image validation/flash, and reset,
using the existing Windows OpenOCD recipe and testing partial-failure cleanup.
Hardware acceptance is the corresponding Phase-1 checklist above. Implement
Phase-2 boot-smoke separately after flash/reset/capture are proven. A dedicated
Pi remains a later deployment choice, not a prerequisite for agent-accessible
hardware testing.

Validation: 22/22 bench tests pass, including fake serial/decoder scenarios,
storage refusal, Windows spawned CLI execution and forced worker termination
with lock release. Package installation and `pip check` pass. The test environment
uses Python 3.12.14 and pyserial 3.5. All four LOG/DIAG query paths and configured
device selection passed initial live checks; remaining hardware checks are tracked in the
[live results](bench-results-2026-09-24.md). Discovery alone is not target health evidence.
