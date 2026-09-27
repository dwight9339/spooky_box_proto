# 0003. Radio control stays available while recording

- **Status:** Accepted 2026-09-27
- **Date:** 2026-09-27
- **Supersedes:** none
- **Beads:** `full_spooky_proto-54w.14` (proposal), `full_spooky_proto-54w.6` (in-band
  tuning), `full_spooky_proto-54w.12` (band transitions)

## Context

Spooky Box must work like a spirit box while it records: recording should change the
way it moves through radio territory as little as possible, and ideally not at all. The
user stated this on 2026-09-24 in the `54w.12` notes and again on 2026-09-27.

Product intent implies the rule but never states it. The
[roadmap](../../spec/product/roadmap.md) schedules in-band tuning while recording in M3
and band transitions qualified during recording in M4. Field Mode keeps a rolling
capture running whenever it is active
([Rolling Capture Buffers](../../spec/product/modes-and-interaction.md#rolling-capture-buffers)),
so a rule that blocked radio control during capture would block Field scanning
altogether. A Field territory may span several bands, so scanning engines issue band
transitions as well as in-band tunes.

Today the firmware rejects `TUNE`, `UP`, `DOWN` and `BAND` while recording. Principle
VI requires that guard to stay until a bench test proves the guarded workload safe. Two
known obstacles stand in the way:

- The radio control service blocks the cooperative foreground loop during every tune
  and band switch, polling for completion for up to 2 s, while the recorder queues hold
  about 683 ms (`54w.6` notes).
- A band switch closes the radio stream gate, so no radio audio reaches the recorder
  during the switch. [Decision 0004](0004-radio-track-continuity-across-transitions.md)
  addresses this.

Principles involved: I (recording is a promise), II (explicit outcomes), IV (bounded
real-time behavior), VI (keep the guard until proven), VII (instrument first).

## Options

1. **Keep rejecting radio commands during sessions and capture.** Rejected. Field Mode
   could not scan while recording, and with rolling capture always on it could not scan
   at all.
2. **Allow in-band tuning during sessions and reject band transitions permanently.**
   Rejected as an end state, because territories may span several bands. It is an
   acceptable interim step while `54w.12` is open.
3. **Pause the scan engine during a session and resume it afterwards.** Rejected. It
   changes scanning because of recording, which is the opposite of the requirement.
4. **Make radio commands legal in every session and capture state, and lift each
   recording guard only after its qualification passes.** Chosen.

## Decision

Session and capture state never make a radio command illegal. Tune, tune-step,
scan-driven retune and band-transition requests behave the same in every Session state.
Radio-specific conditions, such as a transition already in progress, may still defer or
reject a command, and those rules are identical whether or not a session is active.

The existing recording guard stays in place case by case until its qualification
passes: in-band tuning under `54w.6`, band transitions under `54w.12`. Before a guard is
removed, each of these questions has an answer backed by evidence or an accepted record:

1. **Stream continuity.** The radio stream keeps producing blocks through tune and band
   changes. Today an overrun or DMA error aborts the session.
2. **Timing.** Worst-case tune and band durations are measured against the queue stall
   budget of about 683 ms, or the sequences are made non-blocking.
3. **Scan rate.** The cost of each tune is known for continuous Classic retuning.
4. **Raw-track semantics.** Mute gaps and switching transients have a defined meaning in
   the stored radio track.
5. **Event timestamps.** Tune and band events are recorded on the session sample
   timeline (`hpq.1`, `hpq.3`).
6. **Failure isolation.** A failed tune or band switch publishes a radio fault and does
   not abort the session while audio capture survives.
7. **Acknowledgement.** A band change during a recording is acknowledged consistently
   on display, lights and audio (Principle II).
8. **Bus ownership.** Bus contention during switching is bounded.

## Consequences

- The [behavior model](../design/behavior/session.md) treats "radio commands are legal
  in every session state" as the target, and lists today's rejection as a deviation.
- The Radio region is the next machine in the behavior model (`54w.15`).
- Radio control must become non-blocking, or its blocking time must be proven to fit the
  queue budget.
- Revisit this record if qualification shows that band transitions cannot be made safe
  on this hardware. The fallback is option 2 for band transitions only, recorded in a
  superseding record.

## Evidence

None yet for the decision. The
[radio regression](../evidence/2026-09-24-radio-regression.md) shows today's guard
rejecting tune and band commands during a recording. Acceptance of the lifted guards
requires the bench evidence named in `54w.6` and `54w.12`.
