# WAV alignment drift: REC015

Date: 2026-09-26  
Beads issue: `full_spooky_proto-hpq.1`

REC015 was the two-minute follow-up to the successful REC014 acoustic-loopback
preflight. The earbud placement, level, and source setup were retained. Two initial
inspection attempts stopped before transfer because PuTTY still held COM3. After the
user closed PuTTY, the complete file transferred and passed inspection.

## Inspection

- Result: pass; target healthy and running after transfer.
- Artifact run: `2026-09-26T155304.689959_0000-3f0d9e3e`.
- File: 34,578,476 bytes; CRC32 `471c25ab`; SHA-256
  `0553ed66a111754fa44bd6118cc36063a4a4da089b487f8e2fe095c3a8857c05`.
- Audio: 48 kHz, signed 16-bit PCM, three channels, 5,763,072 frames,
  120.064 seconds.
- Radio RMS: 748.203 left and 748.223 right; no clipped samples.
- Microphone RMS: 1,809.285; peak 23,085; no clipped samples.
- Inspection source SHA-256:
  `7becc4abd41f75427986fdc17fc4e41cfd8589d6220e8dd16c4df6b41e00cbad`.

## Alignment

The default 2-second window, 5-second hop, and plus-or-minus 50 ms search produced:

- Result: pass; 24 of 24 windows detected.
- Median lag: 173 frames / 3.6042 ms.
- Dominant positive phase: 20 of 24 windows (83.33%).
- Dominant-phase lag: 172–173 frames; stable within one frame.
- Four opposite-phase peaks were retained in raw output and excluded from drift:
  three at 177 frames and one at 179 frames.
- Correlation magnitude: 0.5109–0.7904 across all windows.
- Drift reliable: true.
- Drift: +1 frame over 115 seconds, or +0.181 ppm.

The endpoint change is at the analyzer's one-frame resolution. The result establishes
that the current firmware has no observed radio-to-microphone divergence beyond one
sample over this two-minute recording. It does not by itself set the product's final
alignment or drift tolerance.

REC013 was reanalyzed as a negative regression. Its dominant phase covered only 8 of
12 windows (66.67%), so it remained `drift_reliable=false` with
`drift_reason=phase_ambiguous`.

## Tool provenance and validation

The final offline result used working-tree code with these SHA-256 hashes:

| File | SHA-256 |
|---|---|
| `host/spookybench/wav_align.py` | `0a7dbc4bdcea68875741709ef0e11138ddfe22cc4a87be3c69e18ef9bb6a6d9d` |
| `host/spookybench/cli.py` | `51a644a3466d2c120c0eb1e190761f0327bedc087b82788f17084dcace972841` |
| `host/spookybench/wav_inspect.py` | `d5222738be6103aa81fb269be211c8da0d78a9f0b24815895c5f17016cb75e98` |
| `host/tests/test_wav_align.py` | `20b8d5c040d186a521332547942680412307692888c772f3f8afdfb2aebca6bb` |

The focused alignment suite passed 11 tests. The complete offline Spooky Bench suite
passed 62 tests in 20.503 seconds. Python compilation and `git diff --check` also passed;
the latter printed only an unrelated line-ending warning for `.vscode/extensions.json`.
