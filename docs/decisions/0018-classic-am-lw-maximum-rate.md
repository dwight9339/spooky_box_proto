# 0018. Classic maximum rate on AM and LW

- **Status:** Accepted 2026-10-02
- **Date:** 2026-10-02
- **Supersedes:** [0016](0016-classic-scan-motion.md) item 5, for the AM and LW maximum
  rate only. The rest of 0016 stands.
- **Beads:** `full_spooky_proto-54w.33`

## Context

[Decision 0016](0016-classic-scan-motion.md) item 5 set each band's maximum Classic
rate from tune round trips measured while recording: FM 400, AM 200 and SW 240 jumps
per minute, with LW taking AM's value until LW is measured. Item 23 keeps these as
starting values until a bench listening trial sets them, and its Consequences expected
the trial to decide whether AM's maximum should be lower, because the receiver mutes
about 115 ms of each AM tune.

The 2026-10-02 listening trial ([evidence](../evidence/2026-10-02-classic-on-m7.md))
ran Classic on the M7 over the USB CLI. On AM at 200 per minute (one jump every
300 ms), the user heard occasional short landings and judged it "might be ok". At 180
per minute (333 ms) it "feels better", and the user chose 180 as AM's ceiling. FM's
400 was judged a good ceiling. SW and LW could not be judged at the bench location.

Principles involved: IV (timings set from measurement and a recorded trial), VII
(the rate control stays an understandable quantity; the setting is kept and the
limited rate is shown, as in 0016 item 6).

## Options

1. **Keep AM at 200.** Rejected: the user preferred 180 after hearing both.
2. **Add 200 to the rate table and lower AM to it.** Not needed: 200 is already the
   value in force, and 180 is already a table value.
3. **Chosen: AM's maximum is 180. LW keeps following AM** (0016 item 5), so LW's is
   180 too until LW is measured and judged.

## Decision

1. Classic's maximum rate on AM is 180 jumps per minute (one jump every 333 ms).
2. LW takes AM's maximum, 180, until a trial measures and judges LW.
3. FM's 400 is confirmed by the trial. SW's 240 stays a starting value until SW is
   judged (0016 item 23).

## Consequences

- `ClassicScan_DefaultConfig` sets AM and LW to 180. A rate setting above 180 on those
  bands runs at 180 and is shown as limited (0016 item 6).
- The SW listening trial, the LW trial, and the hands-on judgment of the controls
  (spec SC-009) remain open. They need reception at the bench location and physical
  controls.
- Revisit if a recording session at AM's maximum (spec SC-006) shows that 180 is not
  safe, or if hands-on use with the knobs suggests a different ceiling.

## Evidence

[Classic on the M7, 2026-10-02](../evidence/2026-10-02-classic-on-m7.md), listening
trial.
