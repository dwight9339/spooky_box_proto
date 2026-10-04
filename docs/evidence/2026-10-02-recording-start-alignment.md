# Recording start alignment on the radio timeline

Date: 2026-10-02 (bench runs 23:53 UTC on 2026-10-02 to about 00:40 UTC on 2026-10-03)  
Beads issue: `full_spooky_proto-hpq.1`  
Decision: [0012](../decisions/0012-common-audio-sample-timeline.md) items 2 to 4 and 9

This session ran the M1 recording regression and the decision 0012 item 9 loopback set
on the first firmware that aligns the recording start on the radio sample timeline.

## Firmware and tools

- Source: `e73ce45be380936fc9247d1b9e29dc536d908b92` (branch `claude/hpq1-timeline`),
  clean. Preset `IpcSmoke`, build ID `hpq1-timeline-ipcsmoke-20261002a`, built with the
  STM32Cube bundle arm-none-eabi-gcc 14.3.1. CM7 ELF SHA-256
  `6f2d025073a3f61e5faa73939da7addaf2b42514177afd8736ff2dec850fa2a9`, CM4 ELF SHA-256
  `88ec55028661ff3e8074318441e7eab60f33864fa9523ba2bf96880a1129df67`.
- `SPOOKY_RECORD_MIC_LATENCY_FRAMES` (*C*) was 0.
- Spooky Bench 0.7.0, source SHA-256
  `29049719da5e3712b5699598a73c79cc826d4ce87261ea1960f5114886dda37f`.
- The same-boot series used an ad hoc driver (`build/loopback_series_hpq1.py`,
  untracked) that sends `RECORD START`, waits for the outcome, then queries
  `RECORD TIMELINE` and `RECORD RESULT`. Its raw log is
  `build/hpq1-loopback-20261002.jsonl` (untracked).

## Recording regression

`test recording-regression --seconds 60`, ambient stimulus: **pass**. Artifact run
`2026-10-02T235314.889854_0000-8d25a2f4`.

- Every stage passed: boot smoke, prerequisites, recording under IPC load,
  CRC-verified WAV inspection, accounting and post-transfer health.
- The target reported build ID `hpq1-timeline-ipcsmoke-20261002a`.
- REC093: 2,883,584 frames, 60.074 s. Queue high-water 1/8 (radio and PDM), longest
  SD write 50 ms, storage margin OK. Transfer CRC32 `80aa7a10`, SHA-256
  `bc342651c486a801020eebaeea9d18a20b7829445efdbb5690ef609f0df7d73a`.
- `RECORD TIMELINE` afterwards: `epoch=1 origin=3198771 mic-start=3198771 p=307
  skip=307 c=0 tx-rx-phase=602`.

## Loopback set

The user fixed an earbud carrying the monitor output against the microphone and set
the radio to a broadband source. The setup was not moved during the session.
`RECORD TIMELINE` was queried after every recording.

| File | Boot | Length | *p* | skip | TX/RX phase | Recorder result |
|---|---|---|---:|---:|---:|---|
| REC094 | 1 | 20 s | 289 | 289 | 602 | PASS, 962,560 frames |
| REC095 | 1 | 20 s | 346 | 346 | 602 | PASS, 962,560 frames |
| REC096 | 1 | 20 s | 131 | 131 | 602 | PASS, 962,560 frames |
| REC097 | 1 | 20 s | 231 | 231 | 602 | PASS, 962,560 frames |
| REC098 | 2 | 20 s | 97 | 97 | 602 | PASS, 962,560 frames |
| REC099 | 2 | 20 s | 473 | 473 | 602 | PASS, 962,560 frames |
| REC100 | 2 | 600 s | 348 | 348 | 602 | PASS, 28,803,072 frames |

Boot 1 is the regression's boot. Boot 2 followed `spookybench reset` (system reset,
same image). No completion was pending at any start (`skip` equals `p` each time). The
TX/RX phase was 602 frames in both boots.

### File retrieval

REC095, REC096 and REC099 were retrieved with `wav inspect` (CRC-verified transfer,
pass): runs `2026-10-03T000636.375103_0000-0aa91762`,
`2026-10-03T000500.941400_0000-3f22c8e8` and `2026-10-03T000942.640105_0000-7925af88`.
After the session the user moved the SD card to a PC card reader, and REC093 to REC100
were copied from it. These copies did not pass through the CRC-verified transfer.
Instead:

- The card copies of REC093, REC095, REC096 and REC099 are byte-identical (SHA-256) to
  their CRC-verified transfers.
- Spooky Bench's offline container and signal analysis (`wav_inspect._analyze`)
  passed for every card copy, and each frame count matches the recorder's
  `RECORD RESULT` exactly.
- No channel clipped. Radio RMS was about 325 to 336 and microphone RMS about 543 to
  583 in every file.

| File | SHA-256 |
|---|---|
| REC094 | `f010aa69dbebabc5facea5e84923a2f0b6545eacb2fb3d70ea2aa20b2b64e0fd` |
| REC095 | `ce5807ce897798b6bfa4c77fb41db0fe3aa2afd01e1f80d82deffdd3a6fc56f8` |
| REC096 | `3aee5c84c2c3f335b1ead514bf5c1d7c2bff03a036e89558687d4570e2868ab6` |
| REC097 | `4e62f848f677276d67794cc35d4490138bd10276c7ba5f570e9279d86feaa7f7` |
| REC098 | `275e936820e9fa9219a8793e52b774a5803117aacad406f63800ed7a317e3198` |
| REC099 | `eaf519aa64c5c0c09e0baa57a4cca416d8c291c47881f6edf54da99e96891aa3` |
| REC100 | `3cb1437e86014cf4fdecca452b418e39ff1f6a8a5459df578757d64f7f7c9320` |

## Alignment

`wav align` with default settings (2 s windows, 5 s hop, ±50 ms search):

| File | Windows detected | Lag (frames) | Spread | Polarity | drift_reliable |
|---|---|---:|---:|---|---|
| REC094 | 4/4 | 616 | 0 | + | true |
| REC095 | 4/4 | 616 | 0 | + | true |
| REC096 | 4/4 | 616 | 0 | + | true |
| REC097 | 4/4 | **621** | 0 | **−** | true |
| REC098 | 4/4 | 615 | 0 | + | true |
| REC099 | 4/4 | 616 | 0 | + | true |
| REC100 | 120/120 | 615 | 0 | + | true, 0 frames over 600 s |

As reported by the tool, the boot 1 spread is 5 frames (616 to 621), outside decision
0012's ±2 frames.

REC097 is the exception, and the cause is phase selection, not an alignment change. The
loopback correlation oscillates: a positive peak near 616 and a negative peak of almost
equal size near 620 to 621, which is the response of the earbud, air and microphone path.
`wav align` picks the largest |r| in each file. In REC097 the positive peak fell between
two frames (+0.56 at 616, +0.55 at 617), so the negative peak (−0.57 at 621) won.

An ad hoc follow-up (`build/hpq1_peaks.py`, untracked) evaluated the tool's own
correlation function at every lag from 608 to 626, for two windows per 20-second file.
It read the positive peak in each and fitted a parabola through the three samples
around it:

| File | Boot | Positive-phase lag (frames) |
|---|---|---:|
| REC094 | 1 | 616.2 |
| REC095 | 1 | 616.2 |
| REC096 | 1 | 616.2 |
| REC097 | 1 | 616.5 |
| REC098 | 2 | 615.1 |
| REC099 | 2 | 615.7 |

Read on the positive phase, the lag spread is 0.3 frames in boot 1, 0.6 frames in boot
2 and 1.4 frames across both boots. This interpolation is not part of the qualified
tool, so it is reported as analysis, not as a tool result.

## Against decision 0012 item 9

- **Same-boot start variation** (at least four recordings, spread within 2 frames).
  Met on the positive phase: 0.3 frames over four recordings with *p* from 131 to 346.
  The tool's raw spread is 5 frames because of REC097's phase selection. Before this
  change the same measurement gave a 282-frame spread (REC009 to REC012).
- **Cross-boot residual** (within 2 frames after correcting for the TX/RX phase). The
  phase was 602 in both boots, so no correction applies. Lag minus phase is 14.2 to
  14.5 frames in boot 1 and 13.1 to 13.7 in boot 2, a spread of 1.4 frames. Two boots
  with the same phase do not show how the residual behaves when the phase differs.
- **Drift** (at most 2 frames over ten minutes): met, 0 frames over 600 s, 120 of 120
  windows on one phase.
- ***C*: not determined.** With the start now aligned, the loopback lag is the
  output-path latency (TX/RX phase plus the codec, earbud, air and microphone-filter
  delay *K*) minus *C*. The measurements give *K* − *C* ≈ 14 frames. They cannot
  separate *C* from *K*, so item 9's "compute *C* from the first two sets" cannot be
  done with an acoustic loopback through the monitor output. *C* remains 0 in the
  firmware.

## Not covered

- The absolute radio-to-microphone offset, which needs *C* or a reference that reaches
  both inputs at once.
- A boot with a different TX/RX phase. Startup produced 602 both times.
- Listening.
- Tune, band and PTT event stamps (`hpq.3`, `54w.6`, `54w.9`).
