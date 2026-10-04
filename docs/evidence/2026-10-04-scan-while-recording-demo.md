# Classic scanning while recording (demo image)

Date: 2026-10-04 (local, UTC-6; Spooky Bench and CLI log timestamps are UTC)  
Beads issue: `full_spooky_proto-p04.4` (demo track, decision 0011 item 13)

**This is demo-image evidence.** It qualifies Classic scanning during a recording on
the opt-in `Demo` preset on `demo/halloween-2026` only. It does not qualify in-band
tuning while recording on `main` (`full_spooky_proto-54w.6`, decision 0003). The band
guard stays in place during sessions.

The bench was the NUCLEO-H755ZI-Q with the UI board, the radio path, an SD card and the
3,700 mAh LiPo switched on. The operator confirmed it was connected and free. Spooky
Bench 0.7.0 ran on Windows, with target CDC on COM3 and Spookyprobe on COM6.

The runs were driven by the ad hoc script `build/scan_while_recording_p044.py`, which
is not part of Spooky Bench. Its raw CDC log is `build/scan-rec-p044-20261004b.jsonl`,
with a `.summary.json` beside it. The console capture is run
`2026-10-04T172315.891296_0000-fc355603`. After the runs the operator moved the card to
the host, and the WAVs were analyzed locally with Spooky Bench's own WAV analysis
(`build/wav_local_p044.py`; results in `build/wav-local-p044-20261004b.json`).

**Result: pass.** Classic scanned during four 60 s recordings: FM at 120/min, FM at
400/min, AM at 180/min and SW at 240/min (each band's maximum, decision 0018).
- Every recording finalized with its exact frame count, with no overrun and no recorder
  fault.
- Recorder queues peaked at 1/8. The event queue peaked at 2 of 32, with nothing
  rejected.
- No service broke its foreground budget.
- Tune timing was measured on the target.

On that basis the `Demo` preset lifts the in-band tune guard.

Five AM and SW tunes could not be issued. Classic recovered on its next jump, and
nothing was lost from the recordings. AM tunes mute the radio for about 113 ms each, so
scanning AM at its maximum rate silences 41% of the radio track.

## Change under test

`CM7/App/command_policy.c` allows `COMMAND_ACTION_RADIO_TUNE` while recording when
`SPOOKY_DEMO` is defined, as the existing `SPOOKY_RADIO_TUNE_QUALIFICATION` build does.
`COMMAND_ACTION_RADIO_BAND` stays rejected. Classic issues its jumps only when that
policy allows in-band tuning. It therefore scans during a session instead of publishing
`UNABLE REASON=SESSION`. The `command_policy_demo_test` host test checks the demo rule
set.

`DEMO TUNES` reports Classic's tune timing on the target, per band and split by whether
the recorder was capturing. Each tune is timed from the Radio machine's tune start (the
command written to the receiver) to its answer, including the poll and dispatch delay.

## Images

Both images were built from `3a8c395` on `demo/halloween-2026` plus the uncommitted
`p04.4` changes. Each build's source is archived as `build/<build ID>.patch`, with its
SHA-256 in the manifest. The toolchain was GNU Tools for STM32 14.3.1+st.2, preset
`Demo`. Both flashes passed.

| Build ID | Change | CM7 SHA-256 | Source patch SHA-256 |
| --- | --- | --- | --- |
| `p044-demo-20261004b` | Guard lifted in Demo; `DEMO TUNES`. **The image under test.** | `88b309d6f6135bacf4e33397aa2a11060da006008845c53fa4cf64befa0eb26c` | `d466e21e4a21a19cb89d9971317f50aecdb2c67d55a28ea73b6cd43f1f9cd4ca` |
| `p044-demo-20261004c` | b plus `ISSUE_FAILED` in `DEMO TUNES` (counter only); left on the board | `2b2e067ed1249dbb556be86e34ec6788420b4d045a0a724a62359c7998689a79` | `7d6c7fb80c8b0dbddfcce2446b10a849d8a92e7787160a3c8069cdadbd7e9354` |

The CM4 image for both was
`fe162352187af5241efa8f2eba35cae68c6975946f2ce96f883866ed8d71a465`. An earlier candidate,
`p044-demo-20261004a`, dropped the end of the multi-line `DEMO TUNES` reply. It was
replaced before any run. Native host tests passed 33/33 (MSVC 14.29). Host tests are not
hardware evidence.

## Runs

Before each run the script set the band with `BAND`, outside the recording. It then set
Classic's rate with `CLASSIC RATE` (down to the lowest setting, then up by the default
index or to the maximum), sent `CLASSIC RUN` and cleared `DEMO TUNES`. Then it sent
`RECORD START 60`. Distance stayed 1 channel on FM and AM; on SW it was 20 channels
(100 kHz) from an earlier session. Edge behavior was wrap.

| Run | File | Rate in effect | Classic jumps | Result | Audio / elapsed | Longest write | `RECORD DIAG` queues |
| --- | --- | --- | --- | --- | --- | --- | --- |
| FM-120 | `REC140.WAV` | 120/min | 123 | `RECORD PASS` | 60.074 s / 60,122 ms | 27 ms | radio 1/8, pdm 1/8 |
| FM-max | `REC141.WAV` | 400/min | 412 | `RECORD PASS` | 60.074 s / 60,127 ms | 52 ms | 1/8, 1/8 |
| AM-max | `REC142.WAV` | 180/min (band limit) | 185 | `RECORD PASS` | 60.074 s / 60,123 ms | 25 ms | 1/8, 1/8 |
| SW-max | `REC143.WAV` | 240/min (band limit) | 247 | `RECORD PASS` | 60.074 s / 60,136 ms | 37 ms | 1/8, 1/8 |

Every file holds 2,883,584 frames and `margin=OK`. Classic stayed `RUNNING` through each
recording. Classic's jump count covers the window from just before the recording to
about 1 s after it.

`DIAG LATENCY`, recording-only maxima since boot, after all four runs:
- Loop: at most 61 ms against a 75 ms budget. It was 36 ms after FM-120 and reached 61
  ms during FM-max.
- Recorder: at most 53 ms against 70 ms.
- Every other service, including MATRIX (6 ms) and DEMO (7 ms), stayed within its 10 ms
  budget.
- No service broke its budget.

`DIAG QUEUE` after the runs: 4,990 events posted and dispatched, peak 2, nothing
rejected, no reconciles.

## Tune timing

These are `DEMO TUNES` results for the tunes that started while the recorder was
capturing.

| Run | Tunes | Failed after start | Mean | Max |
| --- | --- | --- | --- | --- |
| FM-120 | 120 | 0 | 28.9 ms | 64.0 ms |
| FM-max | 401 | 0 | 28.4 ms | 86.5 ms |
| AM-max | 179 | 0 | 172.5 ms | 202.4 ms |
| SW-max | 238 | 0 | 93.4 ms | 176.2 ms |

Outside a capture, FM tunes at 120/min averaged 18.6 to 20.0 ms over 18 to 42 tunes, on
candidate a and on build c, which have the same radio path. On build b, the few tunes
between setting each band and starting its recording averaged 181.8 ms on AM (3 tunes)
and 88.5 ms on SW (6 tunes). FM tunes therefore take about 8 to 10 ms longer during a capture, as the
foreground loop is busier and the tune completion is polled and dispatched from it.

**Tunes that could not be issued.** The console logged `[radio] tune failed at` five
times: 1,680 kHz and 680 kHz during AM-max, and 12,200, 19,300 and 4,095 kHz during
SW-max. Classic's `FAILED` count went from 0 to 2 in AM-max and from 2 to 5 in SW-max.

In each case the Radio machine answered `TUNE_FAILED` from its tune-issue step, without
publishing a tune start. The receiver did not accept the command within the bounded
device-ready wait. The next jump retuned the same frequency successfully, and the next
log line is `tuned` at that frequency. That is 5 of about 432 AM and SW tunes, all at
those bands' maximum rates. 54w.6's paced CLI tunes had none.

Build b's `DEMO TUNES` did not count these, because it times only tunes that started.
Build c adds `ISSUE_FAILED` for them. Build c was not rerun at these rates.

## WAV analysis

The four files were copied from the card and analyzed with
`spookybench.wav_inspect._analyze`. Every file is a RIFF/WAVE container, PCM, 3 channels,
48 kHz, 16-bit, 2,883,584 frames, 60.075 s. No sample was clipped.

| File | SHA-256 | CRC-32 | Radio L/R RMS | Peak | L-R correlation | Mic RMS |
| --- | --- | --- | --- | --- | --- | --- |
| `REC140.WAV` | `aa345325e9915d6050d623ea601bc28e0d408b5aaa0d11d0f0ce97d4b3fa3c05` | `bfcb30c3` | 423.9 | 4,526 | 0.99994 | 58.9 |
| `REC141.WAV` | `e7c54453e6e96d65af3a2819d0155fe2316f33c6c1120ebf48e682853fe60e75` | `fc3afcf7` | 451.3 | 5,113 | 0.99988 | 58.7 |
| `REC142.WAV` | `d28bb1c2c67ce00da6ed2fc7bc0c767ed87aa67c58963d090102a3cccdb2ca3c` | `05c933b1` | 285.2 | 5,016 | 0.99999 | 57.0 |
| `REC143.WAV` | `a9580c8230b8a6f88ee1067002624e178608bb9fb65e8070389a3622e241aa80` | `629e906d` | 3.7 | 126 | 0.96572 | 58.3 |

**Radio mute runs.** These are runs of at least 48 frames (1 ms) where both radio
channels are exactly zero. Decision 0015 stores the tune output as delivered.

| File | Runs | Median | Longest | Total |
| --- | --- | --- | --- | --- |
| `REC140.WAV` (FM 120/min) | 68 | 6.3 ms | 7.0 ms | 0.42 s |
| `REC141.WAV` (FM 400/min) | 200 | 6.4 ms | 7.0 ms | 1.27 s |
| `REC142.WAV` (AM 180/min) | 280 | 113 ms | 119 ms | 24.5 s |
| `REC143.WAV` (SW 240/min) | 5,314 | 2.5 ms | 137 ms | 41.5 s |

FM tunes leave a mute of about 6.4 ms, as 54w.6 found. Not every FM tune leaves a run
of 1 ms or more. AM tunes leave about 113 ms, also as 54w.6 found, so at 180/min 41% of
the radio track is the receiver's tune mute. That is how the receiver sounds when
scanned at that rate, not a recording fault. A slower AM rate will sound less broken up
in the video.

The SW recording carries almost no signal (RMS 3.7). Its zero runs are silence, not
tune mutes, so SW mute timing is inconclusive here, as it was for 54w.6. The demo's
random-wire antenna (decision 0011) is untested.

## Surfaces during the runs

The OLED, lights and matrix ran throughout. `DEMO STATUS` reported no OLED page write
failures. The longest page write rose to 4,965 µs during FM-max; it was 1,566 µs after
FM-120.

The matrix composed 394, 412, 399 and 377 frames in the four captures, about 6.3 to 6.9
frames/s. Of those, 23, 47, 32 and 2 were superseded before they were fully written.
The magnetometer is not read during a capture, so the EMF outline was the unknown grey.

## Not covered

- LW was not run. Its maximum is 180/min, like AM.
- Distances above 1 channel on FM and AM were not run.
- Band changes during a session stay rejected (`full_spooky_proto-54w.12`).
- No recording longer than 60 s with scanning.
- No run with a recorded session started from the Button 0 prompt while scanning.
  That path uses the same Session machine as `RECORD START`.
