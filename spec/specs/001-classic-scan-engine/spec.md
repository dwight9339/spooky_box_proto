# Feature Specification: Classic Scan Engine

**Feature Branch**: none (spec directory `001-classic-scan-engine`)

**Created**: 2026-10-02

**Status**: Ratified 2026-10-02

**Input**: User description: "We're about to start work on the Classic scan engine so I think
it's a good time to develop a formal spec doc for it." Scoped in discussion on 2026-10-02:
band-edge behavior is an adjustable parameter (wrap by default, bounce, or stop at the edge);
band and frequency are shared by every Field engine and by Manual, which becomes an overlay
for hand-tuning rather than an engine; the device boots scanning on FM; remembering the
startup band waits for persistent settings.

**Beads**: `full_spooky_proto-54w.7` (implementation), epic `full_spooky_proto-54w` (M3)

## Clarifications

### Session 2026-10-02

- Q: How does stop mode tell a completed sweep from the user's own pause? → A: A completed
  sweep ends exactly on the band edge, and that position is the marker. Resuming there starts
  a new sweep; a pause anywhere else resumes in place. No stored flag.
- Q: Does Classic keep scanning while the engine menu, band menu, session prompt or utility
  root is open? → A: Yes.
- Q: Is the jump distance capped? → A: Yes, at half the current band's channel count.
- Q: Are band and frequency engine-specific or shared? → A: Shared by every Field engine.
  Only engine-specific parameters are kept per engine across a switch away and back.
- Q: When the user leaves Manual and returns to Classic, does Classic continue from the
  frequency tuned in Manual or retune to its previous frequency? → A: Continue from the
  tuned frequency and band, keeping Classic's run or pause state and parameters. Manual is
  how the user repositions a scan. This replaces the earlier answer that returning restores
  Classic's previous position. The user also directs that Manual become a Field overlay over
  the active engine rather than an engine; that reclassification is settled outside this
  spec (Dependencies).
- Q: Does turning Encoder 3 set the activity hold time or the activity threshold? → A: The
  hold time (how long Classic lingers on activity). The threshold is internal.
- Q: Does Classic's jump distance carry over between bands, or does each band remember its
  own? → A: Each band remembers its own, from a per-band default, while the device stays
  powered.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Scan a band with a predictable rhythm (Priority: P1)

The user powers on Spooky Box and hears it already scanning FM. Classic steps through the
band at a steady rhythm. The user speeds it up or slows it down, makes the jumps longer or
shorter, reverses the direction, and pauses on a frequency that caught their ear, then
resumes. Every change is immediately audible and shown on the display, and the user can
predict where the next few jumps will land.

**Why this priority**: Classic is the default Field engine and the device's first
impression. Without it there is no scanning instrument and no M3 slice.

**Independent Test**: With only this story built, power on the device, then exercise run and
pause, jump rate, jump distance and direction on each band. Host tests check every landing
and timing against the parameters; a person judges the rhythm and legibility on the bench.

**Acceptance Scenarios**:

1. **Given** the device is powered off, **When** it is powered on, **Then** it enters Field
   Mode with Classic active on FM, scanning upward with no user input.
2. **Given** Classic is running, **When** the user raises the jump rate, **Then** jumps
   come more often at an even interval, and the display shows the new rate.
3. **Given** Classic is running with a distance of 1 channel, **When** the user turns the
   distance up, **Then** each jump skips the chosen number of channels and the display
   shows the count and its kHz equivalent.
4. **Given** Classic is running upward, **When** the user clicks the direction control,
   **Then** the next jump goes downward and the display shows the new direction.
5. **Given** Classic is running, **When** the user clicks run or pause, **Then** the
   receiver stays on the current frequency, the display shows paused, and rate, distance
   and direction can still be changed. **When** the user clicks again, **Then** scanning
   resumes from that frequency with the current parameters.

---

### User Story 2 - Choose what happens at the band edge (Priority: P1)

The user chooses how Classic behaves when a jump would leave the band: **wrap** continues
from the other end, **bounce** turns around and scans back, and **stop** ends the sweep on
the band edge and pauses there. Resuming from that edge begins a new sweep from the opposite
end. A pause the user makes anywhere else resumes where it left off.

**Why this priority**: Every sweep reaches an edge, so edge behavior is part of the core
motion, not an extra. Without a defined edge behavior, Classic's motion is incomplete.

**Independent Test**: Host tests drive sweeps across every band in each edge mode with
several distances and check every landing, every direction change and every stop. On the
bench, a person watches one sweep per mode.

**Acceptance Scenarios**:

1. **Given** edge behavior is wrap and a jump would pass the upper edge, **When** the jump
   is made, **Then** Classic lands near the lower edge, offset by the remainder of the jump,
   so the spacing between landings stays the same across the seam.
2. **Given** edge behavior is bounce and a jump would pass the upper edge, **When** the jump
   is made, **Then** Classic reflects off the edge by the remainder of the jump, the
   direction changes to down, and the display shows the new direction.
3. **Given** edge behavior is stop and a jump would pass the upper edge, **When** the jump
   is made, **Then** Classic lands exactly on the upper edge, pauses, and the display shows
   that the sweep is complete.
4. **Given** a sweep stopped at the upper edge with direction up, **When** the user
   resumes, **Then** Classic starts a new upward sweep from the lower edge.
5. **Given** a sweep stopped at the upper edge, **When** the user reverses the direction
   and resumes, **Then** Classic sweeps downward from the upper edge.
6. **Given** the user paused manually in the middle of the band, **When** they resume,
   **Then** Classic continues from the current frequency, in any edge mode.

---

### User Story 3 - Reposition the scan by hand and keep Classic's settings (Priority: P1)

Classic finds something, and the user enters Manual to tune by hand around it, perhaps
moving to another part of the band or another band. When the user leaves Manual, Classic
carries on scanning from wherever the user tuned to, with the same parameters and the same
running or paused state. Opening the band or engine menu or visiting the utility root
leaves Classic unchanged.

**Why this priority**: The M3 exit requires that navigation never resets the active engine.
Manual is how the user moves a scan to a new place, so Classic must pick up from there
without losing its settings.

**Independent Test**: Host tests run every navigation round trip available in the first
Field slice and check that Classic's parameters and run state are unchanged and that it
continues from the current band and frequency.

**Acceptance Scenarios**:

1. **Given** Classic is running at a known frequency, **When** the user enters Manual,
   **Then** Manual starts at that frequency and Classic stops moving.
2. **Given** the user entered Manual from Classic and tuned elsewhere in the same band,
   **When** they leave Manual, **Then** Classic continues from the tuned frequency in the
   run or pause state it had, with every parameter unchanged.
3. **Given** the user entered Manual from Classic and changed the band, **When** they leave
   Manual, **Then** Classic continues on the new band from the current frequency, with its
   run state, its parameters and that band's remembered jump distance.
4. **Given** Classic is running, **When** the user opens and closes the engine menu, band
   menu or utility root without a change, **Then** Classic keeps scanning throughout and is
   unchanged afterwards.
5. **Given** Classic is running on FM, **When** the user selects AM in the band menu,
   **Then** Classic continues on AM from AM's last frequency, in the same run state, with
   AM's remembered jump distance.

---

### User Story 4 - Scan while recording (Priority: P2)

The user starts a session while Classic is scanning. Classic keeps scanning exactly as it
did before the session began, and every control keeps working, so Spooky Box behaves as a
scanning radio that records.

**Why this priority**: Product intent and decision 0003 require that recording never
changes how Field Mode moves. It is P2 because it depends on the in-band tuning
qualification (`full_spooky_proto-54w.6`), which lifts today's recording guard.

**Independent Test**: A bench run records a session while Classic scans at its fastest
supported rate on each band, then inspects the recording and the event record.

**Acceptance Scenarios**:

1. **Given** Classic is running, **When** a session starts, **Then** scanning continues at
   the same rate, distance and direction, without a pause or a reset.
2. **Given** a session is active, **When** the user changes any Classic parameter or pauses
   and resumes, **Then** the change takes effect exactly as outside a session.
3. **Given** retuning is not permitted during a session (the recording guard is still in
   place), **When** Classic is running and a session starts, **Then** Classic shows that it
   is not scanning and why, instead of appearing to scan.

---

### User Story 5 - Hold on activity (Priority: P2)

The user sets a hold time. When Classic lands on a frequency with measurable radio activity,
it stays there while the activity lasts, up to the hold time, then moves on. With the hold
time at zero, Classic keeps its strict rhythm.

**Why this priority**: The hold makes Classic more musical and useful for listening without
turning it into Seek: it has no memory and never leaves its linear path. It is P2 because
the fixed-rhythm scan is complete without it, and it depends on a trustworthy radio
activity measure.

**Independent Test**: Host tests feed activity sequences to the hold logic. A bench sweep
over a band with known strong stations and empty channels checks where Classic holds.

**Acceptance Scenarios**:

1. **Given** the hold time is above zero, **When** Classic lands on a channel whose measured
   activity exceeds the threshold, **Then** it stays there and the display shows that it is
   holding.
2. **Given** Classic is holding, **When** the activity ends or the hold time runs out,
   **Then** Classic makes its next jump and continues at its rhythm.
3. **Given** activity continues past the hold time, **When** the hold time runs out,
   **Then** Classic moves on and does not hold again on that landing.
4. **Given** the hold time is zero, **When** Classic lands on an active channel, **Then** it
   does not hold.
5. **Given** Classic is holding, **When** the user pauses and later resumes, **Then**
   Classic makes its next jump on resume.

---

### Edge Cases

- **Distance larger than the band allows**: the distance is capped for each band (FR-008),
  so a jump never spans more than half the band and wrapped motion never looks like
  backward motion. If a band change makes the remembered distance exceed the new band's
  cap, the cap applies.
- **Resuming at an edge after a manual pause**: if the user paused manually exactly on an
  edge with the direction pointing out of the band, resuming behaves like resuming a
  completed sweep (FR-016). Nothing is left to scan in that direction, so starting a fresh
  sweep is the only meaningful continuation.
- **Edge behavior changed while paused at a completed sweep**: switching to wrap or bounce
  and resuming applies the new mode to the next jump.
- **A jump that is still in progress when the next is due**: Classic waits for it rather
  than queuing or skipping landings (FR-006).
- **Radio fault or receiver unavailable**: Classic stops advancing and shows that it is not
  scanning and why. When the receiver becomes available again, Classic resumes in its run
  state from its current frequency.
- **Band change requested during a session while band changes are guarded**: the band menu
  is rejected as decision 0009 item 21 specifies, and Classic is unaffected.
- **Activity measurement missing or stale**: no hold is started. A hold in progress ends
  when the measurement becomes unavailable.
- **Input loss and reconciliation**: a synthesized release never runs, pauses, reverses or
  changes Classic (decision 0009 item 6).

## Requirements *(mandatory)*

### Functional Requirements

**Startup and territory**

- **FR-001**: At startup the device MUST enter Field Mode with Classic as the active engine,
  on FM at the band's default frequency, running, with direction up, edge behavior wrap,
  each band's documented default jump distance, activity hold off and the documented default
  jump rate. No user action is required.
- **FR-002**: Classic's territory MUST be the whole of the current band. Every frequency
  Classic lands on MUST be a channel of that band, on the band's channel spacing and between
  its edges inclusive.
- **FR-003**: Selecting a band in the band menu MUST make that band Classic's territory.
  Classic MUST continue from that band's last frequency, in its current run state, with that
  band's remembered jump distance.

**Motion and parameters**

- **FR-004**: While running, Classic MUST make one jump per jump period, of the jump distance
  in the scan direction. The jump period is one minute divided by the jump rate.
- **FR-005**: The jump rate MUST be set in jumps per minute with an Encoder 0 turn (C-104).
  Its limits MUST be set so that every jump completes before the next is due, from measured
  receiver tune times on each band. The limits and the default MUST stay configurable until
  bench trials settle them.
- **FR-006**: Classic MUST NOT queue jumps, skip landings or let jump timing drift
  cumulatively. If a jump is still in progress when the next is due, the next jump MUST
  wait for it.
- **FR-007**: The jump distance MUST be set in channels with an Encoder 1 turn (C-106). Each
  detent MUST move through an accelerating sequence (for example 1, 2, 3, 5, 10, 20, 50,
  100 and so on). The display MUST show the channel count and its equivalent in kHz.
- **FR-008**: The jump distance MUST NOT exceed half the current band's channel count.
- **FR-009**: Each band MUST remember its own jump distance while the device stays powered,
  starting from a documented default for that band. Changing band MUST switch to the new
  band's remembered distance and MUST NOT change the distance of any other band.
- **FR-010**: An Encoder 0 click MUST toggle between running and paused (C-103). While
  paused, the receiver MUST stay on the current frequency, and rate, distance, direction,
  edge behavior and hold time MUST remain adjustable.
- **FR-011**: An Encoder 1 click MUST toggle the scan direction between up and down (C-105).
  The change MUST apply to the next jump.
- **FR-012**: A parameter change MUST take effect no later than the next jump.

**Band edges**

- **FR-013**: The edge behavior MUST be one of wrap, bounce or stop, chosen with an Encoder 2
  turn. Wrap is the default.
- **FR-014**: In wrap, a jump that would pass an edge MUST continue from the opposite edge
  by the remainder of the jump, so landings stay evenly spaced across the seam.
- **FR-015**: In bounce, a jump that would pass an edge MUST reflect off that edge by the
  remainder of the jump, and the direction parameter MUST change to match the new motion.
- **FR-016**: In stop, a jump that would pass an edge MUST land exactly on the edge, and
  Classic MUST pause there with the run state "sweep complete". When Classic resumes on an
  edge with the direction pointing out of the band, it MUST land on the opposite edge and
  start a new sweep. In every other case, resuming MUST continue from the current frequency.

**Activity hold**

- **FR-017**: The activity hold time MUST be set with an Encoder 3 turn, from zero (off) to
  a configurable maximum. The activity threshold MUST NOT be a user control; it stays
  internal and configurable until bench trials set it.
- **FR-018**: When the hold time is above zero and the measured radio activity on the
  current landing exceeds the activity threshold, Classic MUST stay on that landing while
  the activity continues, for no longer than the hold time, and then make its next jump.
- **FR-019**: Classic MUST hold at most once per landing, and only on a valid, current
  activity measurement. Receiver output during a tune MUST NOT count as activity.
- **FR-020**: Holding MUST be a visible run state, distinct from paused. Pausing during a
  hold MUST pause Classic, and resuming MUST make the next jump.

**Context and navigation**

- **FR-021**: Opening and closing the engine menu, the band menu, the session prompt or the
  global utility root MUST NOT reset or stop Classic. Classic MUST keep scanning while they
  are open.
- **FR-022**: Band and frequency MUST be shared by Classic, Manual and every other Field
  engine. Classic MUST NOT keep a frequency of its own. Whatever band and frequency are
  current when Classic becomes active again are where it continues.
- **FR-023**: Entering Manual MUST stop Classic's motion and start hand-tuning at the current
  frequency. Leaving Manual MUST return to Classic with its run state and every parameter
  unchanged, continuing from the band and frequency the user left Manual on.
- **FR-024**: Switching away from Classic through the engine menu and back MUST likewise keep
  Classic's run state and parameters, for as long as the device stays powered.
- **FR-025**: The engine menu MUST offer only engines that are available. Seek and Orbit
  MUST NOT be selectable.

**Recording and honesty**

- **FR-026**: Classic's motion and controls MUST behave identically whether or not a session
  or rolling capture is active.
- **FR-027**: When Classic cannot retune (the receiver is faulted or unavailable, or retuning
  is not permitted during the current session), it MUST NOT present itself as scanning. It
  MUST show that it is not scanning and why, and resume in its run state once retuning is
  possible again.
- **FR-028**: Classic MUST publish its state as semantic facts: band, frequency, position in
  the territory (channel index and channel count), direction, jump rate, jump distance, edge
  behavior, hold time and run state (running, paused, holding, sweep complete or unable to
  scan). It MUST NOT publish display or light instructions.
- **FR-029**: Every change to Classic's run state, direction (including bounce reversals) and
  parameters MUST be published as an event with a time, so that sessions can record
  scan-engine state.
- **FR-030**: Routine jumps MUST update the displayed frequency without extra light or audio
  acknowledgement (decision 0008 item 12). Run state, direction, edge behavior, distance,
  rate and hold time changes MUST be shown on the display.

### Key Entities

- **Classic parameters**: run state, direction, jump rate, edge behavior, hold time and the
  per-band jump distances. They belong to Classic and survive navigation, Manual and engine
  switches while the device stays powered.
- **Shared tuning**: the current band and frequency, and each band's last frequency. They
  are shared by every Field engine, Manual and the band menu, not owned by Classic.
- **Territory**: the band Classic scans: its lower and upper edges, its channel spacing and
  its channel count.
- **Scan position**: the current channel as an index within the territory and as a
  frequency. Under Principle II, this is what "scan position" means.
- **Activity hold**: the hold time and an internal activity threshold, applied to the
  measured radio activity on the current landing.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: After power-on, the user hears Classic scanning FM with no input, every time.
- **SC-002**: On every band, in every edge mode, at a distance of 1 channel, one full sweep
  lands on every channel of the band exactly once (for bounce, a sweep is one pass from edge
  to edge), and no landing is ever off the band's channel spacing or outside its edges.
- **SC-003**: Over five minutes at any supported rate, the number of jumps is within 1% of
  the set rate, and no jump is skipped or queued.
- **SC-004**: Every navigation round trip available in the first Field slice returns Classic
  with unchanged parameters and run state, continuing from the current band and frequency,
  in 100% of host-test cases.
- **SC-005**: Pausing takes effect before the next scheduled jump in 100% of host-test cases.
- **SC-006**: A recorded session of at least ten minutes with Classic scanning at its
  fastest supported rate on each band shows exact frame accounting, no overrun, and the same
  scan behavior as outside a session.
- **SC-007**: In 100% of fault and unavailable cases tested, Classic shows that it is not
  scanning, and never shows running while the frequency is not changing.
- **SC-008**: On a bench sweep over known strong stations and empty channels with a hold
  time set, Classic holds on each strong station and on no silent channel, and no hold lasts
  longer than the hold time.
- **SC-009**: After a short hands-on session, the user can predict where the next few jumps
  will land and say what each Classic control does (a judgment check by a person).

## Assumptions

- **Manual is a separate spec.** Manual's own tuning and wrap behavior (C-107, C-108), how
  it is entered and left, and its reclassification as a Field overlay are out of scope here.
  This spec covers only what Classic does when the user enters and leaves Manual, which is
  the same whether Manual is an overlay or an engine.
- **Continuing from the current frequency** answers product open decision 4 for Classic
  (Clarifications). It is recorded in a decision record and a protected-document proposal,
  not settled by this spec alone.
- **Unit and ranges.** Jump distance is in channels rather than kHz, so every landing is a
  real channel and a setting means the same in every band. The half-band cap and per-band
  distance memory are confirmed (Clarifications). The accelerating sequence is an agent
  recommendation; its values and the per-band defaults belong to the decision record. The
  band sizes motivate all three: FM has 206 channels, AM 120, SW 4,141 and LW 15.
- **Wrap and bounce carry the remainder** of a jump past the edge, rather than landing on
  the edge, so the rhythm of distances is unbroken. Stop lands on the edge so that a
  completed sweep always reaches it, and the edge position itself marks the sweep as
  complete.
- **New bindings.** The Encoder 2 turn (edge behavior) and the Encoder 3 turn (activity
  hold) are not yet in the control map. Decision 0009 items 11 and 12 leave both turns
  without action and Classic on one parameter page. This spec does not make them Defined.
  They need a control-map proposal with its audit before ratification. Classic keeps one
  parameter page.
- **No persistence.** Classic parameters, shared tuning and the startup band reset at
  power-on.
  Remembering the startup band, as last used or as a user preference, waits for persistent
  settings (`full_spooky_proto-v7l.6`, product open decision 11).
- **Navigation keeps scanning** (confirmed, Clarifications). Individual utilities that need
  the radio, such as Playback, may constrain it; that is outside this spec.
- **Presentation.** The display must show the facts listed in FR-030. The design of the
  Classic display view and of a scan-position expression on the LED matrix is outside this
  spec. The matrix mapping in decision 0013 does not cover scan position.

## Dependencies

- **[Decision 0016](../../../docs/decisions/0016-classic-scan-motion.md) (Proposed):**
  Classic scanning: edge behaviors and remainder rules, the distance unit, sequence and
  cap, rate limits and default from measured tune times, the activity trigger, and
  continuing from the current frequency after Manual. The constitution requires scan
  algorithms and timing budgets to be settled in a decision record.
- **Manual as a Field overlay (user direction 2026-10-02):** needs a decision record that
  supersedes the Manual parts of decision 0009, and proposals to the control map and to
  modes and interaction (product open decisions 3 and 4). It is settled with the Manual spec.
- **Control-map proposal** (`full_spooky_proto-54w.31`): Encoder 2 and Encoder 3 turns on
  the Classic page, as C-110 and C-111.
- **In-band tuning while recording** (`full_spooky_proto-54w.6`): required for User Story 4.
  Until it lifts the recording guard on the default branch, FR-027 governs Classic during a
  session. Band changes during a session stay guarded until `full_spooky_proto-54w.12`.
- **Radio activity measure** (`full_spooky_proto-54w.8`): required for User Story 5. It must
  report no measurement while a tune is in progress.
- **Sample timeline** (`full_spooky_proto-hpq.1`): required to place FR-029 events on the
  session timeline. The session format that stores them belongs to `full_spooky_proto-hpq.3`.
- **Out of scope:** multi-band and curated territories, scanning across band edges
  (`full_spooky_proto-54w.17`), Seek and Orbit, transition noise (`full_spooky_proto-54w.16`),
  additional parameter pages, and persistent settings.
