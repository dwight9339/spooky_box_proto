#!/usr/bin/env bash
# Replays the Beads changes approved on 2026-09-28 (StateSmith migration, decision 0006).
# Run ONCE from the repository root on a machine where `bd dolt push` works.
# New issue IDs are captured from bd create, so notes point at whatever IDs your database assigns.
# Temporary: remove this file from the branch once it has run.
set -euo pipefail
export BD_NON_INTERACTIVE=1
P=full_spooky_proto

if bd search "StateSmith behavior-model tooling" 2>/dev/null | grep -q "8lw\."; then echo "Tasks already exist; not creating duplicates." >&2; exit 1; fi

A=$(bd create --silent --parent "$P-8lw" -t task -p 1 -l m2,off-bench,roadmap \
  --title 'Add StateSmith behavior-model tooling: pinned generator, regenerate/check script, rendering' \
  --description 'Sources: docs/decisions/0006-statesmith-behavior-model.md (Proposed; start after acceptance), Tooling items 14-16.

Scope
Pin StateSmith.Cli in .config/dotnet-tools.json (0.22.2 used in the 2026-09-28 spike; runs on .NET 8 with DOTNET_ROLL_FORWARD=Major and needs a pseudo-terminal on Linux). One script regenerates every docs/design/behavior/*Sm.puml into CM7/App/sm/ and renders SVG with a pinned PlantUML using the smetana layout (Java only); --check fails if committed generated files differ. Host CMake helper adds generated sources with unused-parameter warnings suppressed for those files only (gcc/clang and MSVC). Add repository-layout.md rows for .puml sources and generated C and SVG. The CMake firmware build never runs StateSmith.

Inline tooling for the first consumers (InputResolution machine and the Session/command-policy work, roadmap tooling rule 6); not a track-tooling task.' \
  --acceptance '--check passes on a clean tree and fails after editing a generated file or a diagram without regenerating; script runs on Windows (developer host) and Linux; firmware and host-test builds need neither .NET nor Java; repository-layout.md updated.')
echo "created A=$A"
B=$(bd create --silent --parent "$P-54w" -t task -p 1 -l interaction,m3,off-bench,roadmap \
  --title 'Implement the InputResolution Button 0 slice as a StateSmith machine with host tests' \
  --description 'Sources: docs/decisions/0005-button-0-session-prompt.md; docs/decisions/0006-statesmith-behavior-model.md; docs/design/behavior/input-resolution.md (to be deleted); 2026-09-28 spike.

Scope
Write docs/design/behavior/InputResolutionSm.puml for the Button 0 hold, prompt, confirm, cancel, withdraw, swallow/pre-held-release and release-all behavior of decision 0005; generate C into CM7/App/sm/; write the port header and host scenario tests (fake port recording actions) in tests/. Delete input-resolution.md in the same change, rewrite docs/design/behavior/README.md around decision 0006 (one machine per region, diagram rules, where proven/target/open live), update links in accepted records mechanically, and list decisions 0003-0006 in docs/README.md. Firmware wiring waits for the input service (8lw.9) and IPC (54w.4). Shift layer, chords and selectors stay in 54w.3.' \
  --acceptance 'Every behavior in decision 0005 items 1-9 has a passing host scenario test, including exclusive guards (a pre-held release is delivered exactly once) and release-all from every state; ctest green; Debug and Release build; regenerate --check clean; no remaining links to input-resolution.md.')
echo "created B=$B"
C=$(bd create --silent --parent "$P-8lw" -t task -p 1 -l decision,firmware,m2,off-bench,roadmap \
  --title 'Decide and implement the M7 event queue and state-machine dispatch order (decision 0007)' \
  --description 'Sources: docs/decisions/0006-statesmith-behavior-model.md Decision item 13; constitution Principles III and IV.

Scope
Generated machines never dispatch into one another synchronously. Draft decision record 0007: one bounded M7 queue (or one per producer) for commands and cross-region events; declared capacity and overrun policy; counters in diagnostics; run-to-completion order across machines; when in(Region.State) guards are read relative to queued events; which producers may post from ISR context. Then implement the queue as portable C with host tests. Distinct from the cross-core product IPC in 54w.4, which feeds it.' \
  --acceptance '0007 accepted by the user; host tests cover ordering, saturation/overrun behavior and counters; queue capacity and overrun counters visible in diagnostics; Debug and Release build.')
echo "created C=$C"
D=$(bd create --silent --parent "$P-8lw" -t task -p 1 -l firmware,m2,off-bench,roadmap \
  --title 'Run the Session region as a StateSmith machine in shadow mode beside the recorder' \
  --description 'Sources: docs/decisions/0006-statesmith-behavior-model.md Migration item 18; docs/design/behavior/session.md (to be deleted); CM7/Core/Src/radio_recorder.c; CM7/Core/Src/main.c RECORD handling.

Scope
Write docs/design/behavior/SessionSm.puml from the current session model (Idle, Active{Recording, Finalizing}; start rejections and aborts as choice points after the open/start actions). Generate C; implement the port over the existing recorder. In firmware the machine receives the same commands and service events and logs its state, but has no authority; a diagnostic reports any disagreement with RadioRecorder_IsActive(). Delete session.md in the same change; update references to it in 8lw.11 acceptance criteria and hpq.3. Authority moves to the machine in 8lw.4. Existing recording guards stay (Principle VI).' \
  --acceptance 'Host scenario tests cover start success, each rejection and abort path, stop/finalize, repeated stop, block-driven completion and capture faults; Debug and Release build; the unattended recording regression passes with the shadow machine enabled and logs no disagreements; regenerate --check clean.')
echo "created D=$D"

bd dep add "$B" "$A"
bd dep add "$D" "$A"
bd dep add "$D" "$C"
bd dep add "$P-54w.3" "$B"
bd dep add "$P-8lw.4" "$D"
bd dep add "$P-54w.6" "$A"
bd dep add "$P-54w.6" "$C"
bd dep relate "$C" "$P-54w.4"

bd update "$P-54w.3" --append-notes '2026-09-28 (decision 0006, Proposed): implement on the StateSmith behavior model: extend InputResolutionSm (from '"${B#$P-}"') with the Shift layer, holds, chords and selectors, and model Context (mode, engine, view, utility return) as ContextSm, deleting top-level.md'"'"'s Context tables when it lands.'
bd update "$P-8lw.4" --append-notes '2026-09-28 (decision 0006, Proposed): the Session machine from '"${D#$P-}"' (shadow mode) takes authority here: it owns the recording command guards now in main.c and the old checks are removed, keeping legacy CLI replies. Existing recording guards stay until 54w.6/54w.12 qualify them (Principle VI).'
bd update "$P-54w.6" --append-notes '2026-09-28 (decision 0006, Proposed): build the non-blocking radio control as RadioSm (docs/design/behavior/RadioSm.puml, generated into CM7/App/sm/) and delete radio.md when it lands. Behavior-model row IDs are retired: SES-I4 means '"'"'radio commands legal in every session state'"'"' (decision 0003). Update RAD-* references in 54w.7 and 54w.17 at the same time. Depends on '"${A#$P-}"' (tooling) and '"${C#$P-}"' (event queue).'
bd update "$P-54w.7" --append-notes '2026-09-28 (decision 0006, Proposed): row IDs are being retired. RAD-05 = TuneStep with edge '"'"'stop'"'"' (clamp at band edge); RAD-16 = TuneStep with edge '"'"'wrap'"'"' (to the opposite edge). The Radio diagram (54w.6) will carry these as guarded TuneStep behaviors.'
bd update "$P-54w.17" --append-notes '2026-09-28 (decision 0006, Proposed): row IDs are being retired. RAD-09 = a TuneTarget in another band starts a band transition to the target frequency.'
bd update "$P-8lw.11" --append-notes '2026-09-28 (decision 0006, Proposed): session.md is deleted by '"${D#$P-}"'. Read its acceptance criteria as: document the chosen sleep behavior in usb-cli.md and in SessionSm.puml (or the 54w.1 command policy).'
bd update "$P-hpq.3" --append-notes '2026-09-28 (decision 0006, Proposed): session.md is deleted by '"${D#$P-}"'; session-state references move to docs/design/behavior/SessionSm.puml.'

bd dolt push
echo "Done. New tasks: $A $B $C $D"
