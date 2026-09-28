# Behavior model

This folder is the authoritative behavior model of the M7 application: what the device
does in each state, which commands and service events it accepts, and which facts it
publishes. It refines the [modes and interaction](../../../spec/product/modes-and-interaction.md)
product intent and the [application model](../architecture.md#application-model). If
this model conflicts with product intent or the constitution, the higher source wins
and the conflict is reported, not resolved here.

The model is a hierarchical statechart. States nest, transitions may form cycles, and
independent concerns run as parallel regions. The [top-level chart](top-level.md)
shows the whole device; each region with enough behavior gets its own file.

## Files

| File | Contents |
|---|---|
| [top-level.md](top-level.md) | Device chart, parallel regions, context transitions, cross-region invariants |
| [session.md](session.md) | Session region: recording lifecycle |
| [radio.md](radio.md) | Radio region: tuning, band transitions and radio faults |
| [input-resolution.md](input-resolution.md) | InputResolution region: the Button 0 session hold and prompt |
| [presentation.md](presentation.md) | How domain events and published state reach the user |

## What gets a state machine

Not everything with a value is a state machine. A region or state exists only where
being in it changes which transitions are legal or how a command or input is
interpreted. Session is a region because a tune command is rejected while recording.

Values that do not change legality are **model data**: published state that commands
read and write, but that owns no transitions. Examples: band, frequency, scan position,
scan rate, EMF level, radio activity, selected parameter page, battery level. A value
can have a machine around the process that changes it while staying data itself: the
band is data, and a band transition in progress may be a state.

## Vocabulary

| Term | Meaning | Example |
|---|---|---|
| Input event | Behavior-neutral report from the input subsystem: press, release, encoder detents, with sequence and time | Encoder 0 +2 detents |
| Command | A request for a product-level operation, from gesture resolution or the CLI | StartSession |
| Service event | A timer, subsystem completion, failure or other occurrence that may drive a machine | BlockWritten, CaptureFault |
| State | A condition that remains true for some duration and affects behavior | Recording |
| Guard | A condition that must hold for a transition to occur | card_present |
| Invariant | A condition that must remain true while a state is active | Tune commands do not execute while recording |
| Action | Authoritative work performed because of a transition | Open session file |
| Domain event | A one-shot fact published by the authoritative model, including rejections and failures | RecordingStarted, RecordingRejected |
| Presentation | How published state and domain events are expressed to the user | Record LED, OLED text, audio cue |

The flow is always the same. Input events pass through gesture resolution and become
commands; the CLI produces commands directly, through the same command handling. A
machine receives commands and service events, checks guards, performs actions, changes
state and publishes domain events. Presentation renders published state and domain
events and never feeds back into the machine.

## Notation

- **Stable IDs.** Every state, invariant and transition has an ID with its machine's
  prefix: `SES-S1` for a state, `SES-I1` for an invariant, `SES-01` for a transition.
  IDs are never reused or renumbered. When a row's meaning changes, it gets a new ID
  and the old ID moves to the file's Retired IDs table. Host tests, code comments, bug reports and
  evidence cite them, for example a test named `test_SES_07_stop_requests_finalize`.
- **Tables are authoritative.** Diagrams illustrate the tables and must not show a
  transition, state or trigger that the tables lack.
- **Diagrams render in Mermaid 11.** VS Code's preview uses Mermaid 11, which fails
  on a composite state nested inside a parallel region. Draw such a state as a simple
  state with a description, and keep its substates in the tables.
- **One row per outcome.** Rows with the same source state and trigger have mutually
  exclusive guards, so exactly one row applies. When a synchronous action can fail,
  each result gets its own row, and the guard names the result. The guard table says
  whether a guard is evaluated before the actions or after a named action.
- **Commands are answered.** Every command a region can receive has a row, or an
  Open behavior entry, in every state of that region. Rejections are rows too. A
  service event without a row in the current state is ignored.
- **Cross-region conditions** are written `in(Region.State)`, for example
  `in(Session.Active)`.
- **Composite states.** A transition from a composite state applies in every state
  nested inside it.
- **Returning to a previous context** is specified as observable behavior: the entering
  transition saves the context and the leaving transition restores it. Diagrams may
  annotate this as deep history to explain the semantics. The model does not require
  the firmware to implement a history pseudostate or any other particular technique.

## Open behavior and maturity

A semantic table holds only behavior that is settled. A Target row must trace to the
constitution, product intent, an accepted decision record, an explicit user direction
recorded in a Beads issue, or existing firmware behavior. Anything else is open: it goes
in the machine's **Open behavior** table as a question, with the Beads issue or product
open decision that settles it, and never as a guessed row. Settling it moves the answer
into the semantic tables.

Maturity describes confidence in a row, not machine behavior. Each machine file keeps
it in a separate **Maturity** table keyed by ID, never in the semantic tables.

| Status | Meaning | Basis column |
|---|---|---|
| Proven | Bench evidence shows the behavior on hardware | Link to the evidence file |
| Target | Settled behavior with no qualifying evidence yet, whether or not code exists | Its source, and the state of the implementation |

Host tests are not hardware evidence, so a host-tested row stays Target until a bench
run proves it. A **deviation** is a place where current firmware behaves differently
from the model. Each machine file lists its deviations with the Beads bug that tracks
them.
