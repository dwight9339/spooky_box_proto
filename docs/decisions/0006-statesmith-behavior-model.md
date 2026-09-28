# 0006. StateSmith PlantUML diagrams are the behavior model

- **Status:** Accepted 2026-09-28
- **Date:** 2026-09-28
- **Supersedes:** none
- **Beads:** `full_spooky_proto-8lw.12` (tooling), `full_spooky_proto-54w.21`
  (InputResolution), `full_spooky_proto-8lw.13` (event queue), `full_spooky_proto-8lw.14`
  (Session shadow)

## Context

The [behavior model](../design/behavior/README.md) specifies the M7 application as
hand-written Markdown tables: states, transitions, guards, invariants, open behavior,
maturity, deviations and retired IDs for each region, with Mermaid diagrams that only
illustrate the tables. No firmware is generated from it. Each region would therefore
exist twice, once as tables and once as hand-written C, and the two would have to be
kept in step by hand. Maturity and deviation tables add a third copy of facts that
already live in evidence files and Beads.

The user wants to keep the number of hand-synced documents as small as possible and
directed on 2026-09-28 that the behavior model move to StateSmith, keeping only what
still serves that goal.

A spike on 2026-09-28 wrote the InputResolution region (decision 0005) as a StateSmith
PlantUML diagram in a scratch workspace outside the repository. It found:

- StateSmith 0.22.2 generates plain C99 with no heap and no runtime library:
  `ctor`, `start`, `dispatch_event`, and state and event enums. Generation is
  deterministic; two runs produced identical files.
- Hierarchy removes duplication. The swallow, pre-held-release and cancel-on-release
  behavior is written once on a parent state; reconciliation is one transition from the
  outer state. An event a state does not handle falls through to its parent, and a
  transition ends the dispatch.
- Every non-transition behavior whose guard holds runs. Without explicitly exclusive
  guards, a pre-held release was both delivered and swallowed. A host scenario test
  caught the bug when it was reintroduced on purpose.
- A state must be declared inside its parent before any reference to it
  (StateSmith issue 460).
- StateSmith ignores PlantUML comments and `note` blocks, so notes can carry
  human-readable pointers without affecting generated code.
- Generated files can be written to another directory (`outputDirectory`, which must
  exist) and use a repository-style include guard (`IncludeGuardLabel`).
- The generated C compiles with `-std=c11 -Wall -Wextra -Werror -pedantic` except for
  `-Wunused-parameter`: some handlers ignore their `sm` argument. The firmware builds
  with `-Wall` only.
- The CLI targets .NET 7 and runs on .NET 8 with `DOTNET_ROLL_FORWARD=Major`. On Linux
  it printed nothing without a terminal; Windows behavior is untested.
- PlantUML renders the diagrams with the `smetana` layout and Java alone, without
  Graphviz.
- 11 host scenario tests against a fake port covered every InputResolution transition
  and passed. These are host results, not hardware evidence.

StateSmith has no orthogonal regions. The existing model already uses one machine per
region, so this costs nothing.

Principles involved: III (M7 authority, one owner), IV (bounded queues), V (proven,
target and open stay separate; host tests are not evidence), VI (generated-file policy,
incremental change), VII (tooling admission).

## Options

1. **Keep the Markdown tables and hand-write the C.** Rejected. Every region exists
   twice with nothing to keep the copies in step.
2. **Tables authoritative; StateSmith diagrams implement them, with a checker comparing
   IDs.** Rejected. Two authored copies remain; the checker only detects drift.
3. **Generate StateSmith diagrams from the tables.** Rejected. Actions and guards are
   prose, so the tables would need a strict format of their own: a custom language to
   maintain.
4. **StateSmith diagrams authoritative, with maturity, open behavior and deviations as
   structured annotations in PlantUML comments, and tables generated from them.**
   Rejected. It invents a metadata language and a document generator to preserve the
   table format.
5. **StateSmith diagrams authoritative. Every other kind of fact moves to the place
   that already owns it, and the table format is retired.** Chosen.

## Decision

**Source of truth**

1. Each region of the M7 behavior model is one StateSmith PlantUML diagram,
   `docs/design/behavior/<Region>Sm.puml`. The diagram is authoritative for states,
   hierarchy, events, guards, actions and transitions, and firmware is generated from it.
2. Generated C goes to `CM7/App/sm/`, next to each machine's hand-written port
   (`<region>_port.h` and `.c`). Generated files are committed and never edited by hand.
3. Diagram actions and guards call only functions declared in the machine's port
   header. No HAL calls, service internals or globals appear in a diagram.

**Where other facts live**

4. Proven behavior is recorded in `docs/evidence/`. Diagrams claim nothing about proof.
5. Open behavior is a Beads issue. A diagram may point to it with a `note` naming the
   issue; the note is for readers only.
6. Target behavior that differs from the firmware, such as lifting a Principle VI
   recording guard, lives in its decision record and Beads task. The diagram changes in
   the change that implements it. A safety guard still in force is drawn as a guard,
   with a note citing its record.
7. Invariants are host-test assertions. Invariants that span regions, and the rules in
   this record, are stated once in `docs/design/behavior/README.md`.
8. Row IDs, maturity tables, deviation tables and retired-ID tables are retired. Tests
   are named for the behavior they check.
9. `presentation.md` stays hand-written as the contract from domain events to
   surfaces. Its event names match the diagrams' published events.

**Diagram rules**

10. Behaviors of one state that share a trigger have mutually exclusive guards.
11. A state is declared inside its parent before it is referenced; initial transitions
    of a composite state are written inside it.
12. A behavior that must not run exit and entry actions is written as a behavior of the
    state (`State : EVENT / action`), not as a self-transition.
13. Machines never dispatch into one another synchronously. Commands and cross-region
    events pass through a bounded M7 event queue with declared capacity, overrun policy
    and counters. The queue and its dispatch order are a separate design task.
    Cross-region guards such as `in(Session.Active)` read another machine's state
    through the port.

**Tooling**

14. The StateSmith CLI version is pinned in `.config/dotnet-tools.json`. One script
    regenerates every machine and, with a check option, fails if the committed
    generated files differ from a fresh generation. The CMake build never runs StateSmith.
15. Host tests compile generated files with unused-parameter warnings suppressed for
    those files only.
16. Rendered diagrams are committed as SVG and checked for drift in the same way,
    rendered with a pinned PlantUML and the `smetana` layout, so Java is the only
    rendering dependency.

**Migration**

17. Regions move one at a time, in this order: InputResolution, Session, Radio,
    Context. Each region's Markdown file is deleted in the change that lands its
    diagram, so the two models never coexist for one region. Links in accepted records
    are updated mechanically, as the constitution allows.
18. Session runs in shadow alongside the existing recorder before it takes authority.
    Radio moves together with the non-blocking radio work in `full_spooky_proto-54w.6`.
    Context moves once its open behavior is settled.

## Consequences

- One authored artifact per region. Documentation that describes the code is generated
  from the same source, and the drift check makes stale generated files fail.
- Proven, target and open are separated by where they live: evidence files, decision
  records and Beads, and notes pointing into Beads. The diagram describes implemented
  behavior. The user should confirm that this satisfies the design-document rule to
  separate proven, target and open.
- Nothing records per transition what is proven until bench scenario traces exist.
  Readers consult the evidence files instead.
- The model README is rewritten around these rules, and `repository-layout.md` gains
  rows for `.puml` sources and the generated C and SVG.
- New developer dependencies: the .NET SDK for regeneration and Java for rendering. The
  firmware build and the host test build do not need either, because generated files
  are committed. The constitution's Platform section is unaffected.
- Adopting the tooling is inline tooling for the InputResolution task under roadmap
  tooling rule 6, not a separate tooling-track admission.
- Revisit if StateSmith stops being maintained, if a region needs orthogonal regions
  inside one machine, or if generated code cannot meet a measured timing budget.

## Evidence

Spike of 2026-09-28 in a scratch workspace, not preserved in the repository:
InputResolution diagram, generated C, and 11 passing host scenario tests built with the
repository's host-test flags plus `-Wno-unused-parameter`. Host results only, with no
hardware evidence. The first implementing task reproduces them in the repository.
