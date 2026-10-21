# Demo image: an Encoder 1 click toggles a step from step browse (p04.34)

Date: 2026-10-20 (local, UTC-6; Spooky Bench and log timestamps are UTC, 2026-10-21
01:43 to 01:53)  
Beads issue: `full_spooky_proto-p04.34`

Demo evidence qualifies the demo image only; it is not milestone evidence (roadmap,
demo track).

## Change

The step view turned a step on or off only in step edit (C-075), so muting a step meant
entering and leaving the edit each time. The user asked to toggle from step browse
(user call 2026-10-20, which authorizes this change under
[0020](../decisions/0020-halloween-demo-slicer-and-sequencers.md) item 5). An Encoder 1
click in step browse now turns the selected step on or off and stays in browse. The
toggle in step edit is kept. The control map binds nothing to the Encoder 1 button in
step browse (C-072, C-073, C-077), so nothing is displaced. Granular and the Slicer
share the step view, so both get the toggle.

- `CM7/App/demo_sequencer.[ch]`: step browse takes the Encoder 1 click, flips the
  step's on flag and reports `pattern_edited`. It does not report `value_changed`, so
  nothing is auditioned.
- `docs/design/usb-cli.md`: the step view describes the toggle in browse.
- The caller (`InstrumentGesture` in `CM7/App/demo_field.c`) is unchanged.

## Build and flash

- Source: branch `claude/p04.34-browse-toggle` from `demo/halloween-2026` `081207d`,
  built dirty. The snapshot is the working-tree diff (`build/p0434-bench/source.patch`,
  SHA-256 `6b9a786a5311...`), archived with the Spooky Bench run.
- Native host tests 49/49 (MSVC 14.29). `demo_sequencer_test` checks the toggle from
  browse: focus stays in browse, only the selected step changes, `pattern_edited` is
  set and `value_changed` is not.
- Debug, Release and Demo build. The only warnings are the existing unused CubeMX
  `MX_*_Init` functions. Demo `p0434-demo-20261020a`: CM7 SHA-256 `1760e1d38e8c...`, CM4
  `7f23d9c8b1f8...` (unchanged), declaration `build/p0434-demo-20261020a.json`.
- Spooky Bench 0.7.0 flash, pass, both cores verified (run
  `2026-10-21T014335.072710_0000-3c4e738e`). `DIAG IDENTITY` then reported
  `BUILD=p0434-demo-20261020a BOOT=2`.

## Bench

The operator confirmed the board was connected and free. They drove the controls and
listened on the monitor output. CLI queries went to the target CDC (COM3) through
`build/console_p046.py`, logged to `build/p0434-bench/console-p0434-20261020a.jsonl`.
`DEMO SEQ` reports the view, focus, selected step and each step as `on:value` for the
active engine. `DEMO SLICE TRIGGERS` counts every Slicer trigger, and `DEMO SEQ FIRED`
counts transport steps.

The operator loaded a 3 s clip with C-010 (`CAPTURE=C050`, `LOAD_MS=5954`; fit 80 BPM,
1/16, 16 slices of 187 ms), chose SLICER and opened SEQUENCER.

| Phase | Operation | Heard | Readback |
| --- | --- | --- | --- |
| 1 | Stopped, step browse: Encoder 1 click on steps 3 and 6 (plus some extra clicking) | — | `FOCUS=STEPS`; steps 3 and 6 off, the other 14 on; all values still the identity slices |
| 2a | Running, step browse: step 3 back on, step 9 off, then stop | Each change heard when its step came round; no clicks, drops or stutter | Steps 6 and 9 off, step 3 on; `FIRED` 0 → 242, `TRIGGERS` 26 → 242 |
| 2b | CLI `DEMO SEQ RUN`, about 6 s, `DEMO SEQ STOP`; pattern fixed | — | `FIRED` 242 → 284 (+42, indices 0 to 15, 0 to 15, 0 to 9); `TRIGGERS` 242 → 278 (+36) |
| 3a | Slicer, stopped: step 12 edited, Encoder 1 click in step edit, Encoder 0 click back | — | Step 12 off (read after 3b) |
| 3b | GRANULAR, step browse: Encoder 1 click on step 5; transport run about a second | Nothing odd | Granular step 5 off, the other 15 on; back on SLICER, its pattern still had steps 6, 9 and 12 off |

In phase 2b, steps 6 and 9 each came round 3 times in 42 fired steps, so 42 − 6 = 36
triggers were expected, and 36 were counted. In phase 2a, the 26 fired steps without a
trigger fit steps that were off for part of the run. The click times were not logged,
so that count was not checked exactly.

`FIRED` rose 284 → 330 in phase 3, from the operator's short Granular run. Slicer
`TRIGGERS` rose 278 → 282 over phase 3. These 4 triggers were not attributed. They fit
auditions from the Slicer page on the way to the engine menu, but that was not
confirmed.

At the end: `SILENT=0`, `FADES_CUT=0`, `SWITCHES=3`, Slicer `RENDER_US_MAX=1144`. The
maximum was 721 µs through phase 2b and rose during the engine switch. It is within the
1,182 µs seen in [p04.15](2026-10-07-slicer-engine-demo.md) and under the 2,300 µs
budget. `DIAG STATUS`: `RADIO_OVR=0 PDM_OVR=0 SD_ERR=0 AUDIO_ERR=0 HAS_FAULT=0`,
`LOOP_MAX_MS=65`, `SD_MAX_MS=57`.

## Result

Pass on the first image. An Encoder 1 click in step browse turns the selected step on or
off, stopped and running, on the Slicer and on Granular, and focus stays in browse. A
step that is off fires no Slicer trigger (36 for 36 in the fixed-pattern run). The
toggle in step edit still works, and each engine's pattern stays its own.

Not covered: a Granular step that is off keeps the previous position (0020 item 8). That
was checked only by ear, as "nothing odd".
