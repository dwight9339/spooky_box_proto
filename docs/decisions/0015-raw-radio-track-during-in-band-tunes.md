# 0015. Raw radio track during in-band tunes

- **Status:** Accepted 2026-10-02
- **Date:** 2026-10-02
- **Supersedes:** none
- **Beads:** `full_spooky_proto-54w.6` (in-band tuning while recording); consumers
  `full_spooky_proto-hpq.1` (sample timeline), `full_spooky_proto-hpq.3` (session
  event format), `full_spooky_proto-54w.8` (radio onset detector)

## Context

[Decision 0003](0003-radio-control-during-recording.md) lifts the in-band tune guard
only after eight questions are answered. Question 4, raw-track semantics, asks for a
defined meaning for mute gaps and switching transients in the stored radio track.

[Decision 0004](0004-radio-track-continuity-across-transitions.md) already settles
part of it. Receiver output during an in-band tune is stored as received, including
any switching transient, and tune events on the timeline mark where those transients
occur. It does not say what the stored interval means, how long it lasts or how
consumers treat it.

The [tune-while-recording evidence](../evidence/2026-10-01-tune-while-recording.md)
shows what the receiver delivers:

- The Si4735 writes runs of exact-zero samples into the radio channels around each
  tune: a median of 6.4 ms on FM (maximum 7.0 ms) and 114.8 ms on AM (maximum
  166.9 ms). SW on that antenna was too weak to attribute its zero runs to tuning.
- No zero runs occurred before the first tune or from one second after the last.
- The mute runs were matched to host send times only, not to the recorder's sample
  timeline, so where they start and end relative to the tune is not yet measured.
- Tunes took a median of 60.8 ms on FM and 204.6 ms on AM while recording (host round
  trip). Classic scanning at a short dwell can therefore spend much of its time
  retuning, especially on AM.

Without a defined meaning, a playback or analysis tool reading the WAV would take the
receiver's mute for a quiet radio. The radio onset detector of
[decision 0013](0013-matrix-emf-radio-and-status-mapping.md) would read the end of the
mute as a sudden rise in sound. Both would claim something about the radio signal that
was not measured.

On 2026-10-01 the user directed the shape of the answer (`54w.6` notes): store the
receiver's tune mute as delivered, bracket it with tune start and complete events on
the session timeline, and have the activity detector report no measurement while
tuning.

Principles involved: I (raw tracks preserved non-destructively), II (no fabricated
meaning; uncertainty shown honestly), IV (bounded work; timings measured, configurable
until then), V (qualification needs bench evidence).

## Options

1. **Close the stream gate for each tune and substitute silence, as for band
   switches.** Rejected. It discards real receiver output, including audio before the
   mute starts and after it ends, and conflicts with decision 0004's rule that tune
   output is stored as received.
2. **Store the output and give it no special meaning.** Rejected. Consumers would
   read the receiver's mute as a quiet radio and its end as an onset (Principle II).
3. **Detect exact-zero runs in the stream and mark them.** Rejected. A weak or silent
   station can also produce zero runs (the SW recording shows this), so detection would
   fabricate tune events. It would also add work to the audio path.
4. **Chosen: store the output as delivered and mark each tune's interval with radio
   events. Consumers treat the marked interval as receiver output that is not a
   measurement of the radio signal.**

## Decision

**Storage**

1. Receiver output during an in-band tune is stored in the raw radio track exactly as
   the receiver delivers it. The firmware does not gate, substitute or alter it.

**Events**

2. The Radio machine publishes a *tune start* event when it issues a tune to the
   receiver, with the band and target frequency. It publishes a *tune end* event when
   it observes the tune's outcome: tuned (with the frequency, RSSI and SNR reported),
   failed, or abandoned by a radio fault. A command that is superseded or rejected
   before it is issued produces neither event.
3. Both events are stamped on the session sample timeline of
   [decision 0012](0012-common-audio-sample-timeline.md):
   - The start stamp is taken immediately before the tune command is written to the
     receiver, so receiver output cannot change before it.
   - The end stamp is taken when the completion or failure is observed. Its declared
     uncertainty is the foreground observation bound of decision 0012 item 6 until a
     smaller bound is measured.

**Meaning**

4. A *retune interval* runs from a tune's start stamp to its end stamp plus a settle
   margin. Inside it, the radio track holds receiver output that is not a measurement
   of the signal at either the old or the new frequency.
5. The settle margin is configured per band. Its starting value is one radio
   half-buffer (512 frames, 10.7 ms), the granularity at which radio audio reaches the
   M7. Item 10's qualification sets the final values (Principle IV).
6. Inside a retune interval:
   - Playback plays the stored audio unchanged. A display may mark the interval.
   - Activity metrics and any analysis derived from the radio track report no
     measurement.
   - The live radio onset detector receives no measured blocks from tune start until
     the settle margin after the end, so the mute and its end cannot fire an onset.
     The detector keeps its averages through the interval (`radio_activity`, decision
     0013 item 10).
   - The monitored mix carries receiver output unchanged.
7. A retune interval is distinct from a decision 0004 gap. A gap means the receiver
   produced no output and the firmware substituted digital silence. A retune interval
   holds real receiver output. Both are "no radio measurement" to consumers, and their
   events carry different kinds and causes.
8. Retune intervals cannot overlap, because the Radio machine has at most one tune in
   flight. Back-to-back tunes during scanning produce adjoining intervals.

**Scope**

9. Rolling capture (decision 0010) carries the same events in its event sidecar, so a
   saved capture has its retune intervals marked.

**Qualification**

10. With the `hpq.1` timeline in the firmware, a bench run records repeated tunes on FM
    and AM, and on SW if an antenna gives a usable signal. It passes when every
    exact-zero run of 1 ms or more after the first tune lies inside a marked retune
    interval. The settle margin for each band is set to the largest overhang observed
    past the end stamp, rounded up to a whole half-buffer. Evidence goes to
    `docs/evidence/`.

## Consequences

- This answers decision 0003 question 4. Question 5 still needs tune events stored
  with the session (`hpq.1`, `hpq.3`). Until it is answered, a recording made with
  tuning allowed has no marked retune intervals, so the in-band guard on `main` stays
  in place. The `RadioTuneQual` and `Demo` images remain qualification and
  demonstration images, and their evidence is labelled as such.
- `hpq.3` needs tune start and tune end event kinds, carrying band, target, outcome
  and the reported frequency, RSSI and SNR. They are separate from the decision 0004
  gap events.
- The radio adapter, or whatever feeds `radio_activity`, passes "not measuring" from
  the tune start until the settle margin after the end. On the `Demo` image this
  applies to `p04.3` and `p04.4`.
- Spooky Bench WAV checks that look for silence must accept zero runs inside marked
  retune intervals.
- Revisit this record if item 10 finds mute runs that start before the start stamp.
  That would mean the receiver mutes before it receives the command, and the start
  stamp would need its own margin.

## Evidence

- [Tune while recording](../evidence/2026-10-01-tune-while-recording.md): zero-run
  durations per band and tune times while recording.
- [Non-blocking radio](../evidence/2026-10-01-nonblocking-radio.md): idle tune
  durations per band.
- Item 10's bench run is pending `hpq.1`.
