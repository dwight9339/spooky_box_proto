# 0021. Instrument engines own their performance-view semantics

- **Status:** Proposed
- **Date:** 2026-10-05
- **Supersedes:** none
- **Beads:** `full_spooky_proto-v7l.8` (decision); epic `full_spooky_proto-v7l`

Items marked *(user 2026-10-05)* were agreed in conversation. Items marked *(agent)* are
proposals awaiting the user's call. The Open questions section is removed before
acceptance.

## Context

[Modes and interaction](../../spec/product/modes-and-interaction.md) lists the Instrument
engines as "Granular, Sampler and Slice concepts, Future engines", and the performance
views as Effects, Modulation, Macros and Sequencer. Principle VII says engines are
distinct models of sound generation, not presets, and views operate around the active
engine. The Sequencer section already says it "may sequence slices, triggers, macro
values, parameter locks, or other engine-specific events".

The control map defines one Sequencer view (C-072 to C-084) built on note bars, a note
value "within current scale", a step toggle and a settings row (tempo, division, scale).
Nothing yet says which parts of a view are shared across engines and which belong to
the engine.

The user wants to split the placeholder into three engines with different musical
semantics:

- **Granular:** make captured audio into a texture.
- **Slicer:** cut captured audio up and rearrange it rhythmically.
- **One-shot:** play captured audio as pitched notes.

All three read the same clip ([0023](0023-clip-selection-and-region-editing.md),
[0024](0024-clip-sources-and-track-mixing.md)).

## Options

1. **One engine with modes.** Rejected: the modes want different sequencers, macros and
   performance behavior, and the shared state grows a field for every mode.
2. **One universal step format with optional fields.** Rejected: steps whose fields mean
   nothing for the active engine, and a UI that shows them.
3. **Chosen: separate engines over shared infrastructure.** Each engine owns its sound
   semantics and the meaning of its views. The views share a shell: navigation,
   gestures, transport and persistence.

## Decision

**Engines**

1. Instrument engines are Granular, Slicer and One-shot. *(user 2026-10-05)*
2. Exactly one engine runs at a time. Running several engines together, and other
   cross-engine interactions, are experiments for a post-M5 milestone.
   *(user 2026-10-05)*
3. One-shot is tone-based: it plays the clip as pitched notes, driven by external MIDI or
   by the sequencer. It has no dedicated performance trigger on the panel.
   *(user 2026-10-05)*

**Ownership**

4. An engine owns its playback or synthesis semantics and its interpretation of every
   attached view: Sequencer, Macros, Modulation and Effects. *(user 2026-10-05)*
5. Shared infrastructure: clip storage and access, the transport and clock, BPM and
   timing primitives, swing math, pattern and preset persistence, parameter IDs, and the
   slice map type ([0022](0022-slicer-engine.md)). *(user 2026-10-05)*
6. Views share navigation and gesture conventions: the same gesture does the same kind of
   thing in every engine. Page advance, page colors, transport start and stop, step
   browse and edit, and return to the performance page behave identically. *(agent)*

**Sequencer shell**

7. One Instrument-wide transport (BPM, run and stop) keeps running across engine and
   view switches. Each engine keeps its own pattern. *(user 2026-10-05, S1)*
8. Every engine's sequencer is a row of steps on the OLED. Each step has an on/off flag
   and one primary value, edited with C-072 to C-077. The engine defines the value's
   meaning and range: a note for One-shot, a slice for Slicer, a position for Granular.
   Per-step extras (ratchet, reverse, probability and so on) belong to the engine.
   *(user 2026-10-05, S2 as revised)*
9. While a sequence drives a parameter, the live knob for it offsets the sequence rather
   than overriding it. *(user 2026-10-05, S3)*
10. A pattern belongs to its engine and survives engine switches. Loading a new clip keeps
    the pattern. *(user 2026-10-05, S4)*

**Display**

11. All parameter and sequence editing happens on the OLED. The matrix gives glanceable
    feedback (activity, motion, intensity, identity) and never shows editing detail.
    *(user 2026-10-05)*
12. The matrix's product behavior in Instrument is open (see Open questions).

## Open questions

- What does the matrix show in Instrument for the product? The demo uses a tempo hue
  rotation ([0020](0020-halloween-demo-slicer-and-sequencers.md) item 13).
- Does EMF keep a role in Instrument (for example as a modulation source), and how does
  the Field-to-Instrument sensor policy (fixed baseline) show on the matrix?
- Which gestures must be identical across engines (item 6), stated as a list in the
  control map, and which may differ?
- Macros and Modulation: are sources shared and targets engine-owned? Are macro slots
  per engine or global?
- Effects: is the FX chain per engine or one chain after whichever engine runs (D-008
  asks the preset side of this)?
- Swing: per pattern or global?
- Granular's product sequencer beyond position steps: parameter locks per step, or a
  stepped modulation source routed through the Modulation view?
- One-shot: MIDI input path (USB MIDI alongside the CDC port, or serial MIDI), which core
  owns it, polyphony and voice stealing, and scale handling (C-074 already says "within
  current scale").
- Does performance record and replay (M5) capture engine parameters and events, or
  rendered audio?
- How does microphone injection (C-029, "depends on the engine") behave in each engine?
- Mapping onto the roadmap: M5 delivers one Granular engine. Slicer, One-shot and the
  sequencer are post-M5; a roadmap proposal is needed to place them.

## Product document changes on acceptance

These are proposals to protected documents and need separate approval.

- [Modes and interaction](../../spec/product/modes-and-interaction.md), Mode Hierarchy:
  replace `Sampler and Slice concepts` with two entries, `Slicer` and `One-shot`.
- Same document, Instrument Mode: add a short section per engine stating its intent
  (the three lines in Context), and a paragraph stating items 4 to 6.
- Same document, Sequencer: add that each engine defines the meaning of a step's value,
  and that editing happens on the OLED (items 8 and 11).
- [Control map](../../spec/product/control-map.md) C-072 to C-076: generalize "note bar"
  and "note value" to "step" and "step value (engine-defined)", keeping "within current
  scale" for pitched engines.

## Consequences

- The demo's slice quantization in Granular is not product behavior; the portable
  granular core carries no slice parameter.
- Spec work follows: a Granular feature spec for M5 via `/speckit-specify`; Slicer and
  One-shot stay as intent plus open questions until the roadmap admits them.
- Revisit item 2 when the post-M5 milestone is scoped.

## Evidence

None. This is a design decision; it is tested through the specs and host tests that
follow.
