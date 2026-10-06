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

4. An engine owns its playback or synthesis semantics, the meaning of its sequencer
   steps, and which of its parameters macros and modulation may target.
   *(user 2026-10-05; Effects removed by item 14, user 2026-10-06)*
5. Shared infrastructure: clip storage and access, the transport and clock, BPM and
   timing primitives, swing math, pattern and preset persistence, parameter IDs, the
   slice map type ([0022](0022-slicer-engine.md)), modulation sources (item 13) and the
   effects chain (item 14). *(user 2026-10-05, 2026-10-06)*
6. Every engine keeps these gestures identical: the engine selector (C-018 to C-020),
   the view selector (C-021 to C-023), page advance (C-024), Shift (C-026), the utility
   root (C-027), the mode switch (C-028), the session prompt (C-091), the returns to the
   view selector and to the engine (C-083, C-084), step browse and edit (C-072 to
   C-077) and transport run/stop (item 7). An engine may differ only in its parameter
   assignments (C-025), its Button 1 action (C-029) and the meaning of a step's value.
   *(agent)*

**Sequencer shell**

7. One Instrument-wide transport keeps running across engine and view switches. Tempo
   and swing are transport settings, so switching engines does not change the groove.
   An Encoder 0 click on the main engine page runs or stops it, echoing the Classic
   scan's run and pause (C-103). *(user 2026-10-05, S1; swing user 2026-10-06; the
   control user 2026-10-06)*
8. Every engine's sequencer is a row of steps on the OLED. Each step has an on/off flag
   and one primary value, edited with C-072 to C-077. The engine defines the value's
   meaning and range: a note for One-shot, a slice for Slicer, a position for Granular.
   Per-step extras (ratchet, reverse, probability and so on) belong to the engine.
   *(user 2026-10-05, S2 as revised)*
9. While a sequence drives a parameter, the live knob for it offsets the sequence rather
   than overriding it. *(user 2026-10-05, S3)*
10. A pattern belongs to its engine and survives engine switches. Loading a new clip keeps
    the pattern. Division, length and scale are pattern settings.
    *(user 2026-10-05, S4; pattern settings user 2026-10-06)*
11. Every engine's sequencer is also a stepped modulation source, routable like any other
    source (item 13). A standard pattern setting, on by default, sends the step value to
    the engine's primary parameter; turned off, the sequencer acts purely as a
    modulation source. Per-step parameter locks are deferred. *(user 2026-10-06)*
12. With that routing off, step on/off still triggers engines that have triggers. Each
    engine's own record defines what it plays then: Granular follows its position knob,
    Slicer replays the slice selected on its page, One-shot plays its root note.
    *(agent)*

**Macros, modulation and effects**

13. Modulation sources are shared: encoders, macros, LFOs, envelopes, sequencer lanes,
    EMF level and radio activity. Targets are the active engine's parameters and the
    effect parameters. Each engine has its own macro slots and modulation routes, saved
    in its preset as the control map's preset scope already lists. Slot and route counts
    stay open (modes and interaction, open decision 10). *(user 2026-10-06)*
14. One effects chain follows whichever engine runs and is kept across engine switches.
    Engine presets do not include it. *(user 2026-10-06)*

**Display**

15. All parameter and sequence editing happens on the OLED. The matrix gives glanceable
    feedback (activity, motion, intensity, identity) and never shows editing detail.
    *(user 2026-10-05)*
16. The active engine owns the matrix body. Two layers are shared by every engine: the
    recording ring whenever a session records, and EMF level while EMF is routed as a
    modulation source. EMF is measured against the fixed Instrument baseline and drawn
    as the outline buckets of [0019](0019-matrix-emf-four-buckets-and-quiet-baseline.md),
    so it keeps its Field meaning (Principle II). *(user 2026-10-06)*

**Recording**

17. An Instrument session's authoritative record is the rendered stereo mix. Engine
    parameters and sequencer events are stored alongside it as metadata, as
    [modes and interaction](../../spec/product/modes-and-interaction.md) (Instrument
    Sessions) already states. M5 replay plays the audio; re-rendering a performance
    through an engine is later work. *(user 2026-10-06)*

## Open questions

- Item 7 assigns transport run/stop only on the main engine page. In every performance
  view Encoder 0 click is already taken (browse and select), yet the Sequencer must keep
  "immediate access to start, stop" (modes and interaction, Sequencer). Which control
  runs or stops the transport inside the Sequencer view?
- Confirm the list in item 6.
- Confirm the routing-off behavior in item 12, or leave it entirely to each engine's
  record.

## Product document changes on acceptance

These are proposals to protected documents and need separate approval.

- [Modes and interaction](../../spec/product/modes-and-interaction.md), Mode Hierarchy:
  replace `Sampler and Slice concepts` with two entries, `Slicer` and `One-shot`.
- Same document, Instrument Mode: add a short section per engine stating its intent
  (the three lines in Context), and a paragraph stating items 4 to 6.
- Same document, Effects, Modulation, Macros and Sequencer: state items 11, 13 and 14,
  and that each engine defines the meaning of a step's value and that editing happens on
  the OLED (items 8 and 15).
- Same document, Display and LED Matrix Roles: state item 16.
- Same document, Open Decisions 7: resolved by this record (separate engines).
- [Control map](../../spec/product/control-map.md) C-072 to C-076: generalize "note bar"
  and "note value" to "step" and "step value (engine-defined)", keeping "within current
  scale" for pitched engines.
- Control map: a new row for item 7 (Instrument, main engine page, Normal, Encoder 0
  button, Click, run or stop the transport), and a row for the Sequencer-view control
  once chosen.
- Control map C-080: the setting list becomes pattern settings (division, length, scale,
  primary routing) and transport settings (tempo, swing).
- Control map preset scope: the Effects chain row becomes Exclude. D-008 is resolved by
  item 14; whether separate FX presets exist is left to the preset work.
- [Roadmap](../../spec/product/roadmap.md), After M5: add "Slicer and One-shot engines"
  beside "Seek and Orbit engines".

## Consequences

- The demo's slice quantization in Granular is not product behavior; the portable
  granular core carries no slice parameter.
- Spec work follows: a Granular feature spec for M5 via `/speckit-specify`; Slicer and
  One-shot stay as intent plus open questions until the roadmap admits them.
- M5's minimal effects chain is built as the shared chain of item 14.
- Deferred to a One-shot record, written when One-shot is admitted: the MIDI input path
  (USB belongs to the M7, so USB MIDI means a composite device beside the CDC port;
  `docs/design/usb-cli.md` already names it as a later option), polyphony and voice
  stealing, and scale handling.
- Deferred to each engine's record: microphone injection (C-029), which stays in the
  P4 backlog until after M5 (roadmap, After M5).
- Revisit item 2 when the post-M5 milestone is scoped.

## Evidence

None. This is a design decision; it is tested through the specs and host tests that
follow.
