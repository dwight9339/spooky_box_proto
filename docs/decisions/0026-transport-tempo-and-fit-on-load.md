# 0026. Transport tempo and fit on load

- **Status:** Accepted 2026-10-07
- **Date:** 2026-10-06
- **Supersedes:** [0022](0022-slicer-engine.md) item 10 in part (the one-bar BPM set on
  load) and [0020](0020-halloween-demo-slicer-and-sequencers.md) item 9 in part (the same,
  for the demo), and adds to 0020 item 5 (a settings row in the demo step view). The rest
  of both records stands.
- **Beads:** `full_spooky_proto-p04.19` (decision)

## Context

[0021](0021-instrument-engines-and-performance-views.md) item 5 makes the transport and
clock shared infrastructure, and item 7 makes tempo and swing transport settings, so
switching engines does not change the groove. Engines have no sense of time: they react
to events from the sequencer (item 12), from external MIDI (item 18) and from their own
controls. The transport is the one clock that sequencers, synced modulation, synced
effects, the matrix and session metadata read. Division and length are pattern settings
(0021 item 10).

0022 item 10 and 0020 item 9 set the BPM on load so the clip spans one bar. With 16 steps
of 1/16 the BPM is 240 divided by the clip length in seconds: 80 BPM for a 3 s clip,
48 BPM for the 5 s demo clip of [0025](0025-demo-clip-in-d2-sram.md), 240 BPM for a 1 s
clip. No record sets a tempo range, a default or a meter.

Radio scan audio has no tempo of its own, so a tempo fitted to a clip is a starting
point, not a property of the material. Curated slices of uneven length cannot all fill
equal steps at any tempo; 0022 item 3 already covers that (a slice plays until its end or
the next step).

The demo builds the step view from C-072 to C-077 only (0020 item 5), so it has no way to
edit the tempo. Control map C-077 enters the sequencer settings row and C-078 to C-082
browse and edit it. How focus returns from the settings row to the steps is control map
D-012, still open.

## Options

1. **Keep the one-bar fit on load.** Gives 48 BPM for the demo clip and 240 BPM for a 1 s
   clip. Rejected.
2. **Loading never changes the tempo.** The first loop only sounds like the clip if the
   tempo happens to match. Rejected.
3. **An explicit fit-to-clip action instead of a fit on load.** Keeps the clock under the
   user's hand, at the cost of one more step before the first loop. Deferred: re-evaluated
   after the demo (item 9).
4. **Chosen: fit on load, choosing the division as well as the tempo,** so the first loop
   sounds like the clip at a usable tempo.

## Decision

**Transport model**

1. Tempo and meter belong to the transport. Division and length belong to each pattern
   (0021 item 10). Meter is fixed at 4/4 and is used only for bar-level behavior: the
   matrix bar cycle, any bar-quantized action and export. A new pattern is 16 steps at
   1/16, one bar.

**Fit on load**

2. Every clip load fits the transport to the clip: it sets the tempo and every pattern's
   division so 16 steps span the clip, choosing the division that puts the tempo in the
   fit range (item 3). The Slicer's identity pattern (0022 item 10) then sounds like the
   clip looping. The fit is the same whichever engine is active.
3. With N steps, clip length L seconds and q steps per beat (4 for 1/16, 2 for 1/8, 1 for
   1/4), the fitted tempo is 60·N / (L·q) BPM. The fit range is 70 to 140 BPM. Because
   the range spans a factor of 2, one division lands in it for any clip from 1.71 s to
   13.7 s at 16 steps; on a tie the finer division wins. A shorter clip uses 1/16 and its
   tempo is clamped to the editable maximum, so steps leave gaps between slices.

   | Clip (16 steps) | Division | Tempo |
   |---|---|---|
   | 5 s | 1/8 | 96 BPM |
   | 4 s | 1/8 | 120 BPM |
   | 3 s | 1/16 | 80 BPM |
   | 2 s | 1/16 | 120 BPM |
   | 1 s | 1/16 | 200 BPM (clamped from 240) |

4. A load while the transport runs applies the fit at the next step.
5. Tempo, swing and division edited by hand stay until the next clip load replaces them.
   Division edits change only the active engine's pattern (0021 item 10).
6. With an external clock (not yet admitted), the fit sets only the division: the tempo
   comes from outside.
7. The product tempo range, default and swing are left to the Granular feature spec and
   the transport work (demo-track rule 5 keeps item 8 from settling them).

**Demo**

8. The demo step view gains a settings row built on C-077 to C-082 with two entries:
   Tempo (transport, 40 to 200 BPM in steps of 1, default 120 at boot) and Division (the
   active engine's pattern: 1/16, 1/8, 1/4). An Encoder 0 hold in the settings row
   returns focus to the steps, mirroring C-077; this is provisional and does not settle
   D-012. If time runs short, Division is cut first; Tempo is the floor.

**Later**

9. Fit on load is re-evaluated after the demo (`full_spooky_proto-v7l.14`), with an
   explicit Fit to clip action as the main alternative (option 3).

## Consequences

- p04.14 builds the transport with the item 8 range and default, the settings row of
  item 8, and the fit calculation of item 3 with host tests. p04.15 replaces the one-bar
  BPM with that fit.
- A load replaces any hand-set tempo and division, including Granular's division when
  the Slicer is what the user cares about, and the reverse.
- The matrix hue (0020 item 13) turns once per 4/4 bar. With the 5 s demo clip the fit
  gives 1/8, so the hue cycles twice per pattern loop.
- If Instrument sessions later store sequencer events as beat positions, their metadata
  needs the tempo changes made during the recording; a note goes to
  `full_spooky_proto-hpq.3`.

## Product document changes on acceptance

These are proposals to protected documents and need separate approval
(`full_spooky_proto-v7l.12`).

- [Control map](../../spec/product/control-map.md) C-080: with 0021's split, the transport
  settings are tempo and swing (meter fixed at 4/4).
- [Modes and interaction](../../spec/product/modes-and-interaction.md), Sequencer: a clip
  load fits the tempo and every pattern's division (items 2 and 3).

## Evidence

None. Host tests for the fit calculation follow in p04.14.
