# Spooky Box Prototype Constitution

## Core Principles

### I. Recording Is a Promise

When the device shows that it is recording or capturing, preserving that audio and its
synchronized data has priority over everything else in the system.

- Audio capture and SD writes MUST have priority over display, animation, logging and
  other nonessential work. When a visual workload is under load, it MUST slow down or
  simplify before it affects capture.
- Navigation, utility screens and editor views MUST NOT silently stop, replace or
  invalidate an active recording or rolling capture. Actions that would interrupt one MUST
  be classified as allowed, deferred or rejected, and they MUST require explicit
  confirmation or be unavailable while the recording is active.
- A missing, full, removed or failing card and a failed recording MUST cause a visible
  fault. The device MUST NOT show a failed recording as a successful session. Where
  possible, it MUST finalize the valid partial data.
- Raw source tracks MUST be preserved non-destructively. Monitoring behavior, such as PTT
  muting the radio in the live mix, changes only the monitored mix, never the stored raw
  capture.

**Rationale:** The instrument's value depends on events it has already caught. A dropped
recording cannot be recovered; a dropped animation frame costs almost nothing.

### II. Honest, Stable Meaning

Core signals MUST keep the same meaning in every mode, view and presentation. The device
MUST NOT fabricate meaning its measurements do not support.

- EMF level means disturbance relative to a calibrated or explicitly chosen baseline.
  Radio activity means measured energy or change in the incoming radio audio. Scan
  position means the engine's position within its territory. Session state means idle,
  armed, recording, finalizing or reviewing. Presentation MAY change how these look; it
  MUST NOT redefine them.
- The device MUST NOT claim a cause for a sensor reading, paranormal or otherwise, that
  it cannot measure. An evocative presentation is fine; a fabricated interpretation is not.
- Feedback MUST show uncertainty, staleness and failure honestly. The UI MUST NOT infer
  that an operation succeeded only because a command was sent. Consequential transitions
  (band change, calibration, recording start or stop, capture, fault) MUST have explicit
  outcomes, and audio, lights and display MUST acknowledge them consistently with one
  another.
- Sensor policy MUST change explicitly between environmental observation (adaptive
  baseline in Field) and intentional control (fixed baseline in Instrument). It MUST NOT
  change silently.

**Rationale:** A user can build intuition only if what the device shows is truthful and
means the same thing in every mode.

### III. Single Authority, Explicit Ownership

The Cortex-M7 is the source of truth for product state. Every hardware resource has
exactly one owner.

- M7 MUST own the operating mode, engine and view state, session and recording state,
  radio control, audio, storage transactions, sensor interpretation and gesture
  resolution. Other components publish or render semantic state; they MUST NOT hold a
  competing copy of that state.
- A peripheral and its pins MUST have exactly one owning core in any image pair.
  Ownership MUST change only through a build-time transfer backed by a documented
  decision and a qualifying test, never through a runtime handoff. A shared bus
  controller (for example I2C2) MUST move as a whole domain or stay where it is.
- Cross-core communication MUST use the versioned, sized and sequenced IPC contract with
  explicit cache and barrier policy, bounded queues, defined overrun behavior and
  detectable staleness. Arbitrary shared mutable application objects and shared driver
  buffers MUST NOT be used.
- M4 input reporting MUST stay behavior-neutral: debounced press and release transitions
  and signed encoder detents, with sequence, time and held-state information. Restart,
  overflow or staleness MUST trigger release-all reconciliation so that no control (for
  example Shift or PTT) can stay latched.
- The CLI and the physical controls MUST use the same command handling and safety policy.

**Rationale:** Two owners of one bus, or two cores that each think they know the mode,
cause failures that are hard to reproduce. They are prevented by design, not debugged
later.

### IV. Bounded, Observable Real-Time Behavior

All time-critical work MUST have bounded duration, and its limits MUST be measurable.

- DMA and ISR work MUST be bounded. Filesystem operations MUST NOT run in audio callbacks.
  Logging producers on the recording path MUST NOT block.
- Every queue MUST have a declared capacity and an explicit overrun policy. Queue
  capacities, overrun counters and the longest SD-write latency MUST be observable in
  diagnostics.
- Timing budgets (buffer duration, UI frame rate, chord windows, rolling-capture length)
  MUST be derived from measurement or explicit arithmetic and recorded, not assumed.
  Timings that are not yet settled MUST remain configurable until bench trials fix them.
- Blocking waits in the foreground loop MUST follow a recording-aware policy before they
  can coexist with capture.

**Rationale:** The current cooperative loop proves that the workload is possible, not that
it is safe. A guarantee needs bounded behavior and counters that show whether the bounds
hold.

### V. Evidence Before Claims

A capability counts as delivered only when evidence at the appropriate level shows it
working.

- Documentation and specs MUST separate what is proven today from the target direction
  and from open decisions. They MUST NOT describe a target feature as an existing
  capability.
- Portable logic (protocols, state machines, gesture resolution, buffers, host tooling)
  MUST have host tests that run without hardware. Host tests MUST NOT be cited as evidence
  of SRAM visibility, cache or MPU behavior, HSEM operation, boot timing or recording
  reliability. Those require a bench procedure.
- Hardware acceptance MUST produce preserved, dated evidence (bench results, Spooky Bench
  JSON artifacts, inspected WAVs). Test code MUST NOT be weakened to make a failing test
  pass.
- When a result is negative, partial or skipped, it MUST be reported as such.

**Rationale:** On embedded hardware, "it compiled" and "it worked once" are not the same
thing, and neither is qualification.

### VI. Protect the Working Baseline

The known-good recorder and boot behavior are a regression reference and MUST keep
working while the code changes around them.

- Refactor incrementally, one service at a time. Preserve peripheral setup, callback
  cadence and buffer placement during each extraction. The tree MUST NOT be renamed or
  moved wholesale.
- After each timing-sensitive change, the Debug and Release dual-core builds MUST pass and
  the relevant bench regression MUST run again before more timing-sensitive work is
  stacked on top.
- Services MUST expose narrow init, service and status interfaces. They MUST NOT export
  private globals or pass one large context object.
- Generated files MUST follow `docs/repository-layout.md`. The `.ioc` file is not
  authoritative until its documented discrepancies are reconciled. CubeMX regeneration
  requires a clean tree and a full diff review, and proven runtime overrides MUST be
  restored.
- An existing safety guard (for example, rejecting band changes during recording) MUST
  stay in place until a bench test proves the guarded workload is safe.

**Rationale:** A working three-track recorder took effort to prove. Every change that
cannot be traced back to it makes regressions harder to find.

### VII. Instrument First, Narrow Slices

Spooky Box is a field instrument, not a general-purpose workstation. Delivery proceeds in
narrow vertical slices that deepen the instrument.

- Controls MUST manipulate understandable quantities (position, range, rate, sensitivity,
  mode, recording state) and SHOULD map physical axes to consistent conceptual axes. The
  common actions MUST be available on the performance surface, not buried in menus.
- Field and Instrument remain the two operating modes. Settings and Playback are global
  utility spaces. Engines are distinct models of motion or sound generation, not presets.
  Views operate around the active engine. Returning from a view, a utility or Manual MUST
  restore the previous context.
- The application MUST publish semantic facts and events, not pixel instructions, so that
  product behavior survives changes to display, driver, board or core ownership.
- Work MUST follow the milestone sequence in `spec/product/roadmap.md`. A feature MUST
  NOT be added unless it deepens the instrument.
- Supporting tooling (Spooky Bench, Spookyprobe integration, host utilities) exists to
  shorten the product development cycle. That means producing trustworthy evidence and
  reducing the time product work waits on repetitive manual hardware validation. A
  tooling task MAY start only in three cases: a product task depends on it; it removes a
  recurring manual validation step from a current or next milestone gate; or a tooling
  defect blocks product evidence. Among admitted tooling, work that removes the most
  human validation time comes first. At most one tooling task is in progress at a time,
  and a tooling task's priority MUST NOT exceed that of the product work it serves. An
  automated check replaces a manual one only when it is at least as trustworthy, and
  judgment checks stay with a person.
- A proposed or open product decision (for example from the control-map workbook) MUST
  NOT be silently promoted to a requirement. It is settled through a short decision record
  and a test.

**Rationale:** Building every screen first would bury unresolved audio, memory and
inter-core questions under UI code. Narrow slices bring those questions to the surface
while they are still cheap to answer.

## Platform & Repository Constraints

- **Target:** NUCLEO-H755ZI-Q (STM32H755, dual-core Cortex-M7 and M4) with the RF and
  backplane hardware, the Zio audio shield and the UI board, as documented in `BRINGUP.md`
  and `reference/`. The prototype is not the complete product firmware.
- **Firmware toolchain:** ARM GNU toolchain, CMake and Ninja through the root presets
  (`Debug`, `Release`, and the opt-in experiment presets such as `IpcSmoke`). The root
  maintained dual-core orchestration is the canonical build. PlatformIO is a deployment
  wrapper only.
- **Firmware language:** C with fixed-width, C-compatible types for anything shared
  between cores. Layout invariants MUST be enforced with static and linker assertions.
- **Host tooling:** native C11 tests under `tests/` and the Python Spooky Bench under
  `host/`, with offline tests. Host tests MUST NOT depend on the firmware cross toolchain.
- **Opt-in experiments:** diagnostic or experimental workloads MUST be opt-in build
  variants. Normal Debug/Release behavior stays unchanged until the experiment passes its
  qualification gates.
- **Source hierarchy:** where documents conflict, the classes defined in
  Documentation Classes take precedence in the Governance authority order. Frozen
  history (`docs/history/`, `reference/`) never overrides a maintained document.

## Development Workflow & Quality Gates

- **Task tracking:** Beads (`bd`) is the only source of truth for executable tasks, status
  and dependencies. Markdown TODO lists and parallel trackers MUST NOT be created.
- **Spec Kit scope:** Spec Kit is used only for this constitution (`/speckit-constitution`)
  and for feature specifications (`/speckit-specify`, `/speckit-clarify`). Planning, task
  breakdown, implementation tracking and issue export belong to Beads and the documentation
  classes:
  - design goes into `docs/design/` and decision records;
  - work items go into Beads tasks under the relevant milestone epic;
  - status is recorded only in Beads.

  Agents MUST NOT create `plan.md` or `tasks.md` files or export tasks to another tracker.
  The other Spec Kit commands are removed from the project, and reinstating one requires an
  approved amendment. Where a kept command suggests `/speckit-plan` as its next step, the
  next step is instead the spec-to-work handoff below.
- **From spec to work:** once a spec is ratified, an agent proposes the Beads tasks that
  implement it. Each task cites the spec path and the requirement IDs it satisfies, carries
  a milestone label, and has acceptance criteria traceable to the spec's success criteria.
  Design questions the spec leaves open become decision records or design-document updates,
  not a plan file. The user approves the task set before work starts.
- **Handoff:** agents MUST keep claimed-issue notes current with `bd update <id>
  --append-notes`. The notes cover what is done, what was validated, what remains, which
  files belong to the task, and which unrelated uncommitted changes exist.
- **Quality gates for code changes:** the affected dual-core presets build; host tests
  pass; bench regression runs when timing-, ownership- or hardware-sensitive behavior
  changes; and results, including failures, are reported.
- **Constitution check:** every Beads task is checked against Principles I–VII when work
  starts and again at handoff. A task that departs from a principle MUST name the
  principle, justify the exception and record the simpler alternative that was rejected,
  in the task notes or its decision record.
- **Decisions:** open design decisions (IPC queues and restart, session format, recovery,
  scan algorithms, timing budgets, gestures, ownership transfers) are settled through a
  decision record in `docs/decisions/` and a test. They MUST NOT be embedded silently in
  drivers or design documents.
- **Git:** agents follow the conservative profile. They MUST NOT commit, push or run Dolt
  remote sync unless the user explicitly asks.

## Documentation Classes

Every maintained document belongs to exactly one class. The class decides where the
document lives and when it may change. A document that mixes classes MUST be split.

| Class | Location | Contents | Change rule |
|---|---|---|---|
| Governance | `spec/.specify/memory/` | This constitution | Protected; approved proposal only |
| Product intent | `spec/product/` | Product philosophy, modes and interaction, control map, development roadmap | Protected. Resolving an open decision requires an approved proposal backed by a decision record. |
| Feature specs | `spec/specs/NNN-*/` | Spec Kit `spec.md` and its `checklists/` | `spec.md` is protected once ratified. Checklists are working files maintained by `/speckit-specify` and `/speckit-clarify`. No `plan.md` or `tasks.md` (see Spec Kit scope). |
| Decision records | `docs/decisions/NNNN-*.md` | One decision each: context, options, decision, consequences, evidence | Agents MAY draft a record with status `Proposed`. Only the user accepts one. Accepted records are immutable apart from their status line; a new record supersedes an old one. |
| Design & contracts | `docs/design/` | Architecture, interfaces, wire formats, file ownership | Living. MUST be updated in the same change as the code it describes and MUST separate proven, target and open. |
| Procedures | `docs/procedures/` and `BRINGUP.md` | Setup, bench and acceptance procedures | Living. MUST match current tooling and state prerequisites and pass criteria. |
| Evidence | `docs/evidence/` | Dated bench results and regression records | Immutable once the session it records ends. Corrections are dated addenda. |
| History | `docs/history/` | Superseded reviews and plans, frozen as dated snapshots | Frozen. Any rationale that is still in force is extracted into a decision record. |
| Legacy reference | `reference/` | Schematics and pre-STM32 design documents | Frozen apart from adding new reference material |

Rules for all classes:

- **Status lives only in Beads.** Documents link to Beads issue IDs and evidence files.
  They MUST NOT carry running status logs, checklists of pending work or "now done"
  narration.
- **Evidence is cited, not restated.** Design documents and procedures link to evidence
  files instead of copying results into their own text.
- **Mechanical link updates are allowed.** When an approved move or rename breaks a
  link, the link MAY be corrected in any class, including frozen and immutable
  documents, without changing any other content.
- **Naming:** evidence and history files use `YYYY-MM-DD-topic.md`. Decision records
  use a four-digit sequence number. Other files use lowercase-hyphenated names.
- **New documents:** each new document MUST be placed in a class folder. If it fits no
  class, amend this table by proposal before creating it.
- **Ratifying a spec:** a feature `spec.md` is ratified when the user approves it and
  its header records `Status: Ratified` with the date.

## Governance

**Authority.** This constitution is the highest-ranking guidance for the project. The
order of precedence is:

1. Explicit instructions from the user.
2. This constitution.
3. Product intent and ratified feature specs under `spec/`.
4. Accepted decision records, then design documents and procedures in `docs/`.
5. Beads task text.
6. Evidence, history and legacy references, as records of the past.

When a lower-ranked source conflicts with a higher one, the agent MUST report the conflict
instead of resolving it silently.

**Protected documents.** The following documents are authoritative:

- `spec/.specify/memory/constitution.md`
- Everything under `spec/product/`
- Ratified feature specifications under `spec/specs/`
- Accepted decision records under `docs/decisions/`

Agents MUST NOT create, edit, reformat or delete a protected document on their own
initiative. This applies even to fixing typos, updating status or making changes that
seem obviously correct. An edit is authorized only in one of two cases:

- the user explicitly approves a specific proposal, or
- the user invokes a Spec Kit command whose defined purpose is to amend that document
  (for example `/speckit-constitution`, or `/speckit-clarify` on a spec). The edit stays
  within that command's scope.

**Proposal procedure.** To change a protected document, an agent MUST present a proposal
and then stop until the user approves it. The proposal contains:

- the target document and section;
- the exact proposed text or diff;
- the motivation, including evidence or the conflict that prompted it;
- the principles affected;
- for this constitution, the proposed version bump; and
- the effect on dependent specs, plans, templates and agent instruction files.

The proposal MAY be recorded as a Beads issue so that it is durable. After approval, the
agent applies exactly the approved change and records it in the Sync Impact Report or the
issue notes.

**Versioning.** This constitution uses semantic versioning:

- MAJOR: a principle or governance rule is removed or redefined in a backward-incompatible
  way.
- MINOR: a principle or section is added, or guidance is materially expanded.
- PATCH: clarification or wording only.

Every amendment updates **Last Amended**. The Sync Impact Report at the top of this file
is review scratch material and is removed before the amendment is committed.

**Compliance.** Plans, reviews and handoffs MUST check changes against these principles.
Complexity or exceptions MUST be justified in writing. Agents that find a violation in
existing code or documents file a Beads issue instead of making an unapproved sweeping
fix. Day-to-day agent workflow guidance lives in `AGENTS.md` and `CLAUDE.md`. Those files
MUST NOT contradict this constitution.

**Version**: 1.3.0 | **Ratified**: 2026-09-25 | **Last Amended**: 2026-09-25
