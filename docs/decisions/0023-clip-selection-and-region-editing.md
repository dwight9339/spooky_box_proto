# 0023. Clip selection, region editing and the empty Instrument

- **Status:** Proposed
- **Date:** 2026-10-05
- **Supersedes:** none
- **Beads:** `full_spooky_proto-v7l.10` (decision)

Items marked *(user 2026-10-05)* were agreed in conversation. Items marked *(agent)* are
proposals awaiting the user's call. The Open questions section is removed before
acceptance.

## Context

The only defined way to get audio into Instrument is the C-010 chord, which saves the
rolling window and loads it as the active sample. Nothing defines:

- Instrument with no clip. C-008 (Shift + Button 0) enters Instrument without loading
  anything.
- Loading a different clip when one is active.
- How a clip is cut from a recording that may be hours long.

[Modes and interaction](../../spec/product/modes-and-interaction.md) defines Playback as
a global utility with a session browser that separates Field sessions, standalone
captures and Instrument sessions. In Instrument, Shift + Encoder 1 press has no
assignment (in Field it is the Manual quick-jump, C-094).

Clip length is bounded by memory, not by choice ([0024](0024-clip-sources-and-track-mixing.md)).

## Options

1. **Load the first N seconds of the selected recording.** Rejected as the only path: for
   a long session it rarely contains the moment the user wants.
2. **Confirm first, then edit the region.** Rejected: a confirmation dialog followed
   immediately by another editor; the active clip changes before the user has heard the
   new one.
3. **Chosen: select, choose and audition a region, then one Load or Replace action that
   activates the clip only once it is ready.**

## Decision

**Objects**

1. A recording and a clip are different objects. A recording may be hours long; a clip
   is a bounded interval within it. Trimming never changes the recording.
   *(user 2026-10-05)*

**Entry**

2. Shift + Encoder 1 press in Instrument opens clip selection ("Load clip").
   *(user 2026-10-05)*
3. Entering Instrument with no active clip opens clip selection automatically.
   *(user 2026-10-05)*

**Selection and activation**

4. Clip selection reuses the session browser with a caller-specific action: Playback
   opens a recording for review; Instrument opens it to extract a region.
   *(user 2026-10-05)*
5. The browser filters by readable, compatible audio rather than by recording type.
   Instrument sessions are included, so a recorded performance can become source
   material. Recordings still being written (an active session, the rolling buffer) are
   hidden. *(user 2026-10-05; hiding agent)*
6. Activation order: select a recording, choose and audition a region, confirm Load or
   Replace, prepare and validate the audio, make it active only when ready. Selecting a
   recording establishes a candidate; it does not replace the active clip.
   *(user 2026-10-05)*
7. The Load or Replace action is the guard. A further warning appears only when
   replacing would discard source-dependent edits such as custom slices.
   *(user 2026-10-05)*

**Region editor**

8. A recording opens directly into the region editor. *(user 2026-10-05)*
9. Provisional controls: Encoder 0 moves the window through the recording keeping its
   length; Encoder 1 moves the start boundary; Encoder 2 moves the end boundary;
   Encoder 3 sets the navigation and edit resolution. *(user 2026-10-05, provisional)*
10. Default window: the first N seconds of a session, but the last N seconds of a
    rolling capture, because a rolling capture ends at the moment the user saved.
    *(agent)*
11. The overview comes from the activity metrics and events the recording already stores
    (radio activity, EMF, PTT), not from reading the whole audio file. The editor can
    jump between marked events. *(agent)*
12. A press loops the window for audition. *(agent)*
13. The active clip's window can be re-edited later from the engine without going back
    through the browser. *(agent)*

**No clip and failures**

14. Cancelling clip selection leaves Instrument in an empty state: the OLED shows
    "No clip" and how to load one, and engines are silent. It does not return to Field.
    *(agent)*
15. A load that fails (missing or corrupt file, card removed) leaves the previous clip
    active. With no previous clip, the empty state shows the fault. *(agent)*
16. C-008 back into Instrument restores the clip, engine and view that were active
    earlier in this power cycle. *(agent)*

## Open questions

- Encoder 0 (move the window) and Encoder 1 (move the start) overlap: both move the
  start. The user is undecided on keeping a dedicated start control. Agent view: keep
  both. Moving the window keeps the length and is for finding the moment; moving the
  start changes the length and is for trimming.
- With Encoder 3 on resolution, where do event jumps (item 11) go: a press, or a
  resolution step that snaps to events?
- How does the end boundary behave at the maximum clip length: clamp, or push the start
  along with it?
- Does loading a clip during an Instrument session (C-091) conflict with recording
  safety (control map D-015)?
- Should the C-010 load failure in product behave like item 15 instead of returning to
  Field (the demo returns to Field, [0011](0011-halloween-2026-demo-build.md) item 15)?
- Does the active clip persist across power cycles, and how does a preset reference it
  (control map D-009)?
- Minimum clip length?

## Product document changes on acceptance

These are proposals to protected documents and need separate approval.

- [Control map](../../spec/product/control-map.md): new row C-112, Instrument, main
  engine page, Shift, Encoder 1 button, Press, "Open clip selection", "Session browser
  filtered for clip sources", Defined, this record. Rows for the region editor controls
  in item 9.
- [Modes and interaction](../../spec/product/modes-and-interaction.md), Field To
  Instrument Workflow and Playback And Session Browser: the browser's caller-specific
  action, the recording-versus-clip distinction, and the empty Instrument state.

## Consequences

- The overview in item 11 depends on the session format storing derived metrics
  (`full_spooky_proto-hpq.3`).
- The demo builds none of this; it shows the empty state only
  ([0020](0020-halloween-demo-slicer-and-sequencers.md) item 14).

## Evidence

None yet.
