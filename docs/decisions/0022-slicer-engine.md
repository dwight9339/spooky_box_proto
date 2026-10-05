# 0022. Slicer engine: slice map, slice page and sequencer

- **Status:** Proposed
- **Date:** 2026-10-05
- **Supersedes:** none
- **Beads:** `full_spooky_proto-v7l.9` (decision)

Items marked *(user 2026-10-05)* were agreed in conversation. Items marked *(agent)* are
proposals awaiting the user's call. The Open questions section is removed before
acceptance.

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
   *(user 2026-10-05)*
2. The slice map is a shared data type ([0021](0021-instrument-engines-and-performance-views.md)
   item 5) so other engines may read it later. *(user 2026-10-05)*

**Playback**

3. One voice. A new step cuts the previous slice with a short crossfade. A slice plays
   until its end or the next step, whichever comes first. BPM changes truncate slices or
   leave gaps; there is no time-stretch. *(user 2026-10-05, L2, L3)*

**Slicer page**

4. Slice count (4, 8 or 16) is an engine-level control. *(user 2026-10-05)*
5. Encoder 0 selects a slice; an Encoder 0 press enters a slice-selected state showing
   that slice's parameters, echoing the sequencer's step browse and edit (C-072, C-073).
   *(user 2026-10-05)*
6. Per-slice parameters: pitch, gate, level and length. Length moves the slice's end,
   which is also the next slice's start. *(user 2026-10-05)*
7. Length has a minimum (about 10 ms) so a slice cannot shrink to a click. The last
   slice's end is the clip end and is not edited here. *(agent, L7)*
8. Selecting a slice auditions it once while the transport is stopped. *(agent, L7)*
9. A slice-count change is confirmed: turn to choose, press to apply. If boundaries or
   per-slice parameters were edited, the OLED asks before discarding them.
   *(agent, L5)*

**Sequencer**

10. A step's value is a slice. On load: identity pattern (step n plays slice n) with the
    BPM set so the clip spans one bar. *(user 2026-10-05, L1)*
11. When the slice map changes, the sequencer follows it automatically. When the slice
    count changes, each step moves to the new slice that contains the start of its old
    slice (nearest-neighbor by time, not by index). *(user 2026-10-05; time-based rule
    agent, L6)*
12. Per-step extras beyond on/off and slice are engine-owned and open: rests, mute,
    probability, ratchets, pitch, reverse, swing, per-step parameter overrides.
    *(user 2026-10-05, as candidates)*

## Open questions

- Pitch per slice: playback-rate change (pitch and length change together) or something
  else? Rate change is the only option that fits the time budget now.
- Should a slice's start be editable directly, or only through the previous slice's
  length?
- Gate: fraction of the slice, or of the step?
- Which per-step extras come first after the demo (item 12)?
- Does a disabled slice (item 1) mean silent when stepped, or skipped?
- A "shuffle the pattern" gesture: worth a control, and which one?
- Per-slice source overrides ([0024](0024-clip-sources-and-track-mixing.md)).
- Is the slice map stored with the clip, the engine, or the preset?

## Consequences

- The slice map, the time-based remap and the sequencer stepping are portable logic and
  belong in `Common/` with host tests.
- The Slicer is post-M5 product scope; a roadmap proposal is needed to place it.

## Evidence

None yet.
