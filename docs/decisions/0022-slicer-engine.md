# 0022. Slicer engine: slice map, slice page and sequencer

- **Status:** Accepted 2026-10-06; the one-bar BPM on load in item 10 superseded by
  [0026](0026-transport-tempo-and-fit-on-load.md)
- **Date:** 2026-10-05
- **Supersedes:** none
- **Beads:** `full_spooky_proto-v7l.9` (decision)

## Context

[0021](0021-instrument-engines-and-performance-views.md) makes Slicer its own engine:
cut captured audio up and rearrange it rhythmically. Its musical model is about events
and regions: which slice plays, in what order, with what timing. The Halloween demo
needs a narrow version of it ([0020](0020-halloween-demo-slicer-and-sequencers.md)).

The clip it reads is defined in [0023](0023-clip-selection-and-region-editing.md) and
[0024](0024-clip-sources-and-track-mixing.md).

## Options

1. **Equal divisions only.** Simple, but the user wants to resize slices.
2. **Chosen: a slice map with custom boundaries.** Equal 4, 8 or 16 slicing is only how
   the map is initialized.
3. **Transient-detected slices.** Deferred; it can be another way of initializing the
   same map.

## Decision

**Slice map**

1. A slice map is an ordered list of slices covering the clip without gaps or overlap.
   Each slice has a start, an end and an enabled flag. The end of slice n is the start of
   slice n+1; the first slice starts and the last ends at the clip boundaries.
2. The slice map is a shared data type ([0021](0021-instrument-engines-and-performance-views.md)
   item 5) so other engines may read it later.

**Playback**

3. One voice. A new step cuts the previous slice with a short crossfade. A slice plays
   until its end or the next step, whichever comes first. BPM changes truncate slices or
   leave gaps; there is no time-stretch.

**Slicer page**

4. Slice count (4, 8 or 16) is an engine-level control.
5. Encoder 0 selects a slice; an Encoder 0 press enters a slice-selected state showing
   that slice's parameters, echoing the sequencer's step browse and edit (C-072, C-073).
6. Per-slice parameters: pitch, gate, level and length. Length moves the slice's end,
   which is also the next slice's start.
7. Length has a minimum (about 10 ms) so a slice cannot shrink to a click. The last
   slice's end is the clip end and is not edited here.
8. Selecting a slice auditions it once while the transport is stopped.
9. A slice-count change is confirmed: turn to choose, press to apply. If boundaries or
   per-slice parameters were edited, the OLED asks before discarding them.

**Sequencer**

10. A step's value is a slice. On load: identity pattern (step n plays slice n) with the
    BPM set so the clip spans one bar.
11. When the slice map changes, the sequencer follows it automatically. When the slice
    count changes, each step moves to the new slice that contains the start of its old
    slice (nearest-neighbor by time, not by index).
12. Per-step extras beyond on/off and slice are engine-owned: rests, mute, probability,
    ratchets, pitch, reverse, swing, per-step parameter overrides. Item 18 orders them.

**Slice detail**

13. A disabled slice is silent wherever it would play, from a sequencer step or a MIDI
    note. It is not skipped, so the timing does not shift. This mutes a slice
    throughout the pattern, which a step's off flag cannot do.
14. The slice map is part of the Slicer's state and is saved in its preset with a
    reference to its clip, as the control map's preset scope lists ("slice markers and
    sample trim metadata"). Loading a new clip keeps the slice count, the per-slice
    parameters and the pattern ([0021](0021-instrument-engines-and-performance-views.md)
    item 10) and resets the boundaries to equal slices. The replace warning of
    [0023](0023-clip-selection-and-region-editing.md) item 7 appears only when the
    boundaries had been customized.
15. Pitch changes the playback rate, so a slice played higher is also shorter. The range
    is ±12 semitones. Rate change is the only option within the render budget, and
    item 3 rules out time-stretch.
16. A slice's start is editable as well as its length. Start moves the boundary shared
    with the previous slice; length moves the one shared with the next. The first
    slice's start is the clip start, mirroring item 7. The slice-selected state has two
    pages (C-024): Sound (pitch, gate, level) and Boundaries (start, length). The demo
    page follows [0020](0020-halloween-demo-slicer-and-sequencers.md) item 11 and has no
    start control.
17. Gate is a fraction of the slice's own pitched length: it trims the slice's tail
    whatever the tempo. A gate relative to the step would be a per-step extra.

**Sequencer extras**

18. After the demo, per-step extras come in this order: probability and ratchets, then
    reverse. Swing is a transport setting (0021 item 7), a rest is a step that is off,
    per-step parameter overrides are deferred (0021 item 11), and per-step pitch is
    left out because slices carry their own pitch (item 15).
    - **Probability:** each step has a chance (0 to 100 %, default 100 %) of firing each
      time the playhead reaches it, rolled independently on every pass. A step that is
      off never fires.
    - **Ratchets:** a count from 1 to 4 (default 1). A step that fires retriggers its
      slice that many times, evenly spaced within the step. Each retrigger restarts the
      slice with the usual crossfade, so a repeat is cut by the next. Probability decides
      whether the step fires; ratchets apply only when it does. Swing moves only the
      step's start.
    - **Editing:** in step edit (C-074), Encoder 0 sets the slice, Encoder 1 turns
      probability and Encoder 2 turns the ratchet count. An Encoder 3 turn is kept for
      reverse.
19. Shuffle is an action row in the sequencer settings (C-078 to C-082) and belongs to
    the sequencer shell, so every engine has it. Pressing it rearranges the values of
    the steps that are on, leaving each step's on/off flag and extras where they are:
    the rhythm stays, and every slice is used as often as before, in a new order. Each
    press reshuffles. The first press keeps a copy of the pattern, and a Restore row
    returns to it until the pattern is edited by hand. The copy is held in RAM only.

**External MIDI**

20. The MIDI key map of [0021](0021-instrument-engines-and-performance-views.md) item 19:
    one slice per note counting up from a base note (36 by default, adjustable);
    velocity sets the slice's level; note-off is one-shot by default (the slice plays to
    its end) or gated (releasing the key stops it with a short fade, and the gate of
    item 17 is then the shortest it plays); notes past the last slice are ignored; one
    voice, and a new note cuts the current slice with the usual crossfade.

## Consequences

- The slice map, the time-based remap and the sequencer stepping are portable logic and
  belong in `Common/` with host tests.
- The Slicer is post-M5 product scope; its roadmap line is part of the proposal tracked
  in `full_spooky_proto-v7l.12` (0021).
- Per-slice source overrides are settled in
  [0024](0024-clip-sources-and-track-mixing.md) (item 3).

## Evidence

None yet.
