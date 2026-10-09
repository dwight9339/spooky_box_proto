# 0027. Instrument engine switching

- **Status:** Accepted 2026-10-07
- **Date:** 2026-10-07
- **Supersedes:** none
- **Beads:** `full_spooky_proto-p04.20` (decision); backlog `full_spooky_proto-v7l.15`

## Context

A walk-through on 2026-10-07 followed one session: load a 5 s clip, slice it in the
Slicer, sequence it, slow the tempo, add swing, then switch to Granular. Accepted records
already settle much of what the switch does:

- Only one engine runs at a time ([0021](0021-instrument-engines-and-performance-views.md)
  item 2), so the Slicer stops sounding.
- Each engine keeps its pattern and state across switches (0021 item 10); the Slicer's
  slice map and per-slice parameters are part of its state
  ([0022](0022-slicer-engine.md) item 14).
- The transport keeps running with its tempo and swing (0021 item 7).
- The effects chain carries over (0021 item 14); macros and modulation routes switch to
  the new engine's own (0021 item 13); the source mix is the new engine's own
  ([0024](0024-clip-sources-and-track-mixing.md) items 3 and 4).
- In the demo, the matrix changes view with the engine
  ([0020](0020-halloween-demo-slicer-and-sequencers.md) item 13).
- The engine selector opens only from the main engine page (control map C-018 to C-020);
  its commit and cancel behavior is provisional in the demo (0020 item 12).

Left open:

- What Granular plays first. No record sets Granular's starting pattern, and 0021
  item 12 leaves the meaning of a trigger to each engine's own record. Granular has none
  yet; the demo's step value is a position and grains continue between steps (0020
  item 8).
- When the switch happens and how the old engine's sound hands over.
- Where a pattern's playhead is when its engine becomes active again.
- Which page the switch lands on.
- Granular plays the whole clip, including stretches the user disabled in the Slicer.

[Modes and interaction](../../spec/product/modes-and-interaction.md), Remember Context,
says returning to an engine should normally restore its current performance view. Item 4
departs from that for Instrument engines; the change is proposed below. The control
map's preset scope already excludes the current UI page, restoring "a predictable
default performance page instead".

## Options

For the switch timing:

1. **Quantized to the next step or bar.** Keeps the switch on the grid but adds up to a
   bar of lag, and the transport already keeps time across the switch. Rejected.
2. **Chosen: immediate, with a short crossfade.**

For the landing page:

1. **Restore the new engine's last view.** Follows Remember Context, but the user may land
   in a sequencer or slice editor of an engine whose sound they have not heard yet.
   Rejected.
2. **Chosen: always the new engine's main page.**

## Decision

1. An engine switch takes effect as soon as the selector commits, with a short crossfade
   from the old engine's output to the new one.
2. The crossfade is about 10 ms. If the render budget cannot hold both engines for that
   long, the old engine fades out and the new one fades in over the same total time.
   The demo uses the fade-out, fade-in form unless a measurement shows both engines fit
   the 1.5 ms render budget together.
3. Every pattern's playhead follows the transport position: the current step is the
   number of whole steps since the transport last started, modulo the pattern's length, at
   the pattern's own division. An inactive engine's pattern fires nothing. A switch
   therefore lands mid-pattern, in time, and switching back resumes the old pattern in
   time, not at step 1.
4. A switch always lands on the new engine's main page, whatever view was open when it
   was last active. Its parameters, pattern and other state are kept as before.
5. Granular's starting pattern is 16 steps, all on, with step n at position n/16 of the
   clip, so an untouched pattern sweeps the clip in order. It is a starting point for the
   Granular feature spec, which may change it.
6. In the demo, a Granular step that fires moves the grain position and grains continue
   (0020 item 8); a trigger has no other effect. The product meaning of a Granular
   trigger stays with the Granular feature spec.
7. Whether Granular should use the Slicer's slice map, for example to keep positions
   inside enabled slices or to snap step positions to slice starts, is a product
   question for after the demo. Until then Granular reads the whole clip.

## Consequences

- p04.14 builds items 3, 5 and 6 into the transport and the Granular sequencer, with host
  tests for the playhead rule. p04.15 adds the switch of items 1, 2 and 4 when the Slicer
  arrives.
- In the fade-out, fade-in form the switch is a short audible dip, not a seamless
  blend.
- Backlog: a Granular task to read the slice map (item 7).

## Product document changes on acceptance

These are proposals to protected documents and need separate approval
(`full_spooky_proto-v7l.12`).

- [Modes and interaction](../../spec/product/modes-and-interaction.md), Remember Context:
  "Returning to an engine should normally restore its frequency, scan state, parameters,
  and current performance view rather than resetting it" becomes "Returning to an engine
  should normally restore its frequency, scan state and parameters rather than resetting
  them. Instrument engines always open on their main page (decision 0027)."
- Same document, Sequencer: each pattern's playhead follows the shared transport (item 3).

## Evidence

None. Host tests for the playhead rule follow in p04.14.
