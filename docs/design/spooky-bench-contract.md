# Spooky Bench CLI contract

The result, safety and evidence contract of the `spookybench` command surface, which
developers and agents invoke identically
([decision 0002](../decisions/0002-windows-first-spooky-bench.md)). Installation and
operation are in the [setup](../procedures/spooky-bench-setup.md) and
[controls](../procedures/spooky-bench-controls.md) procedures; each automated test has
its own procedure with pass criteria. Target-side commands are in the
[USB CLI contract](usb-cli.md) and [Spookyprobe v1](spookyprobe-v1.md).

## Result object

`--json` produces exactly one result object, including invocation errors. Help
remains ordinary CLI help. Fields are `schema_version`, `command`, `result`,
`reason`, `execution`, `metrics`, `artifacts`, and `timestamps`; errors also have
bounded `detail`. `execution=simulated` is never hardware acceptance evidence.
`execution=offline` identifies local analysis of an existing artifact: it does not
discover devices, acquire the board lock, create a run directory, or establish new
hardware provenance.
Successful simulations carry `reason=simulated`; simulated failures preserve
their failure reason. Query success does not mean HAS_FAULT=0: inspect the
returned response. Target health and final target state remain explicitly
not_checked/unknown instead of inferred from serial enumeration or silence.
OS failure to terminate a worker is reported through `metrics.human_required`.

| Exit | Meaning |
| --- | --- |
| 0 | Requested operation completed (not a target-health verdict) |
| 1 | Invalid protocol response, OpenOCD/verify failure, capture loss, or evidence cap reached |
| 2 | Invocation/configuration, dependency, missing/ambiguous device, port, storage, or timeout error |
| 3 | Unsupported operation |
| 130 | Interrupted |

Power, trace, and crash collection return `unsupported` (exit 3). Windows is the only
supported control platform; the Linux fallback locking path is not bench-qualified.

## Device selection

Selectors match USB identity (VID/PID plus serial number, optionally `interface`) or,
for a controlled setup, an explicit `{"port":"COMn"}`. An identity-based selector
discovers the current port on each new standalone observation. Missing/ambiguous
matches and probe/device role collisions are explicit errors. Status with a profile
requires both devices; console requires the probe; diagnostic commands require the
target; controls require the Pico identity, not an enumerated target CDC. No command
chooses the first available COM port or silently ignores a supplied selector field.
Spooky Bench does not terminate unrelated applications or take their port handles.

## Board ownership

Only one operation may own a board at a time, including console capture. Boot smoke
owns capture, controls, and diagnostics under one lock. IPC load owns one target CDC
session for recording progress and its in-load IPC requests, then uses bounded
diagnostic sessions before and after it. WAV inspection owns the target CDC and SD
reader for the full acknowledged binary transfer; it cannot run during recording or
another bench operation. SD basic refuses an active recording and a pre-existing
`SDTEST.BIN`, verifies card identity before and after the test, and never issues
`SD CLEAN` on unknown scratch data; its explicit stop path removes only the file
created by that run. The recording regression holds one lock across every stage it
composes; it never deletes or rewrites a recording on the card. `wav align` is
offline and does not acquire the board lock. A second board operation reports
`bench_busy`.

The per-user OS lock is released when its process exits. Its stale file is harmless.
A second lock serializes quota accounting for a shared artifact root. External tools
do not participate in those cooperative locks.

## Serial behavior

Console is receive-only at 115200 8N1, without flow control. It saves arbitrary
bytes, including invalid UTF-8 and partial lines. It never sends a command to
the Pico UART bridge. Diagnostic commands use the separate target CDC, wait
at least 5.2 seconds plus a quiet interval to discard no evidence but avoid
stale replies, and send exactly one whitelisted command. Multi-step tests sequence
these one-request sessions internally while retaining the board lock. Original
diagnostic bytes are saved as base64 alongside the Spookyprobe decoder output.
Missing END stays incomplete; an error ends the session. There is no retry,
automatic reset, or reconnect.

## Time budgets

| Operation | Bound |
| --- | --- |
| `status` | 10 s total |
| Diagnostic query | 15 s including settling, request and cleanup; a request gets at most 8 s |
| `console` | `0 < seconds <= 3600`, plus 12 s for startup and cleanup |
| `probe`, `reset` | 15 s total |
| `flash` | 120 s including preflight, UART capture, tool execution and cleanup |
| `test boot-smoke` | 240 s supervisor deadline |
| `test ipc-load` | 10..600 s duration, plus 60 s |
| `wav inspect` | 600 s default; explicit `--timeout` up to 3600 s |
| `wav align` | Synchronous offline computation; no supervisor deadline or hardware access |
| `test sd-basic` | 30..1800 s deadline, 180 s default |
| `test recording-regression` | 10..600 s duration; default deadline 450 s + 7 x duration (+60 s for `loopback`); explicit `--timeout` 300..7200 s |

Serial reads have 100 ms and writes 1-second timeouts. Two seconds of each total are
reserved for forced termination/reaping. Capture archival uses a bounded
64 x 4096-byte queue in the supervised process; a stuck writer cannot hold the
supervisor waiting on archive close indefinitely.

## Offline WAV alignment

`wav align --wav <path>` validates an existing recorder WAV and measures microphone
lag against the average of its radio channels. Positive lag means the microphone
follows the radio. The command uses first-difference pre-whitening, a decimated coarse
search, and a full-rate refinement in independently reported windows. Window length,
hop, and maximum lag are explicit CLI parameters. The command emits no run artifacts;
the source WAV retains the provenance of the earlier `wav inspect` operation.

A passing result means that at least half the windows, and at least two windows,
exceeded both correlation and peak-confidence thresholds. It does not by itself
qualify clock drift. `endpoint_delta_frames` and `endpoint_delta_ppm` report the raw
first-to-last change. Drift uses the polarity held by at least 75% of detected windows;
opposite-phase peaks remain visible and are counted in `drift_excluded_windows`.
`drift_frames` and `drift_ppm` are populated only when the dominant-phase lag is
monotonic or stable within one frame. Otherwise `drift_reliable=false`, the drift
values are null, and `drift_reason` identifies `phase_ambiguous` or
`lag_not_continuous`. Raw windows remain in the result for review.
The bench procedure defines the controlled acoustic stimulus and evidence requirements.

## Recording regression

`test recording-regression` composes the existing runners in one run directory:
boot smoke, a prerequisite session, IPC load, WAV inspection of the file the
recorder reported, an accounting cross-check, optional loopback alignment, and a
post-transfer health session. `metrics.stages` lists each stage with its result and
reason; the first non-pass stage decides the verdict. Each stage's full metrics are
archived as `stage-<name>.json`, and stage artifacts are prefixed with the stage
name. The result keeps a bounded summary.

The result separates three kinds of evidence. Gates decide the verdict (`checks`).
Observations are reported without a verdict (`observations`: radio left/right
correlation and a `mono_like`/`distinct` label, radio/microphone correlations,
clipped samples; `prerequisites`: tuning and volume state). Human checks stay
pending (`human_checks.listening`). The runner refuses before flashing when the
profile `run_bytes` cannot hold the expected WAV plus 16 MiB. A failure after
`RECORD START` reports the file left on the card in `retained_recording`.

## Process supervision

Worker supervision uses Windows-compatible `spawn`, bounded shared result memory,
and terminate/kill/reap rather than unbounded thread joins in the CLI process.
Python documents that [forced process termination skips cleanup](https://docs.python.org/3/library/multiprocessing.html#multiprocessing.Process.terminate).
On forced termination, the CLI reports incomplete evidence and any known run
directory; it does not attempt further writes to a possibly stuck filesystem.
Normal deadlines are not a guarantee against a hung OS.

Each control worker is assigned to a parent-owned Windows kill-on-close job
before it can start OpenOCD. Forced worker termination or parent exit therefore
also terminates descendants. Unsupported job assignment refuses the operation.
OpenOCD runs from argv without a shell, as a hidden process, with merged raw
stdout/stderr capped at 1 MiB and a 16 x 4096-byte queue with pipe backpressure.
Absolute deadlines bound the tool, including output draining.

## Tool and dependency pins

`host/spookybench/openocd-lock.json` pins the OpenOCD executable, bundled DLLs, and
the required Tcl files; drift fails before attaching. Scripts are copied into the run
directory after verification, and tool identity and hashes are recorded. The profile
accepts only executable/scripts paths, `serial_number` and `adapter_khz` (integer
50..4000). Paths are separate process arguments, never interpolated into Tcl. The
backend is CMSIS-DAP USB bulk; GDB, Tcl and telnet listeners are disabled. There is no
arbitrary command or config-file escape hatch.

`probe-lock.json` pins the installed Spookyprobe package to a verified source commit
and checks its modules on each operation, normalizing CRLF/LF. A different source
snapshot fails explicitly, even if its package version is unchanged. Every configured
run archives the bench module sources and their combined hash; it does not label
dirty files as a clean Git revision.

## Build manifest

Flashing accepts only a manifest-declared CM7/CM4 pair. The manifest declares
`schema_version=1`, `build_id`, `preset`, `source_revision`, `dirty`,
`source_snapshot_sha256`, and exactly CM7/CM4 images with path and sha256. Image paths
resolve relative to the manifest. Each ELF is limited to 16 MiB; it must be ELF32
little-endian ARM executable with valid program headers, nonoverlapping file-backed
LOAD ranges inside its assigned 1 MiB flash bank, and expected stack/reset vectors.
Zero-file-size RAM sections are not flashed. All hashes and ELF checks finish before
hardware access, and the exact checked bytes are staged under the run's firmware
directory. Single images, raw BIN/address input and automatic retries are not
supported.

The manifest is a supplied build-provenance declaration, not cryptographic proof that
the cores are semantically compatible, and it is not reported as target-asserted
identity. No target-reported firmware identity exists. Firmware hashes, declared
provenance and load ranges are reported separately from target health.

## Control semantics

`probe` examines M7/AP0 and attempts M4/AP3 without requesting halt/reset. AP2 is
deferred. An unavailable M4 is explicit; sleep, boot gating and access failure cannot
be distinguished by this observation. A passing probe means M7 was observable, not
that both cores are healthy.

`reset` issues system reset/run through M7/AP0 using SYSRESETREQ. A successful reset
means the reset command completed. `control.final_cm7_state` is the OpenOCD
observation; M4 and aggregate `final_target_state` remain unknown when not observed.

`flash` resets/halts M7, writes and verifies CM7, then writes and verifies CM4, and
reaches reset/run only after both verifications succeed. Programming uses M7/AP0 for
both banks to avoid waiting for a sleeping M4. On a Tcl failure it attempts M7 halt
without another reset; it never intentionally resumes a partly updated pair.
Failure, timeout or interruption after a control attempt reports unknown paired state
and `human_required`.

These semantics follow OpenOCD's documented
[target examination/state commands](https://openocd.org/doc/html/CPU-Configuration.html)
and [reset commands](https://openocd.org/doc/html/General-Commands.html), checked
against the pinned interpreter as well as offline tests.

Reset and flash open receive-only Pico UART capture first, wait for archive readiness,
then keep capture active through the tool operation plus up to two seconds afterward.

## Storage policy

Default policy: 256 MiB/run reserved at admission, 2 GiB/root total, 512 MiB
free-space reserve, 64 MiB UART bytes and 8 MiB diagnostic JSONL. Unknown and
incomplete files count toward quota. Index rows and source/metadata also count; UART
reservations are conservative, so a run can stop below its raw byte cap. Limits are
configurable in the profile (`run_bytes`, `total_bytes`, `min_free_bytes`,
`uart_bytes`, `diag_bytes`). No automatic deletion occurs. Other processes can still
consume free disk after admission; resulting write errors are surfaced. Memory,
streams, discovery inventory, and result sizes have independent bounds.

Artifact roots must be private to this tool and user: symlinks/junctions are
rejected, but these checks are not a security boundary against another process
changing directories during a run.

## Run artifacts

```text
<artifact_root>/<timestamp-id>/
  metadata.json, profile.json, devices.json
  bench-source.json             # exact normalized runner module sources
  test-results.json             # authoritative completed operation result
  uart/raw.bin, chunks.jsonl, session.json    # console, reset, flash and tests
  diagnostics.jsonl             # diagnostic queries
  audio/REC###.WAV              # completed WAV inspection only
  wav-transfer.jsonl            # transfer start/end identity and checksums
  operation.cfg, scripts/, openocd-command.json, openocd.log   # controls
  build-info.json, firmware/CM7.elf, firmware/CM4.elf          # flash
```

`metadata.json` alone is not a completed run. Missing `test-results.json`, a
temporary result file, or a supervisor timeout means the run is incomplete. A killed
capture may also lack session metadata or contain an unindexed raw tail. The run quota
is reserved before control; images and the full tool-log allowance must fit before
tool launch. No-profile discovery returns its evidence only in the CLI result.
