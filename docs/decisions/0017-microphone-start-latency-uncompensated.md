# 0017. Microphone start latency stays uncompensated

- **Status:** Accepted 2026-10-02
- **Date:** 2026-10-02
- **Supersedes:** [0012](0012-common-audio-sample-timeline.md) items 4 and 9 and its
  start-alignment tolerance in item 8, as far as they concern *C*. The rest of 0012
  stands.
- **Beads:** `full_spooky_proto-hpq.1`

## Context

[Decision 0012](0012-common-audio-sample-timeline.md) aligns the recording start on
the radio timeline. Item 4 sets the origin to the radio position snapshot plus *C*,
the measured latency from the microphone DMA start to the first microphone sample.
Item 9 computes *C* from the acoustic-loopback qualification, and item 8 requires the
radio–microphone start residual to be within ±2 frames after compensation.

The [2026-10-02 bench session](../evidence/2026-10-02-recording-start-alignment.md) ran
item 9 on firmware `e73ce45` with *C* = 0:

- Same boot, four recordings with *p* from 131 to 346 frames: lag spread 0.3 frames
  on the positive correlation phase.
- Across a reset: 1.4 frames over six recordings.
- Drift: 0 frames over 600 s.
- The TX/RX phase was 602 frames in both boots.

The loopback cannot measure *C*. The stimulus reaches the microphone through the
monitor output, so the measured lag is the output-path latency minus *C*. That
latency is the TX/RX phase plus a fixed codec, earbud, air and microphone-filter delay
*K*. The measurements give *K* − *C* ≈ 14 frames, one equation with two unknowns.
Separating them needs a reference that reaches the radio input and the microphone at
the same instant, and the bench has none: the radio input is RF into the Si4735.

Explicit arithmetic bounds part of *C*. The DFSDM runs a Sinc4 filter with an
oversampling ratio of 64, integrator ratio 1 and fast mode, from a 3.072 MHz PDM
clock (48 kHz × 64):

- The first conversion is available about 4 × 64 = 256 PDM clocks after the start,
  about 4 radio frames.
- The filter's group delay is about 4 × 63 / 2 = 126 PDM clocks, about 2 frames. The
  first sample therefore represents sound from about 2 frames after the start.
- The masked instructions between the snapshot and the DFSDM enable take
  microseconds, well under one frame.
- The PDM microphone's internal delay is not documented here and is not included.

At 48 kHz, 4 frames is 83 µs.

Principles involved: II (no timestamp claims more precision than its source), IV
(timing comes from measurement or explicit arithmetic), V (evidence before claims).

## Options

1. **Set *C* from the arithmetic, about 2 frames.** Rejected. It would add a
   correction that cannot be checked on this bench, and it would still leave out the
   microphone's own delay. The compensated alignment would claim a precision that was
   never measured.
2. **Build an electrical reference that reaches both inputs at once.** Deferred. Doing
   so needs new bench hardware. No current feature needs sub-millisecond absolute
   alignment between the tracks.
3. **Chosen: keep *C* = 0, qualify the alignment as repeatability, and state the
   absolute offset as unmeasured with an arithmetic estimate.**

## Decision

1. ***C* is 0.** `SPOOKY_RECORD_MIC_LATENCY_FRAMES` stays a build constant with value
   0. The recording origin is the radio frame in progress when the microphone DMA is
   enabled. Radio frame 0 of a recording is that frame.
2. **The absolute offset is unmeasured.** Microphone sample 0 represents sound from an
   unmeasured, constant interval after the origin. The arithmetic above estimates it
   at about 2 to 4 frames, plus the microphone's internal delay. Documents and tools
   state it that way, and do not present the tracks as aligned to a single frame in
   absolute terms.
3. **The start-alignment tolerance is repeatability.** It replaces 0012 item 8's
   start-alignment tolerance:
   - Recordings in one boot agree within ±2 frames.
   - Recordings in different boots agree within ±2 frames after correcting for the
     logged TX/RX phase.

   Both are measured by acoustic loopback on one correlation phase. Item 8's drift,
   event-stamp and seconds tolerances are unchanged.
4. **Qualification.** The 2026-10-02 session meets item 3 (0.3 frames in one boot, 1.4
   frames across two) and the drift tolerance (0 frames over ten minutes). It replaces
   item 9's requirement to compute *C*. The single-phase figures come from an ad hoc
   positive-phase analysis. The tool's own report differs for REC097, which it read on
   the opposite phase. `full_spooky_proto-rk8` brings that analysis into `wav align`.
5. **Event stamps are unaffected.** They are positions on the radio timeline (0012
   items 5 and 6). An analysis that compares radio content with microphone content in
   time must allow for the unmeasured constant of item 2.

## Consequences

- `hpq.1`'s start alignment is qualified as repeatable and drift-free. It is not
  qualified as an absolute radio–microphone offset.
- `RECORD TIMELINE` keeps reporting `c=0`. A later record that measures *C* changes
  the constant, and that record's evidence must show it.
- Both qualifying boots had the same TX/RX phase (602 frames), so the phase correction
  in item 3 has not been exercised. A later loopback that sees a different phase
  checks it.
- Revisit this record if a feature needs absolute radio–microphone alignment finer
  than about 0.1 ms, or if a reference reaching both inputs becomes available.

## Evidence

- [Recording start alignment, 2026-10-02](../evidence/2026-10-02-recording-start-alignment.md)
- [WAV alignment baseline, REC009–REC013](../evidence/2026-09-25-wav-alignment-rec009-rec013.md),
  for the 282-frame same-boot spread before the change.
