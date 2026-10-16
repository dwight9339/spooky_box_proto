# Spooky Bench: host-side WAV inspection

Requires Spooky Bench 0.5.0 or later. This procedure retrieves one
completed recording from the target's SD card, proves transport integrity, and
analyzes the exact saved bytes on the Windows host. Recordings are Field session
folders ([decision 0030](../decisions/0030-field-session-folder-and-event-log.md)),
so the file is `FIELD/Fnnnn/AUDIO000.WAV`. Root `REC###.WAV` files from earlier
firmware can still be fetched.

## Command and prerequisites

The target must run firmware containing binary WAV protocol v1. Keep its USB CDC
connected, leave the SD card inserted, and stop any recording or SD operation.
Run:

```powershell
host/.venv/Scripts/python.exe -m spookybench --json --profile host/bench.local.json wav inspect --file FIELD/F0004/AUDIO000.WAV --timeout 600
```

Only `FIELD/Fnnnn/AUDIOnnn.WAV` and `REC000.WAV` through `REC999.WAV` are accepted.
The run saves the file as `audio/FIELD_F0004_AUDIO000.WAV`, with `/` replaced by `_`.

A session's manifest is fetched and parsed the same way:

```powershell
host/.venv/Scripts/python.exe -m spookybench --json --profile host/bench.local.json session manifest --session F0004
```

It saves `session/FIELD_F0004_MANIFEST.TXT` and reports the parsed keys. It fails
unless the file is at most 1,024 ASCII bytes of `key=value` lines with
`format=spooky-session` and `format_version=1`. The recording regression fetches the
manifest after the WAV and requires `state=ended`, `audio_parts=1` and the recorder's
frame count. The default
whole-operation deadline is 600 seconds; `--timeout` may be set from greater than
zero through 3600 seconds. At the cooperative 5 ms service cadence, a 17.3 MB
(one-minute) file takes about 4.5 minutes.

## Bounded transfer protocol

`WAV FETCH <path>` opens a read-only FatFs session after confirming that no
recording owns the card. The target rejects a WAV smaller than 44 bytes, any other
file smaller than one byte, and any file larger than 256 MiB. It first reports the exact size, 1008-byte payload limit, and
protocol version in an ASCII start line.

The remainder is framed binary. Each frame contains magic/version, absolute file
offset, payload length, flags, and CRC32. The host validates the header, offset,
extent, and frame CRC before writing and replies `WAV ACK <next-offset>`. The
target sends no next frame until that exact ACK arrives. Ten seconds without an
ACK closes and unmounts the file, so a killed host cannot leave an unbounded
stream or permanent SD owner. `WAV ABORT` provides explicit cleanup when the host
is still able to write.

The end frame carries the whole-file CRC32. The host also calculates SHA-256,
reserves the target-declared size against the run artifact budget before
accepting data, flushes progress every MiB, and retains interrupted evidence as
`<name>.partial`. It atomically publishes `audio/<name>` only after the declared
size and final CRC match.

## Inspection verdict

The local analyzer walks RIFF chunks with boundary and padding checks. It
requires one valid PCM `fmt ` chunk and one nonempty frame-aligned `data` chunk,
with the recording contract of 48,000 Hz, signed 16-bit samples, three channels,
six-byte block alignment, and 288,000 bytes/second. RIFF size, data size, frame
count, and duration must agree.

For radio-left, radio-right, and microphone, it reports minimum, maximum, peak,
mean, RMS, exact-zero count, and clipped-sample count. It also reports pairwise
correlations. A constant channel fails with `wav_silent_channel`; clipping and
high correlation are measured rather than assigned a universal failure threshold.
Those can be legitimate for some input material but remain visible for review.

Artifacts include the WAV, `wav-transfer.jsonl`, the exact bench source archive,
device/profile identity, and authoritative `test-results.json`. Simulation covers
a good file plus missing file, frame CRC corruption, truncated transfer, constant
channels, and an invalid RIFF header. Simulated success is not hardware evidence.

## Evidence

[2026-09-24 WAV inspection results](../evidence/2026-09-24-bench-results.md#phase-3-host-side-wav-inspection).
