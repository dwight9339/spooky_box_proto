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
