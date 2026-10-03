# 0012. Common audio sample timeline

- **Status:** Accepted 2026-09-30; items 4, 8 (start alignment) and 9 amended by [0017](0017-microphone-start-latency-uncompensated.md)
- **Date:** 2026-09-30
- **Supersedes:** none
- **Beads:** `full_spooky_proto-hpq.1` (this decision and implementation);
  consumers `full_spooky_proto-54w.9` (PTT stamps), `full_spooky_proto-hpq.3`
  (formats), `full_spooky_proto-hpq.5` (rolling capture)

## Context

Radio, microphone, EMF and semantic events must share one timeline. Decision 0010
item 2 requires it for rolling capture, decision 0004 needs band-switch gaps placed
on it, and `54w.9` needs PTT press and release on it. Today nothing defines it.

Facts from the code:

- The radio arrives on SAI2 A through a circular DMA buffer of 1,024 stereo frames:
  two halves of 512 frames. The stream runs freely from boot. `AudioPath` counts
  completed halves (`rx_half_count` and `rx_full_count`) in its callbacks.
- The microphone arrives through DFSDM, whose DMA starts at `RECORD START`. The
  recorder pairs radio block *i* with microphone block *i*, 4,096 frames each.
- Radio block 0 starts at the beginning of the first radio half that completes after
  `capture_enabled` is set. That half began up to 511 frames before the enable. The
  microphone's first sample follows the DFSDM start by a constant latency *C*. So
  the microphone starts later than the radio by *S* = *p* + *C*, where *p* (0 to 511
  frames) is how far the radio DMA had got into its current half. *p* is not
  controlled.
- SAI2, SAI1 and DFSDM all derive from PLL3P (24.576 MHz, HSI source), so no
  relative drift is expected between radio and microphone. The absolute rate follows
  HSI: 47,990 to 48,004 Hz measured.

The loopback measurements do not measure *S* alone. The stimulus reaches the
microphone through the monitor output: SAI1 TX DMA, codec, earbud and air. The
measured lag is *L* = *D*out − *S*, where *D*out is the output-path latency. *D*out
is fixed within one boot but depends on the SAI1 TX and SAI2 RX DMA phase, so it can
differ between boots.

- Same-boot recordings REC009–REC012 gave 440, 242, 524 and 286 frames. The spread of
  282 frames is *p* varying.
- REC014 and REC015 gave 549 and 172 frames.
- REC015 showed +1 frame over 115 s, at the edge of quantization.

Evidence is linked below.

Constraints:

- **Principle I:** the recorder start path is the capture baseline.
- **Principle II:** a timestamp must not claim more precision than its source has.
- **Principle IV:** timing error must come from measurement or explicit arithmetic.
- **Principle VI:** callback cadence and ISR work are preserved.

## Options

1. **Millisecond tick (`HAL_GetTick`) timestamps.** Rejected. One tick is 48 frames.
   The tick runs from a different clock than the audio, and ticks cannot place a
   sample.
2. **The recorder's `frames_written`.** Rejected. It exists only during a session
   and restarts with each one. Rolling capture and events outside sessions need the
   same timeline (decision 0010 item 2).
3. **The microphone stream as the reference.** Rejected. DFSDM starts and stops with
   each capture, while the radio runs from boot.
4. **Stamp in the SAI completion ISR.** Rejected. It adds work to the protected
   callback and still does not place events between completions.
5. **Chosen: radio stream frames, derived in the foreground.** A position is the
   radio stream frame count within one stream epoch. It is derived from the existing
   completed-half counters and the DMA down-counter, read together in the foreground.
   Sessions, captures and event stamps are offsets from positions.

## Decision

**Timeline**

1. The timeline unit is one SAI2 radio frame: nominally 48 kHz, from PLL3. A position
   is an epoch plus a 64-bit frame count since that epoch's radio stream started. A
   new epoch begins at each radio stream start, at boot or after a fault restart.
   Positions from different epochs are not comparable, and conversion reports them as
   stale.
2. A position is read in the foreground. The caller reads `AudioPath`'s completed-half
   count and the SAI2 DMA down-counter together, with the SAI2 DMA interrupt masked
   for less than one half period (10.7 ms). If the DMA is already filling the half
   after the one the count implies, the pending completion is added. The SAI callbacks
   gain no work. A completed-half count that moves backwards without a new epoch is a
   fault.
3. Halves replaced by silence while the stream gate is closed (decision 0004) count
   as frames. The gap's start and end are events on the timeline.

**Capture start**

4. Radio capture is enabled atomically with a position snapshot, immediately before
   the DFSDM DMA starts. The origin is that snapshot plus *C*. The recorder drops
   radio frames from the start of the half in progress until the origin. Radio frame
   0, microphone sample 0 and session frame 0 are then the same instant. *C* is
   measured (item 9) and set as a build constant.

**Event stamps**

5. A stamp is the position at which the producer observed the event, plus a declared
   uncertainty: the event occurred no earlier than the stamp minus the uncertainty.
   Uncertainty is clipped at a session or capture origin.
6. Each producer declares its uncertainty from measurement or explicit arithmetic, in
   the design document, when it is implemented:
   - **Exact (0 frames):** record start and stop, block boundaries, and gap edges,
     because they are defined by frames.
   - **Foreground observation:** no more than the 75 ms foreground pass budget (3,600
     frames), unless a smaller bound is measured.
   - **M4 inputs over product IPC:** stamped by M7 on receipt, with the IPC latency
     bound added.

   A producer that needs a tighter bound stamps closer to its source.

**Discontinuities**

7. A capture never spans an epoch change or a lost block. The existing recorder abort
   path (radio error, overrun) ends the capture, and its range ends at the last
   written block. This slice has no in-capture discontinuity marker.

**Tolerance**

8. The tolerances are:
   - **Start alignment:** after compensation, the radio–microphone start residual is
     within ±2 frames (±41.7 µs). This allows one frame of snapshot resolution plus
     one frame of measurement resolution for *C*.
   - **Drift:** at most 2 frames over a ten-minute recording.
   - **Event stamps:** ±1 frame of snapshot resolution plus the source's declared
     uncertainty.
   - **Seconds:** conversion uses the nominal 48 kHz and is approximate. The actual
     rate follows HSI, measured between −208 and +83 ppm. Frame offsets are
     authoritative; seconds are labels.

**Qualification**

9. At record start the firmware logs *p* and the SAI1 TX / SAI2 RX DMA phase, for
   diagnostics. The known-stimulus loopback procedure then requires:
   - at least four recordings in one boot, with lag spread within 2 frames (today it
     is 282);
   - recordings from at least two boots, whose residual after correcting for the
     logged TX/RX phase is within 2 frames;
   - a ten-minute recording with drift within 2 frames.

   *C* is computed from the first two sets. Evidence goes to `docs/evidence/`.

**Sequencing**

10. The portable arithmetic (`Common/Src/audio_timeline.c`) and its host tests land
    now. The firmware build does not link them. The M7 adapter, the recorder
    start-path change and item 9's bench runs change the capture baseline. They wait
    until M1 and M2 exit (roadmap; Principle VI), unless the user authorizes them
    sooner. They are the remaining work of `hpq.1`.

## Consequences

- `hpq.3` stores positions or session-relative frame offsets rather than wall-clock
  time. It also records each session's epoch and origin, so events and audio
  reconcile.
- `54w.9` stamps PTT through this API with the foreground bound until a tighter one
  is measured. Decision 0011 item 10 already leaves those stamps open.
- Rolling capture (decision 0010) uses the same positions. A saved capture's range
  is a pair of positions in one epoch.
- Displays that show seconds must present them as approximate when accuracy matters.
- Revisit this record if item 9 cannot reach the tolerance with a constant *C*. That
  would mean the DFSDM start latency varies, and would need a different start method.

## Evidence

- [WAV alignment REC009–REC013](../evidence/2026-09-25-wav-alignment-rec009-rec013.md)
- [Alignment preflight REC014](../evidence/2026-09-26-wav-alignment-preflight-rec014.md)
- [Alignment drift REC015](../evidence/2026-09-26-wav-alignment-drift-rec015.md)
- [Foreground latency](../evidence/2026-09-28-foreground-latency.md)
- Host tests: `tests/audio_timeline_test.c` (host arithmetic only, not hardware
  evidence).
