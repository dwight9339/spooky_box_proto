# Decision records

Each file records one design decision, following the Documentation Classes rules in the
[constitution](../../spec/.specify/memory/constitution.md):

- Agents may draft a record with status `Proposed`. Only the user accepts it.
- Once accepted, a record is immutable apart from its status line.
- A changed decision gets a new record that supersedes the old one. The old record's
  status changes to `Superseded by NNNN`.

| No. | Decision | Status |
|---|---|---|
| [0001](0001-initial-ui-and-bus-ownership.md) | Initial UI and bus ownership | Accepted |
| [0002](0002-windows-first-spooky-bench.md) | Windows-first Spooky Bench host | Accepted |
| [0003](0003-radio-control-during-recording.md) | Radio control stays available while recording | Accepted |
| [0004](0004-radio-track-continuity-across-transitions.md) | Radio-track continuity across receiver transitions | Accepted |
| [0005](0005-button-0-session-prompt.md) | Button 0 session prompt and standalone button lighting | Accepted |
| [0006](0006-statesmith-behavior-model.md) | StateSmith PlantUML diagrams are the behavior model | Accepted |
| [0007](0007-m7-event-queue.md) | One bounded M7 event queue with run-to-completion dispatch | Accepted |
| [0008](0008-recording-safe-command-policy.md) | Recording-safe command policy | Accepted |
| [0009](0009-first-slice-field-controls.md) | First-slice Field controls and gesture resolution | Accepted |
| [0010](0010-sd-backed-rolling-capture.md) | Sixty-second SD-backed rolling capture | Accepted |
| [0011](0011-halloween-2026-demo-build.md) | Halloween 2026 demo build | Accepted |
| [0012](0012-common-audio-sample-timeline.md) | Common audio sample timeline | Accepted |
| [0013](0013-matrix-emf-radio-and-status-mapping.md) | LED matrix mapping for EMF, radio onsets and recording status | Accepted |
| [0014](0014-sd-media-policy.md) | SD media policy for recording | Accepted |
| [0015](0015-raw-radio-track-during-in-band-tunes.md) | Raw radio track during in-band tunes | Accepted |
| [0016](0016-classic-scan-motion.md) | Classic scan motion, band edges and activity hold | Accepted |
| [0017](0017-microphone-start-latency-uncompensated.md) | Microphone start latency stays uncompensated | Accepted |
| [0018](0018-classic-am-lw-maximum-rate.md) | Classic maximum rate on AM and LW | Accepted |
| [0019](0019-matrix-emf-four-buckets-and-quiet-baseline.md) | Four matrix EMF buckets, a quiet-field baseline and no trail by default | Accepted |
| [0020](0020-halloween-demo-slicer-and-sequencers.md) | Halloween demo Instrument scope: Slicer, sequencers and a tempo matrix | Accepted |
| [0021](0021-instrument-engines-and-performance-views.md) | Instrument engines own their performance-view semantics | Accepted |
| [0022](0022-slicer-engine.md) | Slicer engine: slice map, slice page and sequencer | Accepted |
| [0023](0023-clip-selection-and-region-editing.md) | Clip selection, region editing and the empty Instrument | Accepted |
| [0024](0024-clip-sources-and-track-mixing.md) | Clip sources, track mixing and maximum clip length | Accepted |
| [0025](0025-demo-clip-in-d2-sram.md) | Demo clip in D2 SRAM, up to 5 s | Accepted |
| [0026](0026-transport-tempo-and-fit-on-load.md) | Transport tempo and fit on load | Accepted |
| [0027](0027-instrument-engine-switching.md) | Instrument engine switching | Accepted |
| [0028](0028-monitor-only-ptt-without-mic-monitoring.md) | Monitor-only PTT without microphone monitoring | Accepted |

## Template

Name new records `NNNN-short-title.md`, using the next number.

```markdown
# NNNN. Title

- **Status:** Proposed | Accepted | Superseded by NNNN
- **Date:** YYYY-MM-DD
- **Supersedes:** none | NNNN
- **Beads:** issue ID(s)

## Context
What forces the decision; constraints and the principles involved.

## Options
The alternatives considered and why each was rejected.

## Decision
What was decided, stated so it can be checked.

## Consequences
What this enables, forbids or defers, and the conditions for revisiting it.

## Evidence
Links to evidence files, tests or bench results.
```
