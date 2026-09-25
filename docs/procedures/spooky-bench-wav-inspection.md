# Spooky Bench: host-side WAV inspection

Implemented in version 0.5.0, 2026-09-24. This Phase 3 slice retrieves one
completed `REC###.WAV` from the target's SD card, proves transport integrity, and
analyzes the exact saved bytes on the Windows host.

## Command and prerequisites

The target must run firmware containing binary WAV protocol v1. Keep its USB CDC
connected, leave the SD card inserted, and stop any recording or SD operation.
Run:

```powershell
host/.venv/Scripts/python.exe -m spookybench --json --profile host/bench.local.json wav inspect --file REC004.WAV --timeout 600
```

Only names matching `REC000.WAV` through `REC999.WAV` are accepted. The default
whole-operation deadline is 600 seconds; `--timeout` may be set from greater than
zero through 3600 seconds. At the current cooperative 5 ms service cadence, the
initial 17.3 MB acceptance file took about 4.5 minutes.

## Bounded transfer protocol

`WAV FETCH REC###.WAV` opens a read-only FatFs session after confirming that no
recording owns the card. The target rejects files smaller than 44 bytes or larger
than 256 MiB. It first reports the exact size, 1008-byte payload limit, and
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
`REC###.WAV.partial`. It atomically publishes `audio/REC###.WAV` only after the
declared size and final CRC match.

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

## Initial hardware result

The first complete run inspected the earlier load-test file `REC004.WAV`. Run
`2026-09-24T190727.483624_0000-a961b406` transferred 17,301,548 bytes in 17,165
frames over 267.469 seconds. CRC32 was `eb92c866`; SHA-256 was
`fdedb2b5d885afe37865b007d84890f64ca7c4b730025358142ea78f13798098`.

The file contains 2,883,584 frames and 17,301,504 audio bytes over 60.074667
seconds. Peaks were 1643, 1640, and 672, exactly matching the recorder's original
final diagnostics. No channel clipped or stayed constant. One-second follow-up
analysis found no constant window: minimum per-window peaks were 1087, 1085, and
31 for radio-left, radio-right, and microphone. `ffprobe` independently reported
`pcm_s16le`, 48 kHz, three channels, 16 bits, the same duration, and the same file
size. Full evidence is recorded in the
[live bench results](../evidence/2026-09-24-bench-results.md#phase-3-host-side-wav-inspection).
