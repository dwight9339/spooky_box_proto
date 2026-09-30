# Development roadmap

The order in which the prototype is built, what each milestone must prove before the
next begins, and how supporting tooling is kept in service of that order. This document
holds scope, exit criteria and policy only. Status lives in Beads
(`bd graph --all --open`); evidence lives in `docs/evidence/`. Changes follow the
constitution's proposal procedure.

## Tracks

**Product track (primary).** Everything that makes the instrument itself: firmware
services, interaction, capture, sessions, playback and Instrument features. Its
order is the milestone sequence below.

**Tooling track (supporting).** Spooky Bench, Spookyprobe integration and other host
utilities. The tooling's purpose is to shorten the product development cycle: it
produces trustworthy evidence, and it cuts the time the product track spends waiting
for a person to do repetitive hardware validation. The goal is not a device that no one
touches until the product is finished. People should spend bench time on judgment,
not on flashing, resetting, collecting logs or reading counters.

1. **Consumer rule.** A tooling task is admitted only when one of these holds:
   - a product task's acceptance depends on it (a blocking dependency),
   - it removes a recurring manual hardware-validation step from a current or next
     milestone gate, and it names that gate and the step it replaces, or
   - a tooling defect blocks product evidence.

   A tooling task that meets none of these stays in the backlog at P4.
2. **Ranking.** Among admitted tooling, work that removes the most human validation
   time from current and next milestones comes first.
3. **Work in progress.** At most one tooling task is in progress at a time.
4. **Priority ceiling.** A tooling task's priority never exceeds the priority of the
   product work it serves.
5. **Trust and judgment.** An automated check replaces a manual one only when it is at
   least as trustworthy (Principle V). Judgment checks stay with a person: listening,
   feel of controls, visual quality, and first electrical checks on new hardware.
6. **Inline tooling.** Tooling written inside a product task, limited to what that
   task's own acceptance needs (for example the offline `wav align` analysis for the
   sample-timeline task), is product work and needs no separate admission.
7. **First target.** Make the recording regression, which is rerun after every M2
   extraction and at every later milestone, a single unattended command built from
   the existing boot-smoke, recording-load, WAV-inspection and SD runners. Trace,
   Pi/Linux hosting and similar capabilities wait for a named consumer.

**Demo track (time-boxed).** A public demonstration may show product behavior before
its milestone exits. A demo track exists only while an accepted decision record
authorizes it, and it follows these rules:

1. **Authorization.** The decision record names the demo and its date, the script,
   the product tasks pulled forward, every demo-only behavior and its justification
   against the constitution, and an end date. The track ends on that date whether or
   not the demo shipped.
2. **Separation.** Pulled-forward product work lands on the default branch under the
   normal gates. Demo-only code lives on a dedicated demo branch, builds only in an
   opt-in `Demo` preset, and is never merged into the default branch.
3. **Product work stays product work.** A demo task never closes a product task. When
   it ships a narrower version, it appends a note to the product task naming what
   remains.
4. **Principles hold.** The demo image keeps capture priority, honest meaning, single
   ownership and bounded real-time behavior. A safety guard is lifted in the demo image
   only after a bench run on that image shows the guarded workload safe, recorded in
   `docs/evidence/`. Demo evidence qualifies the demo image only; it is not milestone
   evidence.
5. **Open decisions stay open.** Where the demo needs behavior the product has not
   decided, the decision record marks the choice provisional and demo-only. It does
   not settle the product decision.
6. **Bounded reordering.** Demo work may run ahead of the milestone sequence only
   within its authorized scope. It does not change milestone scope or exit criteria.
   Tasks outside that scope keep their order.

In Beads, tooling work is in epic `full_spooky_proto-5yv` and carries the label
`track-tooling`; product tasks carry a milestone label (`m1` … `m5`).
Each authorized demo has its own epic labelled `track-demo`, and its tasks link to the
product tasks they depend on or narrow.

## Milestones

M1 and M2 may proceed in parallel. M3 starts integration only after both exit. Each
later milestone requires the previous one.

### M1 — Baseline qualified

Prove the existing recording and dual-core baseline so that later changes have a
trustworthy regression reference.

- **Scope:** ten-minute recording with channel and power evidence; IPC stale recovery
  and reset-state matrix; normal-build charging sleep after the logger extraction; SD
  full, removal and retained-file recovery; logger loss under sustained recording load.
- **Exit:** each gate has image-linked, dated evidence; unsupported hardware cases are
  recorded explicitly; recording shows no overrun under logger saturation.
- **Epic:** `full_spooky_proto-jjy`.

### M2 — Service boundaries

Finish the firmware structure the product features will stand on.

- **Scope:** one command policy for CLI and physical actions; a single storage service
  owning the recorder and SD volume; measured and bounded foreground latency during
  recording; UI bring-up drivers split into input and rendering services; CubeMX
  overrides reconciled or generation formally frozen.
- **Exit:** Debug and Release build; the M1 recording regression (automated, see
  tooling rule 7) passes after each extraction; foreground latency and queue budgets are measured and recorded.
- **Epic:** `full_spooky_proto-8lw`.

### M3 — Field Classic/Manual slice

The first coherent instrument experience.

- **Scope:** host-tested navigation, gestures and independent capture state; versioned
  product state and input-event IPC; M4 inputs and semantic rendering under recording
  load; in-band tuning while recording; Classic territory scanning with reversible
  Manual; semantic EMF, activity and visual feedback; common audio sample timeline;
  microphone monitoring and monitor-only PTT.
- **Exit:** the integrated Classic/Manual recording acceptance: image-linked audio,
  event, queue and latency evidence; no navigation-induced stop or reset; no stuck
  Shift or PTT; no misleading save or record status; controls stay responsive under
  display and SD load.
- **Epic:** `full_spooky_proto-54w`, plus `full_spooky_proto-hpq.1` (timeline).

### M4 — Sessions, rolling capture and playback

Make capture recoverable and reusable.

- **Scope:** rolling-window strategy chosen from measured capacity; versioned session,
  asset and recovery formats; session transactions and interrupted-recording recovery;
  immutable rolling save while capture continues; nondestructive Field session browser
  and playback; band transitions qualified during recording.
- **Exit:** the rolling-save and playback stress: capture continuity and alignment
  archived with no overrun; every reported saved asset loads; failed saves preserve
  active capture where possible; latencies within measured budgets.
- **Epic:** `full_spooky_proto-hpq`, plus `full_spooky_proto-54w.12`.

### M5 — Field-to-Granular

The first Instrument workflow, reusing reliable captures.

- **Scope:** one bounded Granular engine over immutable captures; transactional Field
  save, switch and load; basic Instrument performance record and replay; a minimal
  effects chain.
- **Exit:** a capture saved in Field loads into Granular without file browsing and
  without interrupting capture; Instrument performances record and replay.
- **Epic:** `full_spooky_proto-v7l` (tasks `.1`–`.4`).

### After M5

Seek and Orbit engines, modulation, macros and sequencer, presets and persistent
settings, and Instrument microphone injection stay in the P4 backlog until M5 exits
and a scope decision admits them.

## Decisions the roadmap is waiting on

These decisions belong to the user. Each is settled with an approved record in
`docs/decisions/` (or a proposal to `spec/product/` where it changes product intent),
followed by a test. They are listed in the order that unblocks the most work.

| Order | Decision | Beads | Unblocks |
| --- | --- | --- | --- |
| 1 | Recording-safe command policy | `54w.1` | M2 command policy, then storage service and latency budget; most of M3 |
| 2 | First-slice gestures, reconciled with the control map (utility entry, hold priority, selector commit/cancel, chord arbitration) | `54w.2` | M3 interaction model, IPC and everything after it |
| 3 | Rolling-window duration and memory/storage strategy | `hpq.2` | M4 formats and rolling save. Needs measurement first, so start early. |
| 4 | Semantic EMF, activity and warning mappings (control map D-016) | `54w.8` | M3 visual feedback |
| 5 | Session, asset and recovery formats | `hpq.3` | M4 transactions and playback |
| 6 | Qualification for lifting the recording guards on tuning and band transitions (decisions 0003 and 0004) | `54w.6`, `54w.12` | Tuning and band transitions while recording in M3 and M4 |
