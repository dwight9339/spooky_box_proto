# WAV alignment: REC009–REC013

Date: 2026-09-25  
Beads issue: `full_spooky_proto-hpq.1`

Five previously retrieved recorder WAVs were analyzed offline with the working-tree
`wav align` utility. The command used its default 2-second window, 5-second hop, and
50 ms maximum lag:

```powershell
host/.venv/Scripts/python.exe -B -m spookybench --json wav align --wav <path>
```

The files came from these Spooky Bench run artifacts:

| Recording | Run directory | Duration |
|---|---|---:|
| REC009.WAV | `2026-09-25T211329.286164_0000-ac57f363` | 20.053333 s |
| REC010.WAV | `2026-09-25T211533.779606_0000-add09641` | 20.053333 s |
| REC011.WAV | `2026-09-25T211737.750703_0000-4836950d` | 20.053333 s |
| REC012.WAV | `2026-09-25T211942.227343_0000-0b5d047c` | 20.053333 s |
| REC013.WAV | `2026-09-25T212459.550606_0000-4b71742c` | 60.074667 s |

## Results

All five commands returned `pass`. Positive lag means the microphone follows the radio.

| Recording | Detected windows | Median lag | Lag range | First to last | Reported drift | Polarity |
|---|---:|---:|---:|---:|---:|---:|
| REC009.WAV | 4/4 | 440 frames / 9.1667 ms | 439–440 | 440 → 440 | 0 frames / 0 ppm | positive |
| REC010.WAV | 4/4 | 242 frames / 5.0417 ms | 242–242 | 242 → 242 | 0 frames / 0 ppm | negative |
| REC011.WAV | 3/4 | 524 frames / 10.9167 ms | 524–525 | 525 → 524 | -1 frame / -1.389 ppm | negative |
| REC012.WAV | 3/4 | 286 frames / 5.9583 ms | 286–286 | 286 → 286 | 0 frames / 0 ppm | negative |
| REC013.WAV | 12/12 | 540 frames / 11.2500 ms | 519–540 | 540 → 528 | -12 frames / -4.545 ppm | mixed |

The four 20-second recordings show stable within-file lag to at most one frame, while
their median startup lag varies from 242 to 524 frames. REC013 extends the observed
startup range to 540 frames. This is evidence of recording-to-recording start-offset
variation on the current firmware.

REC013 does not show a monotonic lag trend. Its detected lag sequence is
`540, 540, 540, 540, 534, 534, 534, 540, 540, 528, 519, 528`, with polarity changes
in four windows. Therefore the utility's first-to-last `-4.545 ppm` summary is not
accepted as a clock-drift measurement. The 21-frame spread may include correlation-peak
ambiguity or stimulus/path changes and requires a better-controlled long-duration
measurement or continuity-aware analysis before setting a drift tolerance.

## Tool provenance and validation

The utility was uncommitted working-tree code with these SHA-256 hashes:

| File | SHA-256 |
|---|---|
| `host/spookybench/wav_align.py` | `fbbb87588dfd5e8aadf8c2f6ac4ed93d5c6713d00187fcea0f1c324412b1dcc9` |
| `host/spookybench/cli.py` | `51a644a3466d2c120c0eb1e190761f0327bedc087b82788f17084dcace972841` |
| `host/spookybench/wav_inspect.py` | `d5222738be6103aa81fb269be211c8da0d78a9f0b24815895c5f17016cb75e98` |
| `host/tests/test_wav_align.py` | `3a20f7573f1fe44b8b805f8af50e20fca16453a7340f15ae1b335852b64dea8e` |

The full offline Spooky Bench suite passed: 58 tests in 18.943 seconds. An initial
sandboxed test attempt failed because Windows temporary directories were not writable;
the same suite passed with normal host filesystem access.

