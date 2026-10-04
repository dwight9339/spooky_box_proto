# In-band tuning while recording (RadioTuneQual)

Date: 2026-10-01  
Beads issue: `full_spooky_proto-54w.6`

This session ran in-band tuning during recordings on the NUCLEO-H755ZI-Q bench with the
opt-in `RadioTuneQual` image, which admits `TUNE`, `UP` and `DOWN` while recording and
still rejects `BAND`. The operator confirmed that the board was connected and free, with
the SD card, radio and microphone path installed. Spooky Bench 0.7.0 ran on Windows
with target CDC on COM3 and Spookyprobe on COM6. Spooky Bench timestamps are UTC.

## Image

| Preset | Manifest | Build ID | CM7 SHA-256 | CM4 SHA-256 |
| --- | --- | --- | --- | --- |
| RadioTuneQual | `build/bench-54w6-tunequal-20261001a.json` | `54w6-tunequal-20261001a` | `82c9840534ce380b10db47a67f277fa771d55bd3396c5e29184b24dc5c55eea1` | `8f2064f4110e474a4711c3c86074e65f1569444b8c2822c1203d766ce33f6330` |

Built from `413db6395580f4a47a2e76fe132030e464fdb747` with GNU Tools for STM32
14.3.1+st.2. The working tree differed from that commit only in Spooky Bench
(`host/`), which was changed in this session to accept `RadioTuneQual` manifests; no
firmware source differed. `spookybench flash` programmed and verified both cores
(run `2026-10-01T163236.352406_0000-2415b170`). `DIAG IDENTITY` then reported
`BUILD=54w6-tunequal-20261001a BOOT=4`, and the boot epoch did not change during
the session.

## Method

An ad hoc pyserial driver, not part of Spooky Bench, ran one bounded recording per
band. It logged every line with a host monotonic timestamp. Raw files, untracked, in
the local `build/` directory: `tune_while_recording_54w6.py`,
`tune-qual-54w6-20261001a.jsonl` and `tune-qual-54w6-20261001a.summary.json`.

The band was selected before `RECORD START`. After 3 s of recording, each run issued:

1. Paced `TUNE` targets striding through the band, one per 200 ms (AM: 250 ms), each
   sent after the previous reply, for 20 s.
2. Twenty paced `UP`/`DOWN` steps.
3. Back-to-back `TUNE`, each sent on the previous reply, for 15 s.
4. A flood: one `TUNE` every 5 ms without waiting, for 5 s.
5. `BAND LW` and one out-of-range `TUNE`, then `RECORD STATUS`.

After `OK RECORD PASS` it queried `RECORD LATENCY`, `DIAG LATENCY`, `DIAG STATUS`,
`LOG STATUS` and `STATUS`. Each WAV was then retrieved with `spookybench wav inspect`.

Reply times below are host round trips from writing the command to its `OK RADIO`
reply. They include USB CDC latency and up to one foreground loop pass, so they are an
upper bound on the receiver tune time.

## Recording results

| File | Band | Requested | Frames | Data bytes | Audio | Elapsed | Result |
| --- | --- | ---: | ---: | ---: | ---: | ---: | --- |
| `REC051.WAV` | FM | 90 s | 4,321,280 | 25,927,680 | 90.026 s | 90,071 ms | `OK RECORD PASS`, duration complete |
| `REC052.WAV` | AM | 60 s | 2,883,584 | 17,301,504 | 60.074 s | 60,119 ms | `OK RECORD PASS`, duration complete |
| `REC053.WAV` | SW | 60 s | 2,883,584 | 17,301,504 | 60.074 s | 60,117 ms | `OK RECORD PASS`, duration complete |

For every run, `RECORD DIAG` reported radio and PDM queue high-water 1/8. `DIAG STATUS`
reported zero radio overrun, PDM overrun, SD error and audio error, with no fault.
`LOG STATUS` reported no dropped writes or bytes and no TX loss.

| File | `RECORD LATENCY` write max | Write histogram (10 ms bins) | Convert max |
| --- | ---: | --- | ---: |
| `REC051.WAV` | 37 ms | 0,1046,8,1,0,0,0,0 | 14 ms |
| `REC052.WAV` | 25 ms | 0,699,5,0,0,0,0,0 | 14 ms |
| `REC053.WAV` | 49 ms | 0,700,3,0,1,0,0,0 | 14 ms |

`DIAG LATENCY` (recording-only, cumulative since boot) after each run reported the
following, with zero violations in every service:

| After | Loop max (budget 75 ms) | Recorder max (budget 70 ms) | Dispatch max (budget 10 ms) |
| --- | ---: | ---: | ---: |
| FM | 44 ms | 37 ms | 4 ms |
| AM | 44 ms | 37 ms | 5 ms |
| SW | 56 ms | 49 ms | 5 ms |

The unconditional `LOOP_MAX_MS` in `DIAG STATUS` rose to 150 ms after the FM run and to
326 ms after the AM run. Both include the synchronous `BAND` switches made outside the
recordings, as in the [non-blocking radio evidence](2026-10-01-nonblocking-radio.md).
A final `DIAG DUMP` held 128 events (125 `SD_WRITE`, one each of `COMMAND_REJECTED`,
`RECORD_END` and `LOOP_STALL`). The history wrapped during the session, so the
`LOOP_STALL` pass cannot be tied to a specific command.

## WAV inspection

| File | Spooky Bench run | Transfer CRC32 | SHA-256 | Radio L/R correlation | Radio/mic correlation | Clipped |
| --- | --- | --- | --- | ---: | ---: | ---: |
| `REC051.WAV` | `2026-10-01T163847.257857_0000-6c48e7c7` | `d7913ff2` | `051c9ff1deb6f51dac8c193a7cfe6bfa9f8ec18e11ddafef610abc12749c683a` | 0.99982 | -0.0011 | 0 |
| `REC052.WAV` | `2026-10-01T164527.595884_0000-e9f910c8` | `91bec0bf` | `fb0bb03480b5be89d2a5bec6b5d7e29a96406d683e31dab670573f29d43779c9` | 0.99999 | -0.0030 | 0 |
| `REC053.WAV` | `2026-10-01T164956.988056_0000-f508084e` | `af538bc6` | `bb0b0098791e5bccfa448b203ee2086b863767d16f269c54c771f369d16d73eb` | 0.99965 | 0.0005 | 0 |

Each inspection passed: CRC-verified transfer, valid 48 kHz/16-bit/3-channel
container, no constant channel, and WAV frames and data bytes equal to the recorder's
`PASS` values. Target health was `healthy` after each transfer. No listening check was
made; signal statistics are not an audio-quality verdict.

## Tune timing while recording

| Band | Paced (n, median / max ms) | `UP`/`DOWN` (n, median / max ms) | Back-to-back (n, median / max ms) |
| --- | --- | --- | --- |
| FM | 100, 60.8 / 112.3 | 20, 46.6 / 100.8 | 253, 50.5 / 102.4 |
| AM | 80, 204.6 / 250.9 | 20, 207.4 / 259.6 | 70, 202.2 / 253.6 |
| SW | 100, 124.7 / 171.6 | 20, 111.8 / 157.3 | 121, 101.5 / 202.5 |

Every paced, step and back-to-back command was answered with `OK RADIO BAND=...`. These
times are longer than the idle figures in the
[non-blocking radio evidence](2026-10-01-nonblocking-radio.md) (FM median 22.4 ms, AM
170.9 ms, SW 81.2 ms) because recording lengthens foreground loop passes. The worst
case, 259.6 ms on AM, is below the recorder's ~683 ms queue budget, and the recorder
queues never rose above 1/8.

## Guards and rejections while recording

- `BAND LW` was rejected in all three runs with `ERR RADIO tuning disabled while
  recording`. Band transitions stay guarded.
- An out-of-range `TUNE` was rejected in all three runs with the band range, for example
  `ERR FM range: 87500..108000 kHz`. The recording continued.
- No device-level tune failure occurred, so failure isolation (a failed tune
  publishing a radio fault while capture survives) was not exercised on hardware.

## Radio track during tuning

The radio channels contain runs of exact-zero samples while tuning is under way. There
were none before the first tune command, and none from one second after the last tune
command to the end of each recording (40 s, 9 s and 10 s respectively).

| File | Zero runs of 1 ms or more | Total | Median | 95th percentile | Max |
| --- | ---: | ---: | ---: | ---: | ---: |
| `REC051.WAV` FM | 299 | 1.9 s | 6.4 ms | 7.0 ms | 7.0 ms |
| `REC052.WAV` AM | 298 | 27.5 s | 114.8 ms | 117.7 ms | 166.9 ms |
| `REC053.WAV` SW | 2,554 | 36.3 s | 3.0 ms | 77.0 ms | 362.7 ms |

The receiver therefore writes digital silence into the raw radio track for a
band-dependent interval around each tune: about 6 ms on FM and about 115 ms on AM. SW
on this antenna was weak (radio RMS 47 counts), so its short zero runs cannot all be
attributed to tuning. Mute starts were matched to host send times only, which are not
on the recorder's sample timeline, so these figures do not establish event alignment.

## USB CDC input under a command flood

Of about 907 `TUNE` lines sent at 5 ms intervals per run, roughly 300 to 340 drew a
radio reply (FM: 176 `OK RADIO` and 127 `OK RADIO SUPERSEDED`). Other lines drew
`ERR unknown command`, `ERR usage: TUNE` or `ERR command too long`, and the rest drew
no reply at all. `UsbCdcReceive` drops bytes when its 256-byte receive ring is full and
always re-arms the endpoint, so lines are truncated or merged. This is a CLI input
defect, filed as `full_spooky_proto-8lw.24`. The recording and radio counters stayed
clean throughout the floods.

## Not covered

- Tune and mute events on the session sample timeline (`full_spooky_proto-hpq.1`).
- A defined meaning for the mute gaps in the stored radio track.
- Failure isolation for a device-level tune failure on hardware.
- Band transitions during recording (`full_spooky_proto-54w.12`).
- Listening.

The radio was left on FM 99.1 MHz and the `RadioTuneQual` pair left running.
