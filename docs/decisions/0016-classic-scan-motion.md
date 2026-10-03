# 0016. Classic scan motion, band edges and activity hold

- **Status:** Accepted 2026-10-02
- **Date:** 2026-10-02
- **Supersedes:** [0009](0009-first-slice-field-controls.md) item 11 for the Classic page
  only (the Encoder 2 and Encoder 3 turns). The rest of 0009 stands.
- **Beads:** `full_spooky_proto-54w.7` (Classic implementation),
  `full_spooky_proto-54w.31` (control-map proposal), `full_spooky_proto-54w.30` (Manual as
  a Field overlay)

## Context

The [Classic scan engine spec](../../spec/specs/001-classic-scan-engine/spec.md) (Draft,
clarified 2026-10-02) defines what Classic does. The constitution requires scan
algorithms and timing budgets to be settled in a decision record rather than in a spec or
in code. [Decision 0009](0009-first-slice-field-controls.md) leaves the ranges, step
sizes and band-edge behavior to `54w.7` and assigns no action to the Encoder 2 and
Encoder 3 turns.

The user settled the product questions on 2026-10-02 (spec Clarifications):

- Band-edge behavior is a parameter: wrap (default), bounce or stop. Bounce reverses the
  direction parameter. Stop pauses at the edge.
- Classic boots scanning FM. Remembering the startup band waits for persistent settings.
- Band and frequency are shared by every Field engine and Manual. Classic keeps only its
  own parameters. Leaving Manual continues Classic from the tuned band and frequency.
- Jump distance is capped at half the band, and each band remembers its own distance.
- Encoder 2 turns set the edge behavior. Encoder 3 turns set the activity hold time, and
  the activity threshold is internal.
- Classic keeps scanning while menus and the utility root are open.

Facts that constrain the numbers:

- Band sizes from the receiver's band table: FM 87.5–108 MHz in 100 kHz steps (206
  channels), AM 520–1710 kHz in 10 kHz steps (120), SW 2.3–23 MHz in 5 kHz steps (4,141),
  LW 153–279 kHz in 9 kHz steps (15).
- Tune round trips while recording, an upper bound on tune time
  ([evidence](../evidence/2026-10-01-tune-while-recording.md)): FM median 60.8 ms, max
  112.3 ms; AM median 204.6 ms, max 259.6 ms; SW median 124.7 ms, max 202.5 ms. LW was
  not measured.
- The receiver mutes its output during each tune: about 6 ms on FM and 115 ms on AM.
  [Decision 0015](0015-raw-radio-track-during-in-band-tunes.md) marks these
  retune intervals as "no radio measurement".
- The radio onset detector of [decision 0013](0013-matrix-emf-radio-and-status-mapping.md)
  measures change in the radio audio, never RSSI or SNR, and reports nothing while the
  radio is tuning.

Principles involved: II (scan position, run state and rate shown truthfully; activity
measured, not inferred), III (one shared tuning state; Classic commands go through the
shared command policy), IV (timings from measurement, configurable until bench trials),
VII (understandable quantities on the performance surface).

## Options

**Rate and dwell**

1. **A jump rate and a separate dwell time.** Rejected. With a fixed rate, the dwell is
   the period minus the tune time, so two controls would set one quantity.
2. **A dwell time instead of a rate.** Rejected. It is the same quantity inverted, and the
   tempo of the jumps is what the user hears and predicts.
3. **Chosen: one jump rate, plus an activity hold that extends a dwell only where radio
   activity is measured.**

**Distance unit**

4. **kHz.** Rejected. Distances that are not multiples of the band step land between
   channels, and one value means one FM channel but more than the whole LW band.
5. **A fraction of the band.** Rejected. It hides which channel is next, and on LW most
   fractions round to the same step.
6. **Chosen: channels of the current band, remembered per band.**

**Wrap and bounce at the edge**

7. **Land on the edge, then continue from the opposite edge or reverse.** Rejected. It
   breaks the spacing at every edge and visits the edges on every sweep.
8. **Chosen: carry the remainder of the jump past the edge.** Wrap continues from the
   opposite edge, and bounce reflects off the edge, so the spacing never changes.

**Stop at the edge**

9. **Stop on the last channel before the edge, and keep a flag for "sweep complete".**
   Rejected. A smaller distance would make the next jump fit, so position and direction
   could not tell a completed sweep from a manual pause without stored state.
10. **Chosen: the last jump lands on the edge itself.** The edge position, together with
    the direction, marks the sweep as complete.

**Activity trigger**

11. **RSSI or SNR.** Rejected. Radio activity means measured energy or change in the
    radio audio (Principle II; decision 0013 item 6).
12. **Absolute audio level.** Rejected. FM static is loud, and the level depends on the
    antenna and the signal, so it cannot tell a station from noise.
13. **A threshold the user sets.** Rejected by the user. A threshold control is Seek's
    defining parameter in product intent.
14. **Chosen: radio onsets from the decision 0013 detector, at an internal size.**

**Rate above a band's limit**

15. **Stop the rate control at the current band's limit.** Rejected. The user's setting
    would be lost on every change to a slower band.
16. **Chosen: keep the setting, run at the band's limit and show the limited rate.**

## Decision

**Territory and position**

1. Classic's territory is the current band. Its channels are the band's minimum frequency
   plus whole multiples of the band step, up to the maximum. Channel index `i` runs from
   0 to `N - 1`, where `N` is the channel count: FM 206, AM 120, SW 4,141, LW 15.
2. Band and frequency are shared tuning state. Classic stores no frequency. It computes
   each jump from the current frequency, taking the nearest channel if the current
   frequency is between channels.
3. Leaving Manual, or returning to Classic from the engine menu, continues from the
   current band and frequency with Classic's run state and parameters unchanged. This
   answers product open decision 4 for Classic. If a completed sweep was left at an edge
   and the user tunes away from it, Classic returns paused.

**Jump rate and timing**

4. The jump rate is in jumps per minute. Encoder 0 detents (C-104) step through 6, 10, 15,
   20, 30, 40, 60, 80, 100, 120, 150, 180, 240, 300 and 400, and stop at both ends. The
   default is 120 (one jump every 500 ms).
5. Each band has a maximum rate, chosen so that the period exceeds the worst tune round
   trip measured while recording, with margin: FM 400 (150 ms), AM 200 (300 ms), SW 240
   (250 ms). LW uses AM's receiver path and takes AM's 200 until it is measured.
6. If the set rate is above the current band's maximum, Classic runs at the maximum. The
   display shows the rate in effect and that it is limited. The setting is kept and
   applies again in a band that allows it.
7. A jump is due one period after the previous jump was due. It is issued at the first
   opportunity after it is due, and only once the previous tune has ended. If the previous
   tune ends after the next jump was due, that jump is issued at once and the schedule
   starts again from it. Classic never issues jumps in a burst to catch up, never skips a
   landing and never has more than one tune in flight. A rate change applies from the next
   due time.
8. Resuming issues the first jump at once, so the user hears the scan start. At startup,
   the first jump is due one period after the radio has tuned the startup frequency.

**Jump distance**

9. The jump distance is in channels. Encoder 1 detents (C-106) step through 1, 2, 3, 4, 5,
   7, 10, 15, 20, 30, 50, 70, 100, 150, 200, 300, 500, 700, 1000, 1500 and 2000. The
   sequence is cut at the current band's cap, `floor(N / 2)`, and the cap itself is the
   last value: FM 103, AM 60, SW 2,070, LW 7. The display shows the distance in channels
   and in kHz.
10. Each band remembers its own distance while the device stays powered. The defaults are
    FM 1 (100 kHz), AM 1 (10 kHz), SW 20 (100 kHz) and LW 1 (9 kHz). At the default rate,
    one full sweep takes about 103 s on FM, 60 s on AM, 104 s on SW and 8 s on LW.

**Band edges**

11. Encoder 2 detents step through wrap, bounce and stop, in that order, and stop at both
    ends. The default is wrap. A change applies to the next jump.
12. **Wrap:** the next index is `(i + d) mod N` going up and `(i - d) mod N` going down.
13. **Bounce:** going up, `j = i + d`. If `j > N - 1`, the landing is `2(N - 1) - j` and
    the direction parameter becomes down. Going down is the mirror image. Because
    `d <= N / 2`, one reflection is always enough. The direction change is published like
    a user's direction toggle.
14. **Stop:** a jump that would pass an edge lands exactly on that edge, and Classic
    pauses with the run state *sweep complete*.
15. **Resuming on an edge:** if Classic is on an edge and the direction points out of the
    band, resuming lands on the opposite edge (index 0 going up, `N - 1` going down) and
    starts a new sweep. This applies in every edge mode and after any pause. In every
    other case, resuming continues from the current channel.

**Activity hold**

16. Encoder 3 detents step through a hold time of 0 (off), 1, 2, 3, 5, 8, 10, 15, 20 and
    30 seconds, and stop at both ends. The default is 0.
17. Activity is the radio onset of decision 0013. During a retune interval of decision
    0015, and while the radio is not running or is faulted, there is no measurement and
    no hold can start.
18. A hold starts when, with the hold time above zero and Classic running, an onset of at
    least the trigger size (starting value: medium) fires on a landing before the next
    jump is issued. Classic publishes the run state *holding*.
19. The hold lasts while onsets of any size keep firing no more than the release time
    apart (starting value 1.5 s). It ends at the release, or when the hold time has passed
    since it started, whichever comes first. The next jump is then issued at once, and the
    schedule starts again from it.
20. At most one hold occurs per landing. Pausing during a hold ends it, and resuming
    issues the next jump at once (item 8).

**When Classic cannot scan**

21. Classic issues jumps through the shared command policy, like every other radio
    command. If the receiver is faulted or stopped, or the policy does not allow tuning in
    the current session state, Classic issues no jump. It publishes the run state *unable
    to scan* with the reason, and it does not retry each period. When tuning becomes
    possible again, it resumes in its run state, with the next jump due one period later.

**Startup**

22. At startup Classic is running on FM at the band's default frequency (99.1 MHz today),
    with direction up, edge behavior wrap, the per-band default distances, a hold time of
    0 and a rate of 120.

**Configuration and qualification**

23. The rate table, band maximum rates, distance table, per-band defaults, hold-time table,
    trigger size and release time are starting values in one configuration. They stay
    configurable until a bench trial sets them (Principle IV). The trial covers:
    - scanning at each band's maximum rate during a session of at least ten minutes, after
      `54w.6` lifts the in-band guard (spec SC-006);
    - a listening judgment of the rate and distance tables and the defaults (spec SC-009);
    - a sweep with the hold on over known stations and empty channels (spec SC-008),
      including FM static.

## Consequences

- `54w.7` implements items 1 to 22 as portable code with host tests: every landing in
  every band and edge mode, the schedule under late tunes, rate limiting, the resume
  rules, holds from onset sequences, and the unable-to-scan cases.
- The control map gains Classic-page rows for the Encoder 2 and Encoder 3 turns through
  its own proposal (`54w.31`). ContextSm adds the matching Classic events in the same
  change.
- On acceptance, decision 0009's status line notes that item 11 is superseded for the
  Classic page by this record. Accepted records may change only their status line.
- How the display shows the rate, distance, edge behavior, hold time and run states is
  presentation work under `54w.5`. This record requires only that those facts are
  published and that a limited rate is shown as limited.
- At AM's maximum rate, the receiver mutes about 115 ms of each 300 ms period. At high
  rates on AM, the user hears mostly retuning. The listening trial decides whether AM's
  maximum should be lower.
- Revisit if onsets cannot tell stations from FM static, if the tables feel wrong in the
  hand, or if Encoder 3 turns cancel Shift too often. Turning Encoder 3 while its button is
  pressed but not yet holding cancels the pending press (decision 0009 item 4), so it
  cancels a Shift that was not yet entered.

## Evidence

None yet for this decision. The tune-time figures come from the
[tune-while-recording evidence](../evidence/2026-10-01-tune-while-recording.md). The
qualification in item 23 produces the evidence.
