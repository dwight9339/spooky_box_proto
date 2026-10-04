# 0009. First-slice Field controls and gesture resolution

- **Status:** Accepted 2026-09-28; item 11 superseded for the Classic page by
  [0016](0016-classic-scan-motion.md)
- **Date:** 2026-09-28
- **Supersedes:** none
- **Beads:** `full_spooky_proto-54w.2` (this decision), `full_spooky_proto-54w.3`
  (implementation and host tests), `full_spooky_proto-54w.7` (Classic and Manual
  engines), `full_spooky_proto-54w.20` (modes-and-interaction restructure),
  `full_spooky_proto-54w.23` (protected-document proposal)

## Context

The first product slice (roadmap M3) is Field Mode with Classic and Manual. Its exit
requires no stuck Shift or PTT and no navigation-induced stop. `54w.3` implements
navigation and gestures on the InputResolution machine, which today models only the
Button 0 session hold of [decision 0005](0005-button-0-session-prompt.md). `54w.7`
implements the two engines. Before either can proceed, these questions are open:

- **D-001:** Field Shift plus Encoder 1 opens Settings (C-007, Proposed), while
  Instrument Shift plus Encoder 0 opens the global utility root (C-027, Defined).
- **D-002:** Encoder 3 hold enters Shift on engine pages but leaves utilities (C-004),
  and a short press advances the page (C-005).
- **D-005:** the scan-engine selector commits on release (C-015), and no cancel exists.
- **D-006:** Shift plus Button 0 must not switch modes before the user can add Button 1
  (C-008, C-010), and no timing has been chosen.
- **D-003:** the performance-view selector is Instrument only and is not in this slice.
- **Field page bindings:** the control map has no rows for what the encoders do on the
  Classic and Manual pages, and no band-change gesture.

The user gave direction on 2026-09-28 in two rounds of review of this record:

- The gesture rules of the first draft stand.
- The Manual quick-jump and its return move from Encoder 0 hold (C-011, C-012) to
  Shift plus an Encoder 1 press.
- In Classic, an Encoder 0 turn sets the jump rate and an Encoder 1 turn sets the jump
  distance. An Encoder 0 click runs or pauses the scan, and an Encoder 1 click toggles
  the scan direction.
- In Manual, Encoder 0 is the only tuning knob for now. An Encoder 0 click toggles
  whether tuning wraps at the band edges.
- In both engines, a long Encoder 1 press opens a band menu.
- The band menu and the scan-engine menu both open on a long press and then stay open.
  They share the menu conventions in items 15 to 17.
- Shift plus Encoder 0 opens the global utility root in Field. C-007 is retired.
- The session hold is not available inside utilities.

Some bindings are Defined in the control map but still described as open in product
prose. These are the Manual quick-jump (C-011, C-012) and the scan-engine selector
(C-013 to C-015). [Modes and interaction](../../spec/product/modes-and-interaction.md)
calls both gestures unsettled in its Manual and Temporary Navigation sections. This
record changes both bindings.

Constraints:

- **Principle I:** a slip must not stop or invalidate a session. Decision 0008 classifies
  every action while a session is active.
- **Principle II:** outcomes are acknowledged consistently. Shift lights only the
  available actions (0008 item 8). A band change is a consequential transition (0008
  item 13).
- **Principle III:** M7 resolves gestures from behavior-neutral inputs. Lost input
  releases every held control, so Shift and PTT cannot latch.
- **Principle IV:** hold thresholds and timeouts stay configurable until bench trials
  set them.
- **Principle VI:** today's guard rejects tune and band commands while recording
  (decision 0003), until `54w.6` and `54w.12` qualify them.
- **Principle VII:** the Shift layer stays small, and axes and gestures stay
  consistent across modes.

## Options

**Chord arbitration (D-006)**

1. **Fire on press, and undo if the chord completes.** Rejected. A mode switch that is
   undone a moment later is visible, and Principle II forbids an outcome that is then
   retracted.
2. **Fire on press after a chord window.** Rejected. It adds a delay to every Shift
   button action and a timing that must be tuned. A slow second finger still loses the
   chord.
3. **Resolve Shift button actions on release, and fire the chord when its second button
   goes down.** Chosen. Resolution follows event order alone, so no chord window is
   needed and both press orders behave the same.

**Utility entry (D-001)**

4. **Keep the split: Field Shift plus Encoder 1 opens Settings, and Instrument Shift
   plus Encoder 0 opens the root.** Rejected. The same intent would use different
   controls in each mode, and Field Shift plus Encoder 1 now belongs to Manual.
5. **Use Shift plus Encoder 0 for the global utility root in both modes.** Chosen by
   the user. Settings is one selection deeper, and Playback and Diagnostics are reached
   the same way from either mode.

**Engine and band menus (D-005)**

6. **A held selector: hold to open, scroll, release to commit (C-013 to C-015).**
   Rejected by the user. Release always commits, so a slip commits too. The only
   way back is to scroll to the original item, and the held button cannot also cancel.
7. **A menu opened by a short press that stays open.** Rejected by the user. A brush
   against the button would open the menu, and Encoder 1's short press now toggles
   the Classic scan direction.
8. **A menu opened by a long press that stays open, with the utility conventions:
   Encoder 0 scrolls, the Encoder 0 button selects, the Encoder 1 button closes.**
   Chosen by the user for both the engine menu and the band menu. It matches the
   menu rules the user already knows (C-001 to C-003). Changing engine or band takes a
   deliberate second press, which suits a consequential transition.

## Decision

Items marked *(user)* are the user's direction. Everything else is proposed by the
agent. Once this record is accepted, the protected-document changes it implies go
through their own proposal (`54w.23`).

**General rules**

1. **Press origin.** A press is resolved in the layer and context where it began:
   Normal, Shift, menu or prompt, in the operating mode or utility that had the
   controls. A release is interpreted with its press. A release whose press was
   swallowed or consumed is swallowed. This extends decision 0005 items 8 and 9 to
   every control.
2. **One outcome per press.** Every press produces at most one action: a click, a
   hold, a Shift action or a chord. Once one fires, the rest of that press is
   consumed.
3. **Clicks and holds.** A click fires on release, if the release comes before the hold
   threshold. A hold fires when its threshold is reached, not on release, so the user
   gets feedback while still holding. A press held past the threshold on a control
   with no hold action does nothing, which gives the user a way to abandon a click.
4. **Push-turn cancels.** Turning an encoder while its own button is down cancels that
   press's click and hold. The turn is delivered with its normal meaning. An
   accidental push while turning therefore never pauses the scan, flips its direction,
   toggles wrap or opens a menu.
5. **Modal layers cancel pending presses.** Entering Shift, opening a menu or starting
   the session hold cancels every other pending click and hold. Those presses are
   consumed. Releases of controls whose press was already delivered, such as PTT, are
   still delivered.
6. **Reconciliation never acts.** A release-all caused by lost input, overflow or
   staleness cancels every pending click, hold, Shift action and chord. It closes any
   open menu without committing and ends PTT. A synthesized release never fires an
   action (Principle III).
7. **Timing.** Resolution depends on event order except for the thresholds and the
   timeout below. These stay configurable until bench trials set them (Principle IV):

| Setting | Applies to | Starting value for bench trials |
|---|---|---|
| Encoder button hold threshold | Encoder 1, 2 and 3 holds; the click limit for Encoder 0 | 400 ms |
| Session hold threshold | Button 0 (decision 0005) | 1000 ms |
| Menu inactivity timeout | Engine menu and band menu | 5 s |

**Classic page bindings** *(user)*

8. An Encoder 0 turn sets the jump rate, and an Encoder 1 turn sets the jump distance.
9. An Encoder 0 click runs or pauses the scan. While paused, the receiver stays on the
   current frequency, and the jump rate and distance can still be set.
10. An Encoder 1 click toggles the scan direction between up and down.
11. Encoder 2 and Encoder 3 turns have no action.
12. Classic has one parameter page in this slice, as `54w.7` scopes it. An Encoder 3
    click therefore stays on that page.

Ranges, step sizes and scan behavior at band edges belong to `54w.7`.

**Manual page bindings** *(user)*

13. An Encoder 0 turn tunes the receiver by one band step per detent. At a band edge
    it wraps to the opposite edge when wrap is on, and stops at the edge when wrap is
    off (TuneStep with edge `wrap` or `stop` in the radio model). Acceleration and the
    other tactile ideas of the product's Manual section come later.
14. An Encoder 0 click toggles wrap. Wrap is part of Manual's engine state, so it is
    kept across the quick-jump and its return. It starts on, and the display shows it.
15. Encoder 1, 2 and 3 turns and the Encoder 1 click have no action.

**Engine and band menus** *(user; details proposed)*

16. *(user)* A long press of the Encoder 2 button opens the engine menu, replacing the
    held selector of C-013 to C-015. A long press of the Encoder 1 button opens the
    band menu. Each opens at the hold threshold and stays open after the release. The
    rest of the opening press is consumed.
17. A menu opens with the current item highlighted. The engine menu lists only engines
    that can be selected (`54w.7`). The band menu lists the receiver's bands: FM, AM,
    SW and LW.
18. While a menu is open:
    - An Encoder 0 turn scrolls.
    - An Encoder 0 click selects the highlighted item and closes the menu. If it is
      the current item, the menu just closes.
    - An Encoder 1 click closes the menu without a change.
    - The inactivity timeout closes the menu without a change.
    - Button 1 keeps its PTT meaning, because it changes monitoring, not navigation.
    - Every other press and turn is swallowed.
19. Selecting an engine switches to it, with its previous state restored. Selecting a
    band switches to it and restores that band's last frequency (SwitchBand in the
    radio model). In Classic, the new band becomes the scan territory, and Classic
    resumes in its current run or pause state.
20. A band change is acknowledged as decision 0008 item 13 specifies. The display shows
    the target band and then the result.
21. While a session is active and today's guard is in force, a long Encoder 1 press does
    not open the band menu. It is rejected with the "unavailable while recording"
    acknowledgement of decision 0008 item 10. Once `54w.12` qualifies band changes
    during recording, the menu opens as usual. The engine menu is available during a
    session (decision 0008).
22. The same menu form is proposed for the Instrument selectors (C-018 to C-023), for
    consistency. That change and D-003 stay open for the Instrument slice.

**Encoder 3: page, Shift and utility exit (D-002)**

23. On a Field engine page, an Encoder 3 press is pending until one of three events
    occurs:
    - Released before the threshold, with no other press during it: advance the
      parameter page (C-005).
    - Hold threshold reached: enter Shift (C-006).
    - Another control pressed while Encoder 3 is pending: enter Shift at once, and
      resolve that press in the Shift layer. A fast Shift combination therefore does
      not wait for the threshold.
24. When the Encoder 3 press has entered Shift, its release ends Shift and never
    advances the page (product intent, Shift Layer).
25. If another control's hold fires while Encoder 3 is pending, the Encoder 3 press is
    consumed, and neither a page advance nor Shift follows.
26. In a utility, Encoder 3 hold leaves the utility at the threshold (C-004). The rest
    of the hold is consumed, so the same hold never becomes Shift after the return.
    A short Encoder 3 press has no action in utilities.

**Shift layer**

27. Encoder button presses in Shift fire on press, because no chord includes them.
28. *(user)* **Manual quick-jump.** Shift plus an Encoder 1 press on a non-Manual engine
    enters Manual at the current tune target. It saves the previous engine and its
    state in a one-entry return slot. Shift plus an Encoder 1 press in Manual, entered
    this way, restores that engine and clears the slot. This replaces C-011 and C-012.
29. Choosing an engine through the engine menu clears the return slot. In Manual with
    an empty slot, Shift plus Encoder 1 has no action, and its light stays dark
    (decision 0008 item 8). Closing the menu without a change keeps the slot.
30. What state Classic resumes after Manual (product open decision 4) belongs to `54w.7`.
31. *(user)* **Utility entry.** Shift plus an Encoder 0 press opens the global utility
    root in Field, matching C-027 in Instrument. C-007 is retired. This answers D-001
    and, for the first slice, product open decision 13: Playback is reached through the
    root.
32. Button 0 and Button 1 presses in Shift resolve as one chord group:
    - The first button's action is held back until its release.
    - If the other button goes down before that release, the chord (C-010) fires
      immediately.
    - If the first button is released alone, its own action fires: the mode switch
      for Button 0 (C-008), the capture save for Button 1 (C-009).
    - After a chord, both releases are consumed.
33. A pending Shift button action survives the release of Encoder 3, because of press
    origin. Sloppy release order therefore never loses or changes an action.
34. A Shift action that changes mode or opens a utility ends the Shift layer: the mode
    switch, the chord or the utility root. Further presses are swallowed until
    Encoder 3 is released. The capture save and the Manual quick-jump keep Shift
    active, because the Shift layer is the same in both engines.
35. Encoder turns in Shift have no action in Field and are swallowed. Shift lights only
    the controls with an available action (decision 0008 item 8).
36. During a session, decision 0008 applies unchanged:
    - Shift plus Button 0 is rejected with its acknowledgement.
    - The chord saves the capture and rejects the mode switch.
    - Shift plus Button 1 saves.
    - The Manual quick-jump and its return are allowed.

**Chord permutations**

In every row, Encoder 3 is already pressed and Shift is active or entered by the first
button press. "Fires" means the action is issued through the shared command policy.

| # | Sequence after Encoder 3 down | Result |
|---|---|---|
| P1 | B0 down, B0 up | Mode switch fires at B0 up |
| P2 | B1 down, B1 up | Capture save fires at B1 up |
| P3 | B0 down, B1 down, B1 up, B0 up | Chord fires at B1 down; both releases consumed |
| P4 | B0 down, B1 down, B0 up, B1 up | Chord fires at B1 down; both releases consumed |
| P5 | B1 down, B0 down, B0 up, B1 up | Chord fires at B0 down; both releases consumed |
| P6 | B1 down, B0 down, B1 up, B0 up | Chord fires at B0 down; both releases consumed |
| P7 | B0 down, E3 up, B0 up | Mode switch fires at B0 up (press origin) |
| P8 | B1 down, E3 up, B1 up | Capture save fires at B1 up |
| P9 | B0 down, B1 down, E3 up, then both up in any order | Chord fires at the second down; releases consumed |
| P10 | B1 down, B1 up, B0 down, B0 up | Capture save at B1 up, then mode switch at B0 up |
| P11 | B0 down, B0 up, then B1 down and up | Mode switch at B0 up; Shift ends and B1 is swallowed until E3 is released (item 34) |
| P12 | Any pending state, then input reconciliation | Nothing fires; Shift ends (item 6) |
| P13 | B1 down in Normal (PTT), then E3 down, then B0 down and up | B1 stays PTT (press origin); Shift entered at B0 down; mode switch at B0 up; PTT ends at B1 up |
| P14 | B0 down in Normal (session hold), then E3 down | E3 swallowed (decision 0005 item 2); no Shift |
| P15 | E1 down, E1 up, E1 down, E1 up | Manual at the first press, return at the second; Shift stays active |

During a session, P1, P7 and P11 reject the mode switch. P3 to P6 and P9 save and
reject the switch (decision 0008).

**PTT (C-016, C-017)**

37. Button 1 in the Normal layer mutes the radio in the monitored mix from its press,
    with no threshold, until its release. Its gesture is a momentary press, not a
    threshold hold. PTT works while a menu is open (item 18). The session prompt still
    swallows it (decision 0005).

**Session hold in utilities** *(user)*

38. Button 0 has no action in utilities. The session hold is available only on Field
    and Instrument pages, as decision 0005 states. To stop a session, the user leaves
    the utility first. This answers the open note in the InputResolution diagram.

**During a session**

39. Decision 0008 classifies the new bindings as follows:
    - Classic run and pause, jump rate, jump distance and direction are allowed, like
      other parameter edits. Manual's wrap toggle is allowed too.
    - Manual tuning and Classic scan motion are radio commands. They follow decision
      0003 and today's guard until `54w.6` qualifies in-band tuning.
    - The band menu follows item 21, and the engine menu is allowed.
    - The Manual quick-jump and its return are allowed.

**Field control audit**

This audit applies to the first slice under this decision. "None" means the gesture has
no action and the press is consumed.

| Control | Normal click | Normal hold | Shift | Engine or band menu | Prompt | Utility |
|---|---|---|---|---|---|---|
| Encoder 0 button | Classic: run or pause; Manual: toggle wrap | None | Utility root (item 31) | Select | Confirm (C-092) | Select (C-002) |
| Encoder 1 button | Classic: toggle direction; Manual: none | Band menu (item 16) | Manual quick-jump or return (item 28) | Close | Dismissed | Back (C-003) |
| Encoder 2 button | None | Engine menu (item 16) | None | Swallowed | Dismissed | None |
| Encoder 3 button | Page advance (C-005) | Shift (C-006) | Not applicable | Swallowed | Dismissed | Hold leaves (C-004) |
| Button 0 | None | Session prompt (C-090) | Mode switch on release (C-008) or chord (C-010) | Swallowed | Release cancels (C-093) | None (item 38) |
| Button 1 | PTT from press (C-016, C-017) | Same press | Capture save on release (C-009) or chord (C-010) | PTT | Dismissed | None |
| Encoder 0 turn | Classic: jump rate; Manual: tune | Not applicable | Swallowed | Scroll | Dismissed | Scroll (C-001) |
| Encoder 1 turn | Classic: jump distance; Manual: none | Not applicable | Swallowed | Swallowed | Dismissed | None |
| Encoder 2 and 3 turns | None | Not applicable | Swallowed | Swallowed | Dismissed | None |

Two controls have both a click and a hold action in the same context: Encoder 3 on
engine pages, and Encoder 1 on the Classic page. Items 3 and 23 to 25 separate them.
Encoder 0 and Encoder 1 buttons share their encoders with turns, and item 4 separates
them.

## Consequences

- `54w.3` can implement Shift, clicks, holds, chords and both menus on
  InputResolutionSm. Its host tests cover P1 to P15, push-turn and reconciliation.
  They also include seeded random sequences asserting that Shift and PTT never outlive
  their releases.
- `54w.7` implements the Classic and Manual bindings, run and pause, Manual's wrap
  state, the return slot, and the band menu's effect on the Classic territory.
- Resolving Shift button actions and clicks on release adds the press duration to
  those actions. Bench trials judge whether that delay is acceptable. The capture-save
  snapshot boundary is taken when the save resolves.
- Protected documents need an approved proposal (`54w.23`), with the control map's
  audit:
  - Retire C-011 and C-012, and add Shift plus Encoder 1 rows for the Manual
    quick-jump and its return.
  - Retire C-007, and update the Global utility root entry in the workspace index.
  - Retire C-013 and C-015. Add rows for the long-press engine menu, its Encoder 0
    select, Encoder 1 close and timeout, and update the scan-engine selector entry in
    the workspace index. C-014 keeps its meaning.
  - Add rows for the Classic and Manual page bindings and the band menu, and add the
    band menu to the workspace index.
  - Change C-016's gesture to a momentary press.
  - Record items 1 to 7 and 23 to 35 against D-002, D-005 and D-006, and answer D-001.
  - In modes and interaction, reconcile the Manual quick-jump and engine-selection
    prose with this decision.
  - Change "Shift plus encoder 1 opens Settings" in both places it appears.
  - Answer open decisions 2 and 15 for Field.

  The modes-and-interaction text overlaps `54w.20`. The chord rows during a session
  overlap `54w.22`.
- Revisit if bench trials show release-resolved actions feel late, if push-turn
  cancellation triggers too easily, if the one-entry return slot is too shallow, or if
  a second Manual tuning control is wanted.

## Evidence

None yet. Host tests belong to `54w.3` and `54w.7`. Bench trials of the thresholds, the
timeout and the release-resolved actions belong to the UI integration work under
`54w.5`.
