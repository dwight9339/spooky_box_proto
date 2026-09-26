# WAV alignment preflight: REC014

Date: 2026-09-26  
Beads issue: `full_spooky_proto-hpq.1`

REC014 was a 20-second acoustic-loopback level check using a louder, fixed earbud
stimulus. The user reported the completed filename and left the board available for
inspection. Spooky Bench retrieved the file from the target, verified its transfer CRC,
validated the recorder WAV contract, and analyzed radio-to-microphone alignment offline.

## Inspection

- Result: pass; target healthy and running after transfer.
- Artifact run: `2026-09-26T154258.908505_0000-26b08445`
- File: 5,775,404 bytes; CRC32 `c785b90f`; SHA-256
  `ffdf799dfce3363abe6f7e0d4208698422d627ede7e639907e9417b39f6492d0`.
- Audio: 48 kHz, signed 16-bit PCM, three channels, 962,560 frames,
  20.053333 seconds.
- Radio RMS: 859.695 left and 859.712 right; no clipped samples.
- Microphone RMS: 3,957.968; peak 25,417; no clipped samples.
- Spooky Bench source SHA-256:
  `7becc4abd41f75427986fdc17fc4e41cfd8589d6220e8dd16c4df6b41e00cbad`.

## Alignment

The default 2-second window, 5-second hop, and plus-or-minus 50 ms search produced:

- Result: pass; 4 of 4 windows detected.
- Lag: 549 frames / 11.4375 ms in every window; spread 0 frames.
- Correlation: 0.6266, 0.7645, 0.7285, and 0.7085.
- Peak confidence: 10.349, 2.460, 2.533, and 2.844.
- Polarity consistent: true.
- Lag monotonic and stable: true.
- Drift reliable: true; endpoint delta 0 frames / 0 ppm over this short file.

This preflight establishes adequate loopback level and an unambiguous correlation peak.
The short duration does not replace the planned long drift recording.
