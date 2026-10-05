# 0020. Halloween demo Instrument scope: Slicer, sequencers and a tempo matrix

- **Status:** Accepted 2026-10-05
- **Date:** 2026-10-05
- **Supersedes:** [0011](0011-halloween-2026-demo-build.md) item 16 in part (slice
  quantization; its grain-activity matrix stays for Granular) and the checkpoint 3
  fallback in item 21, and adds gates to checkpoint 2. The rest of 0011
  stands.
- **Beads:** `full_spooky_proto-p04.13` (decision); epic `full_spooky_proto-p04`

## Context

Decision 0011 item 16 limits the demo's Instrument performance to one granular voice with
provisional pages, slice quantization for a chopped loop, and a matrix showing grain
activity. The user wants the video to show a clip turned into something musical entirely
inside the box. That needs a Slicer engine and a sequencer view for each engine.

This record extends the demo scope, so it must name its departures from the constitution
the same way 0011 does (roadmap, demo-track rule 1). The product shape of these engines is
proposed separately in [0021](0021-instrument-engines-and-performance-views.md) (engine
and view ownership) and [0022](0022-slicer-engine.md) (Slicer). The demo's choices are
provisional and do not settle them (demo-track rule 5).

Facts that shape the scope:

- Firmware freeze is 2026-10-21 (0011 item 21). On 2026-10-05, p04.6 and p04.7 are on
  branches and have not run on hardware.
- The granular render runs in the radio interrupt with a 1.5 ms budget (p04.7). A second
  engine must fit the same budget; only one engine runs at a time (0021).
- The clip is mono radio, 24 kHz, at most 3 s, in free AXI SRAM (0011 item 15). Nothing in
  this record changes that.
- The p04.7 branch puts `slices` in the parameter struct of `Common/Inc/granular.h`, the
  portable core intended as the start of `v7l.1`. Slicing would then be part of the
  product granular engine, which 0021 rejects.
- Control map C-072 to C-084 define one Sequencer view (note bars, value edit, step
  toggle, settings). C-018 to C-020 define the engine selector; its Instrument
  commit/cancel behavior (D-005) is open.

## Options

1. **Keep 0011 item 16 as written.** Granular with slice quantization only. Rejected by the
   user: it shows texture, not a clip rearranged into music.
2. **One engine with a slice mode.** Rejected: it contradicts the engine split in 0021 and
   would put slicing into the portable granular core.
3. **Chosen: Granular and Slicer as separate demo engines, each with a sequencer view,
   delivered in order with a working fallback at each step.**

## Decision

**Scope, in delivery order.** Each step is a shootable fallback if the next misses the
freeze.

1. Granular alone, as in p04.7, minus slice quantization (item 6).
2. The transport and sequencer shell (items 4, 5 and 7), proven on Granular with its
   sequencer view (item 8). The Slicer needs the same shell, since its first sound is
   the identity pattern running on the transport (item 9). Building the Slicer first
   saves no shared work and delays the first fallback that has a sequencer.
3. Slicer plus its sequencer view (items 9 to 11).

**Shared demo behavior**

4. One transport for Instrument: BPM and run/stop, kept across engine and view switches.
   Each engine keeps its own pattern. No swing in the demo.
5. Sequencer and parameter editing happen on the OLED only. The step view follows C-072
   to C-077: Encoder 0 browses steps, a press edits one, Encoder 0 turns its value,
   Encoder 1 button toggles it on or off. 16 steps.
6. Slice quantization is removed from Granular. The portable core in `Common/` carries no
   slice parameter.
7. While a sequence drives a parameter, the live knob for that parameter offsets the
   sequence instead of overriding it.

**Granular sequencer**

8. A step's value is a playback position in the clip. Grains continue between steps; a
   step that is off keeps the previous position.

**Slicer**

9. On load: 16 equal slices, identity pattern (step n plays slice n), and BPM set so the
   clip spans one bar. The loaded clip first sounds like itself looping.
10. One voice. A new step cuts the previous slice with a short crossfade. A slice plays
    until its end or the next step, whichever comes first. BPM changes truncate slices
    or leave gaps; no time-stretch.
11. Slicer page as in [0022](0022-slicer-engine.md) items 4 to 6: slice count at engine
    level; Encoder 0 selects a slice and a press opens its pitch, gate, level and length.
    If time runs short, controls are cut in this order: length editing, audition on
    select, gate, level, pitch, slice count. The floor is 16 fixed slices driven by the
    sequencer alone. 0022 items 7 to 9 apply to the demo only for the controls that
    remain.

**Navigation and display**

12. Granular and Slicer are switched with the engine selector (C-018 to C-020). Its
    commit and cancel behavior is provisional and does not settle D-005.
13. The matrix follows the active engine.
    - Granular keeps the grain-activity view of 0011 item 16 as built in p04.7: the
      columns are the clip, and each sounding grain lights the column it is reading.
    - Slicer shows one solid color rotating through the spectrum, one full cycle per bar.
      The color freezes while the transport is stopped.
    - With no clip loaded, the matrix is dark.
    - Neither view shows EMF, so the Field-to-Instrument sensor policy is not exercised
      (0011 item 16, Principle II).
    - Instrument never records in the demo: mode switches are blocked during a session
      and the session prompt is refused in Instrument (0011 item 15). The recording-ring
      guard in p04.7 stays as a guard.
14. If C-008 enters Instrument with no clip, the OLED shows the empty state p04.6 already
    draws ("NO CLIP", with the C-010 chord as the hint) and engines are silent. A failed
    clip load still returns to Field (0011 item 15); the failure behavior proposed in
    [0023](0023-clip-selection-and-region-editing.md) is not used in the demo. No
    browser is built for the demo.
15. The clip stays mono radio (0011 item 15).

**Schedule.** The checkpoint dates in 0011 item 21 stand.

16. Checkpoint 2 (2026-10-14) adds a gate: Granular (step 1) runs on hardware.
17. Slicer work (step 3) starts only after step 2 runs on hardware.
18. The freeze image (checkpoint 3, 2026-10-21) carries the last step proven on
    hardware. This replaces the checkpoint 3 fallback in 0011 item 21.

**Principles.** 0011 items 17 to 20 apply unchanged. The added departure from the
milestone sequence is items 1 to 15; Slicer and sequencers are post-M5 product scope
(roadmap "After M5"). The rejected alternative that keeps the sequence is option 1.

## Consequences

- p04.7 changes before merge: drop `slices` from `Common/Inc/granular.h`, from page 2
  and from their tests. The grain-activity matrix stays. p04.6 and p04.7 can then go to
  the bench without waiting for the rest of this record.
- New demo tasks under `full_spooky_proto-p04` after acceptance: the transport and
  sequencer shell, proven on Granular; the Slicer engine, its page and its hue-rotation
  matrix; the Slicer sequencer. Portable logic (slice map,
  step remap, sequencer stepping) goes in `Common/` with host tests where it may become
  product code; the rest is demo-only behind the `Demo` preset.
- Demo tasks never close product tasks; notes go to `v7l.1` and to whatever product task
  the Slicer record (0022) produces.

## Evidence

None yet. Rehearsal evidence follows 0011 item 21.
