# Speaker monitoring on the demo image

Date: 2026-10-09 (local, UTC-6; Spooky Bench and log timestamps are UTC, 18:50 to
19:20)  
Beads issue: `full_spooky_proto-p04.25` (demo track; decision 0029 item 5); bug found:
`full_spooky_proto-p04.26`

**This is demo-image evidence.** It covers the opt-in `Demo` preset on
`demo/halloween-2026` only. The speaker monitor itself was first tested on a
`main`-based image in the
[speaker monitor experiment](2026-10-09-speaker-monitor-experiment.md).

The bench was the one from that experiment. The PAM8302 amplifier and speaker were on
the line-out header J5, powered from `VSYS_RAW`. The operator confirmed the board was
powered up battery first, connected and free, with an SD card in and headphones at
hand. Spooky Bench 0.7.0 flashed the image, captured UART7 for 900 s and then 300 s,
and transferred the WAVs. CLI queries went to the target CDC (COM3) through
`build/console_p046.py` (logged in `build/console-p0425-20261009a.jsonl`) and then
`build/p0425-cli.py` (logged in `build/p0425-cli.log`). The operator did the
listening checks.

**Result: pass for monitoring, with one existing fault found.** The speaker plays by
default, headphones take over and hand back, the pot works on both paths, PTT and the
Instrument voice play on the speaker, and two sessions with headphone and pot-mute
changes recorded cleanly. Separately, band switches made with rolling capture running
leave the microphone queue ahead until it overruns. That gap predates the speaker; it
is tracked as `p04.26`.

## Image

| Build ID | Source | CM7 SHA-256 | CM4 SHA-256 |
| --- | --- | --- | --- |
| `p0425-demo-20261009a` | `c1552be` (`claude/p04.25-demo-speaker`, clean) | `0bb2bbdd56941672168186617b1ea7e3d47510b6197917b0439c517a9cb5a570` | `7f23d9c8b1f86ccb4aef393f3bd27a6a074bfa07a6cd602a556f153fc44c3f7f` |

GNU Tools for STM32 14.3.1 (14.3.rel1.20251027-0700). `c1552be` merges `main`
(PR #116) into `demo/halloween-2026` and sets `SPOOKY_SPEAKER_MONITOR=ON` in the
`Demo` preset. The CM4 image is unchanged from p04.24. Demo, Debug and Release build,
and only the Demo CM7 image contains the speaker code. Native host tests pass 46/46
(MSVC), and the Spooky Bench Python tests pass. Host tests are not hardware evidence.

## Walkthrough

At boot the log showed `speaker experiment: line out on; monitor on speaker`, with
the pot at its stop (`volume ADC=1 MUTED`) and rolling capture running. `DIAG STATUS`
was clean, `ROLL` had `QUEUES=1,1/8` and `MONITOR` had no presses.

| Check | Operator report | Log |
| --- | --- | --- |
| Speaker by default, pot up and to the bottom | as expected; silent at the bottom | `MUTED` at ADC 976 |
| Band switches from the band menu (FM to AM, AM to SW, SW to FM) | as expected; the buffer-fault notice appeared after the third | three switches; see the fault below |
| C-010 chord to Instrument, clip played on the speaker, back to Field | as expected | save `C023` in 1482 ms; clip loaded, 3000 ms at 24 kHz, in 5694 ms |
| Session 1 with headphones in and out | as expected | `REC147.WAV`, one insertion and one removal logged inside the session |
| Headphones in and out outside a session | as expected | one insertion and one removal |
| Button 1 PTT, held four times | radio faded out of the speaker and back, no pops or clicks | `MONITOR`: `PRESSES=4 RELEASES=4 UNSTAMPED=0`, last hold 122,523 frames (2.55 s) |
| Session 2 with the pot taken to mute twice | as expected | `REC148.WAV`, `MUTED` at ADC 1023 and 1021 inside the session |

Each headphone insertion and removal logged exactly one path change.

## Recordings

Both sessions were started and stopped from the Button 0 prompt.

| File | Frames | Audio | Elapsed | Queues (radio, PDM) | Longest write |
| --- | ---: | ---: | ---: | --- | ---: |
| `REC147.WAV` | 1,355,776 | 28.245 s | 28,304 ms | 0/8 throughout, at most 1/8 | 24 ms |
| `REC148.WAV` | 1,359,872 | 28.330 s | 28,389 ms | at most 1/8 | 24 ms |

Both ended `RECORD PASS` with margin `OK`.

`REC147.WAV` was inspected with `spookybench wav inspect` (run
`2026-10-09T190649.330082_0000-4d681759`, saved to
`build/wav-REC147-p0425-20261009b.json`):

- the CRC transfer passed;
- 3 channels, 48 kHz, 16-bit, 28.245 s;
- radio left and right RMS 386, peak about 4,460, correlation 0.99994, no clipping;
- microphone RMS 70, peak 4,929, no clipping;
- SHA-256 `7d0da25f3975f3722e5b73271f4b9ac355856f96073e26b6ff71d18d2c6b4908`.

`REC148.WAV` was inspected the same way (run
`2026-10-09T191714.907613_0000-cc892d42`, saved to
`build/wav-REC148-p0425-20261009a.json`):

- the CRC transfer passed;
- 3 channels, 48 kHz, 16-bit, 28.331 s;
- radio left and right RMS 387, peak about 3,966, correlation 0.99996, no clipping;
- microphone RMS 63, peak 834, no clipping;
- SHA-256 `77ef1f982fd5ed4c55d95a180237a8ca515a708ed23b4b9230cb1caaedb94c3c`.

The first transfer attempt failed with `ERR WAV mount failed result=16` while
rolling capture held the card. `ROLL OFF` freed the card, and `ROLL ON` restarted
rolling capture after the transfer.

## Fault: band switches during rolling capture

After the third band switch (SW to FM, 95,390 ms), the log showed `[roll] FAULT: PDM
queue overrun; window discarded, retrying later`. `DIAG LAST` recorded
`EVENT=PDM_OVERRUN A=8 B=0` at 95,396 ms: the microphone queue was full and the radio
queue empty. Rolling capture restarted about 6 s later.

A band switch closes the audio-path stream gate, and while it is closed the radio
half-buffers do not reach the recorder. Rolling capture pairs radio and microphone
blocks by count, so each switch leaves the microphone queue permanently ahead. Two
CLI band switches showed it directly: `ROLL QUEUES` went from `1,1/8` to `1,5/8` after
`BAND AM` and to `1,6/8` after `BAND FM`. Accepted decision 0004 requires the gate to
pass silence instead, so both tracks keep the same block count. The gap is the known
deviation tracked by `full_spooky_proto-54w.12`.

The speaker monitor does not cause it: its codec writes run before the gate closes
and after it reopens. Sessions are unaffected because band changes are refused while
recording. Both recordings above passed.

`DIAG STATUS` at the end: `RADIO_OVR=0 PDM_OVR=1 SD_ERR=0 AUDIO_ERR=0 HAS_FAULT=1`,
where the single PDM overrun is this fault. `LOOP_MAX_MS=338` comes from the
synchronous band switches.

## Not exercised

- Battery runtime with the speaker in use; `p04.9`'s rehearsals measure it.
- Speaker loudness and distortion at the top of the pot on this image; the battery
  was near full.

## Artifacts

Spooky Bench result files in `build/`, each naming its run directory under
`%LOCALAPPDATA%\SpookyBench\runs\`:

- `flash-p0425-demo-20261009a.json`
- `console-p0425-demo-20261009a.json` and `console-p0425-demo-20261009b.json`
- `wav-REC147-p0425-20261009b.json` and `wav-REC148-p0425-20261009a.json`
