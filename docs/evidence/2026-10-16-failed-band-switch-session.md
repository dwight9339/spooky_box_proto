# A failed band switch keeps the session recording (54w.12.3)

Date: 2026-10-16 (local, UTC-6; Spooky Bench and log timestamps are UTC, 2026-10-17
02:19 to 02:33)  
Beads issue: `full_spooky_proto-54w.12.3`

This session checks user decision Q6 of `full_spooky_proto-54w.12`. A band switch that
fails during a session must:

- keep substituting silence, so the microphone track continues;
- publish the radio fault;
- leave the gap open, with no gap end. A reader closes the gap at session end.

Before this change, a failed switch marked the audio path stopped. The silence fill
then stopped and the microphone queue would overflow.

The session also checks decision 0030 item 11, a gap open at the session origin. This
became reachable when `54w.12.2` made band switches non-blocking, because a session
can now start while a switch is in flight.

It uses the opt-in `BandSwitchQual` preset, which allows band changes while recording
for qualification only. That preset also accepts `BAND FAIL`. This makes the next band
switch fail after its receiver power-up command is written, as if the Si4735 had
answered with ERR. The failure then takes the firmware's real failure path. Normal
builds have neither command.

The operator confirmed the board was connected and free, with the SD card in. Spooky
Bench 0.7.0 flashed the images, captured UART7, reset the board, and fetched the event
logs and WAVs. Ad hoc pyserial scripts drove the CLI. None of them is part of Spooky
Bench:

- `build/w12c-bench.py` and `build/w12c-bench-b.py`;
- logs `build/w12c-bench.log`, `build/w12c-bench-b.log` and
  `build/w12c-bench-image-b.log`.

**Result: pass, with one defect found and fixed.**

- **Failed switch:** a band switch failed 15 s into a 40 s session, and the recording
  still passed with exact accounting.
- **Gap:** the radio channels hold exact silence from the failure to the session's last
  frame, and the microphone track continues unchanged. The event log holds a gap start
  with no gap end.
- **Fault visibility:** the fault is visible in the command replies, `STATUS`, Classic's
  state, `HAS_FAULT` with `DIAG LAST`, the red LED and the UART log. A new
  `RECORD START` is refused until reset.
- **Defect:** a session started during a band switch aborted with "radio timeline
  unavailable". It is fixed on image b. Sessions started inside a switch now begin
  with a gap start at frame 0, flagged as before the origin.

## Images

GNU Tools for STM32 14.3.1 (14.3.rel1.20251027-0700), `BandSwitchQual` preset, clean
builds on `claude/54w.12.3-failed-switch-session`:

| Build ID | Source | CM7 SHA-256 | CM4 SHA-256 |
| --- | --- | --- | --- |
| `w12c-bandqual-20261016a` | `c3b71b1` | `ed655189b74b5dd28bad6bad2ed4dc0f47de2df3dae7126f727527d09b076feb` | `23d8a1ac972ecc3f9bb2d56de274d1634861145a2b35ebc198177b758beae655` |
| `w12c-bandqual-20261016b` | `4c33e87`: capture alignment allows a closed stream gate | `fcd2471e976b5b1c7d3d82e3a8e2d9de028c06ada174ef9788d965f2768a2a91` | `23d8a1ac972ecc3f9bb2d56de274d1634861145a2b35ebc198177b758beae655` |

Debug, Release and BandSwitchQual build. Native host tests pass 34/34 (MSVC). They
include:

- the induced fault and the power-down skip in `radio_control_test`;
- the open gap and the gap that spans the origin in `radio_gap_test`;
- `RADIO_FAULT` classified as a fault in `logger_diag_test`.

The Spooky Bench Python tests pass 125/125, including the reader's open-gap rule. Host
tests are not hardware evidence.

## Induced failure during a session

On both images, Classic scanned at 120/min through a 40 s session. About 15 s in, the
driver sent `BAND FAIL` and then `BAND SW`. Replies on image b (F0042):

| Command | Reply |
| --- | --- |
| `BAND FAIL` | `OK RADIO next band switch fails at POWER_UP` |
| `BAND SW` | `ERR RADIO band switch failed; reset required` |
| `STATUS` | `ERR RADIO band switch failed; reset required` |
| `TUNE 99500` | `ERR RADIO band switch failed; reset required` |
| `CLASSIC` | `OK CLASSIC STATE=UNABLE REASON=RADIO_FAULTED ...` |
| `RECORD STATUS` (10 s and 20 s later) | `OK RECORD ACTIVE ... queues=0/8,0/8 ...` and `queues=0/8,1/8`, margin `OK` |
| after the session: `RECORD START 10` | `ERR RECORD radio audio path is not running` |

The UART log shows `[radio] FAIL: SW band switch at POWER_UP`, then
`[radio] FAIL: band switch; receiver reset, output muted, radio track silent until reset`.

| Image | Session | Result | Queues (radio, PDM) | `RECORD EVENTS` | Open gap (session frames) | Radio frames nonzero in it |
| --- | --- | --- | --- | --- | --- | ---: |
| a | F0029 | `PASS`, 1,921,024 frames | 1/8, 1/8 | `written=71 lost=0` | 788,263 to 1,921,023 (1,132,761 frames, 23.6 s) | 0 |
| b | F0042 | `PASS`, 1,921,024 frames | 1/8, 1/8 | `written=69 lost=0` | 778,384 to 1,921,023 (1,142,640 frames, 23.8 s) | 0 |

- **Event logs:** both pass the reader rules with no problems. Each holds the gap start
  and no gap end before `session_end`, which the reader treats as running to session
  end. `spookybench wav retune --events` passes on both WAVs and checks every frame of
  the open gap.
- **Microphone:** the microphone channel continues through the gap at the room's level.

  | Session | Part | Nonzero microphone frames | Peak |
  | --- | --- | --- | ---: |
  | F0029 | before the gap | 669,602 of 788,263 | 18 |
  | F0029 | in the gap | 962,964 of 1,132,761 | 16 |
  | F0042 | before the gap | 668,709 of 778,384 | 16 |
  | F0042 | in the gap | 981,267 of 1,142,640 | 18 |

- **Diagnostics:** `DIAG STATUS` reported `HAS_FAULT=1` with
  `RADIO_OVR=0 PDM_OVR=0 SD_ERR=0 AUDIO_ERR=0`. `DIAG LAST` reported
  `EVENT=RADIO_FAULT A=3 B=2`, a band switch fault on the way to SW. On image b,
  `HAS_FAULT` was 0 just before the failure.
- **Latency:** `DIAG LATENCY` on image b reported the loop at most 42 ms during
  recording against its 75 ms budget, the dispatcher at most 6 ms, and no
  violations. The all-time `LOOP_MAX_MS` read 74 ms before the failure part and 81 ms
  after it. That counts outside recording too, where the next recording's preparation
  measured steps up to 75 ms.
- **WAV hashes:** F0029 `fdbd33342e59f66c407ab03e52252bd9809175ab5ee51456c346e66e7b1f6209`;
  F0042 `e5dc705e5d47735decffa73de0a261b951f62ad7047777d21ce88d698a7c19a0`.

A Spooky Bench reset after image a's run cleared the fault: `STATUS` answered
`OK RADIO BAND=FM ...` again.

## Sessions started inside a band switch

**Image a.** The driver first sent `RECORD START 10` and then a band command up to
0.85 s later (F0025 to F0028). Preparing a short recording took only about 320 ms, so
every switch began after the origin. All four passed with ordinary gaps.

After the reset, the driver sent the band command first and `RECORD START 10` 0 to
0.1 s later:

- F0030, F0031 and F0032 aborted at capture start with
  `ERR RECORD ABORT ... frames=0 ... reason=radio timeline unavailable`.
- F0033's switch finished before its origin, and it passed.

**Defect.** Capture alignment refused a closed stream gate, because gated halves once
were not delivered to the recorder. Since the 54w.12.1 silence fill, they are delivered
as silence, so the check was stale. With the blocking switch, a session could never
start mid-switch. With the non-blocking switch, it made `RECORD START` right after a
band command abort. Image b removes the check.

**Image b.** The same timing (F0034 to F0037), then the image-a timing again (F0038 to
F0041). All eight passed:

| Session | Started | Gap records | Matching `[gap]` line |
| --- | --- | --- | --- |
| F0034 | inside FM→AM | start at frame 0, flagged before origin; end at 11,930 | `start=1:758784 end=1:786431` |
| F0035 | inside AM→FM | start at frame 0, flagged; end at 2,769 | `start=1:1406464 end=1:1426943` |
| F0036 | inside FM→SW | start at frame 0, flagged; end at 7,592 | `start=1:2055680 end=1:2080767` |
| F0037 | after the switch | none | `start=1:2704896 end=1:2724863`, before the origin |
| F0038 to F0041 | before the switch | start and end after the origin | one each |

- **F0034 check:** its origin is 774,501, the gap end 786,431 minus its session frame
  11,930. The gap's first frame, 758,784, lies before the origin, so the frame-0 record
  stands for it.
- **F0034 audio:** every radio frame from 0 to 11,930 is zero. `wav retune` reports
  `no_tunes` for this WAV only because Classic was paused and the session holds no tune
  to check (WAV SHA-256
  `517389752131e7585d6704c04dc45f973b3cab4ae92c5a04aec1222cdb78a3b3`).
- **Reader rules:** every event log in this session passes them.

## Not exercised

- **A real receiver failure:** the failure was induced. A real ERR answer, or a
  receiver that stops answering, reaches the same failure path; the host tests cover
  both.
- **Listening:** no one listened to the monitor while the radio was faulted. The codec
  stays transition-muted and the monitor buffer is zeroed.
- **Sleep entry while faulted:** the power-down skip is covered by host tests only.

## Artifacts

Spooky Bench result files in `build/`, each naming its run directory under
`%LOCALAPPDATA%\SpookyBench\runs\`:

- `flash-w12c-bandqual-20261016a.json` and `-b.json`
- `console-w12c-bandqual-20261016a.json` and `-b.json`
- `reset-w12c-20261016a.json`
- `events-F0025-w12c.json` to `events-F0029-w12c.json` and `events-F0034-w12c.json`
  to `events-F0042-w12c.json`
- `wav-F0029-w12c.json`, `wav-F0034-w12c.json` and `wav-F0042-w12c.json`
- `retune-F0029-w12c.json`, `retune-F0034-w12c.json` and `retune-F0042-w12c.json`

The UART logs are `build/uart-w12c-20261016a.log` and `build/uart-w12c-20261016b.log`.
