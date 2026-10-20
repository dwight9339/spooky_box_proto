# Demo image: a Slicer step edit auditions the new slice while stopped (p04.33)

Date: 2026-10-20 (local, UTC-6; Spooky Bench and log timestamps are UTC, 17:53 to
18:03)  
Beads issue: `full_spooky_proto-p04.33`

Demo evidence qualifies the demo image only; it is not milestone evidence (roadmap,
demo track).

## Change

On the Slicer page, selecting a slice with the transport stopped already plays it once
([0022](../decisions/0022-slicer-engine.md) item 8). The user asked for the same in
the Slicer sequencer's step edit (user call 2026-10-20, which authorizes this change
under [0020](../decisions/0020-halloween-demo-slicer-and-sequencers.md) and 0022). With
the transport stopped, each Encoder 0 turn that changes a step's slice now plays the new
slice once. While the transport runs, nothing extra plays, and the edit is heard when
the step next plays. Granular step edits are unchanged.

- `CM7/App/demo_sequencer.[ch]`: `DemoSeqTarget.value_changed` reports a step-edit
  turn that changed the value. A turn held at either end, an on/off click and browsing
  do not set it.
- `CM7/App/demo_field.c` (`InstrumentGesture`): on `value_changed`, with the Slicer
  active and the transport target stopped, it calls the existing `DemoClip_Audition`
  with the step's slice. The audio side still plays an audition only with the Slicer
  settled and the transport stopped.
- `docs/design/usb-cli.md`: the Slicer step view describes the audition.

## Build and flash

- Source: branch `claude/p04.33-step-audition` from `demo/halloween-2026` `489e7d4`,
  built dirty. The snapshot is the working-tree diff (`build/p0433-bench/source.patch`,
  SHA-256 `118f49d0c8ab...`), archived with the Spooky Bench run.
- Native host tests 49/49 (MSVC 14.29). `demo_sequencer_test` checks `value_changed`
  for a change, both ends, the on/off click and browsing.
- Debug, Release and Demo build. The only warnings are the existing unused CubeMX
  `MX_*_Init` functions. Demo `p0433-demo-20261020a`: CM7 SHA-256 `91ec966ca646...`, CM4
  `7f23d9c8b1f8...` (unchanged), declaration `build/p0433-demo-20261020a.json`.
- Spooky Bench 0.7.0 flash, pass, both cores verified (run
  `2026-10-20T175343.625157_0000-dfb7e399`). `DIAG IDENTITY` then reported
  `BUILD=p0433-demo-20261020a BOOT=5`.

## Bench

The operator confirmed the board was connected and free. They drove the controls and
listened on the monitor output. CLI queries went to the target CDC (COM3) through
`build/console_p046.py`, logged to `build/p0433-bench/console-p0433-20261020a.jsonl`.
`DEMO SLICE TRIGGERS` counts every Slicer trigger, auditions included. `DEMO SEQ FIRED`
counts transport steps. With all 16 steps on, each fired step adds one trigger.

The operator loaded a 3 s clip with C-010 (`CAPTURE=C046`, `LOAD_MS=5847`; fit 80 BPM,
1/16, 16 slices of 187 ms) and chose SLICER.

| Phase | Operation | Heard | Counters |
| --- | --- | --- | --- |
| 1 | Stopped, step edit: single detents, then past both ends, then on/off twice | One slice per changing detent; nothing at the ends or on on/off | `RUN=0`, step 1 at slice 12 (displayed 13), `PATTERN_EDITED=1` |
| 2a | Stopped: exactly 3 detents down | Three auditions | `TRIGGERS` 144 → 147; step 1 slice 12 → 9; `FIRED` 22 → 22 |
| 2b | Running about two bars: 3 detents up, then stop | Edits heard only when step 1 played | `FIRED` 22 → 130 (+108), `TRIGGERS` 147 → 255 (+108); step 1 slice 9 → 12 |
| 3 | GRANULAR, stopped: step 1 edited | No audition | Slicer `TRIGGERS` stayed 255; Granular step 1 at 30 ‰ |

Between phase 1 and 2a, the operator made further stopped edits that returned step 1 to
slice 12. `TRIGGERS` rose 132 → 144 with `FIRED` unchanged, so every trigger in that
span was an audition.

At the end: `SILENT=0`, `FADES_CUT=0`, `SWITCHES=2`, Slicer `RENDER_US_MAX=1159`. The
render maximum was 722 µs through phase 2b and rose during the engine switch. It is
within the 1,182 µs seen in [p04.15](2026-10-07-slicer-engine-demo.md) and under the
2,300 µs budget. `DIAG STATUS`: `RADIO_OVR=0 PDM_OVR=0 SD_ERR=0 AUDIO_ERR=0
HAS_FAULT=0`, `LOOP_MAX_MS=65`, `SD_MAX_MS=56`.

## Result

Pass on the first image. With the transport stopped, a step edit plays exactly one
audition per slice change (3 for 3). Running adds no trigger beyond the fired steps
(+108 for +108). A Granular step edit triggers nothing in the Slicer.

Not covered: a slice disabled on the Slicer page was not auditioned from a step edit.
That path is the same `Slicer_Trigger` call as the page's audition and is counted in
`SILENT`.
