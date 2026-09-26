# Spooky Bench: baseline recording regression

Requires Spooky Bench 0.7.0 or later. This single unattended command runs the recording
regression that is repeated after every M2 service extraction and at later milestone
gates. It flashes a matched `IpcSmoke` pair, checks the audio and SD prerequisites,
records under IPC load, retrieves and inspects the file the recorder reported, and
checks target health after the transfer. It composes the existing
[boot smoke](spooky-bench-boot-smoke.md), [IPC load](spooky-bench-ipc-load.md) and
[WAV inspection](spooky-bench-wav-inspection.md) runners; their individual procedures
define each stage's criteria.

## Preconditions and invocation

Confirm that the board is connected and that no other tool holds the target CDC or
probe ports. A serial terminal left open on the target CDC fails the run with
`serial_open`. Install the SD card, radio and microphone path. Build the `IpcSmoke`
preset and generate its paired manifest (see [boot smoke](spooky-bench-boot-smoke.md)).

```powershell
host/.venv/Scripts/python.exe -m spookybench --json --profile host/bench.local.json test recording-regression --manifest build/<pair>.json --seconds 60
```

`--seconds` is a whole number from 10 through 600. The default deadline is 450 s plus
seven times the duration, and `--timeout` (300..7200 s) overrides it. WAV retrieval
dominates the run time: the acknowledged transfer runs at about 65 KB/s, so a 60 s
regression takes about 8 minutes and a 600 s regression about 55 minutes. The profile's
`run_bytes` must hold the recording (288,000 bytes per second) plus 16 MiB; the command
refuses before flashing otherwise. A ten-minute run needs about 181 MiB, which fits the
default 256 MiB. Artifact storage is append-only, so each ten-minute run takes about
165 MiB of the profile's `total_bytes` quota (2 GiB by default) until an operator
removes old runs.

### Stimulus

- `--stimulus ambient` (default) gates only on nonconstant channels. Radio static
  with an undriven microphone is acceptable.
- `--stimulus loopback` also requires an acoustic loopback: set up the earbud and
  broadband source as in the [alignment procedure](spooky-bench-wav-alignment.md).
  The run then fails unless `wav align` detects at least half the windows and the
  lag stays one continuous phase (`drift_reliable=true`). A discontinuity between
  the radio and microphone tracks fails the run. Loopback runs are limited to
  120 s because the offline analyzer holds the whole recording in memory.

## Sequence and verdict

| Stage | Gate |
| --- | --- |
| `boot_smoke` | Paired flash and verify, CDC rediscovery, M7 health, M4 IPC progress, probe UART evidence |
| `prerequisites` | Recorder idle, SD present and mounted with enough free space, audio path running (`STATUS`), codec volume service ready (`VOLUME STATUS`) |
| `recording` | The IPC load criteria: PASS accounting, queue high-water below capacity, IPC progress during and after, clean DIAG/LOG, gap-free dump |
| `wav_inspect` | CRC-verified transfer of the recorded file, valid 48 kHz/16-bit/3-channel container, no constant channel |
| `accounting` | Retrieved file is the new recorded file; WAV frames and data bytes equal the recorder's PASS values |
| `alignment` | `loopback` only, as above |
| `post_transfer_health` | Clean DIAG/LOG after the transfer, IPC still progressing, recorder idle, same SD card, and free space reduced by the one new file within 1 MiB |

The first stage that does not pass sets the result and reason. The regression never
retries, never issues `SD CLEAN` or `SD STRESS`, and never deletes a recording. The
recorder creates files with `FA_CREATE_NEW`, so earlier recordings cannot be
overwritten. A run that fails after `RECORD START` reports the file left on the card
in `retained_recording`. A stop needed during the recording follows the IPC load
cleanup rules, and incomplete cleanup sets `human_required=true`.

Observations are reported without a verdict: tuning and volume state, radio
left/right correlation (`mono_like` at 0.99 or above), radio/microphone correlations
and clipped samples. Listening stays a pending human check in
`human_checks.listening`. Record the listening verdict separately from the automated
result.

## Evidence

The run directory contains the boot smoke UART, firmware staging and OpenOCD log;
`boot-diagnostics.jsonl`, `recording-diagnostics.jsonl`, `regression-diagnostics.jsonl`
and `wav-transfer.jsonl`; the retrieved WAV under `audio/`; one `stage-<name>.json`
per stage; and the authoritative `test-results.json`. Record bench power, stimulus,
the listening check and the manifest in a dated file under `docs/evidence/`.

Simulation covers the happy path, loopback alignment, and failure in each stage
(`ipc-stale`, `audio-stopped`, `sd-full`, `record-busy`, `record-overrun`,
`record-abort`, `wav-corrupt-frame`, `wav-silent`, `loopback-missing`). Simulation is
never hardware acceptance evidence.
