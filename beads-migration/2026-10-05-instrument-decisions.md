# Beads migration batch: Instrument decisions 0020–0024 (2026-10-05)

A running record of Beads changes made in a cloud session that cannot run
`bd dolt push`. The companion `.jsonl` holds the full current rows of every issue
listed here, exported with `bd export` after the last change. Temporary: delete both
files from the branch once the batch has been imported and pushed.

## Apply

From the repository root, on a machine where `bd dolt push` works:

```text
bd dolt pull
bd show full_spooky_proto-p04.7 full_spooky_proto-v7l.8 full_spooky_proto-v7l.9 \
  full_spooky_proto-v7l.10 full_spooky_proto-hpq.3
bd import beads-migration/2026-10-05-instrument-decisions.jsonl
bd show full_spooky_proto-p04.14 full_spooky_proto-p04.15 full_spooky_proto-p04.16
bd dolt push
```

`bd import` upserts whole rows by ID. Before importing, check that:

- `p04.14` to `p04.16` are still unused IDs;
- `p04.7`, `v7l.8`, `v7l.9`, `v7l.10` and `hpq.3` have not changed since the cloud
  session pulled them (2026-10-05). Any such changes would be overwritten.

After importing, check that the dependencies listed below are present.

## Base

Pulled from `refs/dolt/data` at the start of the session (2026-10-05).

## Changes

1. `p04.13` (Decide 0020): appended notes on how the open questions were resolved, then
   **closed**: "User accepted decision 0020 on 2026-10-05".
2. `p04.7` (Granular voice):
   - appended a note correcting the empty "()" in its first 2026-10-05 note;
   - **description** amended: slice quantization removed, page 2 slot unassigned,
     sources gain 0020;
   - **acceptance criteria** amended: no `slices` field in `Common/Inc/granular.h`;
     the grain-activity matrix replaces "slice quantization gives a chopped loop";
   - appended a note about acceptance and the 2026-10-14 gate.
3. **Created** `p04.14`, "Demo: Instrument transport and step sequencer shell, proven
   on Granular": P1, child of `p04`; labels interaction, off-bench, roadmap,
   track-demo. Depends on `p04.7`; relates to `v7l.8`.
4. **Created** `p04.15`, "Demo: Slicer engine with its slice page and hue-rotation
   matrix": P2, child of `p04`; labels dsp, off-bench, roadmap, track-demo. Depends on
   `p04.14`; relates to `v7l.9`.
5. **Created** `p04.16`, "Demo: Slicer sequencer view": P2, child of `p04`; labels
   interaction, off-bench, roadmap, track-demo. Depends on `p04.15`; relates to `v7l.9`.
6. `v7l.8` and `v7l.9` carry only the reverse `relates-to` rows from items 3 to 5.
7. `v7l.8` (Decide 0021): appended a 2026-10-06 note recording which open questions
   were resolved and what remains.
8. `p04.14`: appended a 2026-10-06 note on the demo transport controls (0021 item 7).
9. `v7l.8`: appended a second 2026-10-06 note saying no open questions remain.
10. `v7l.8`: appended a 2026-10-06 note on the External MIDI items 18 to 20.
11. `v7l.9` (Decide 0022): appended a 2026-10-06 note pointing at the Slicer MIDI key
    map in 0021 item 19.
12. `v7l.8` **closed**: "User accepted decision 0021 on 2026-10-06".
13. **Created** `v7l.12`, "Propose the spec/product changes from decisions 0021-0024 as
    one set": P2, child of `v7l`; labels decision, interaction, m5, off-bench, roadmap.
    Relates to `v7l.8`. Check this ID is still unused before importing.
14. `v7l.9`: appended a 2026-10-06 note on how 0022 was resolved and what remains.
15. `v7l.9` **closed**: "User accepted decision 0022 on 2026-10-06".
16. `hpq.3` (session formats): appended a 2026-10-06 note requiring a peak track
    (0023 item 21) and clip references by asset ID plus window (0023 item 20).
17. `v7l.10` (Decide 0023): appended a 2026-10-06 note on how 0023 was resolved.
