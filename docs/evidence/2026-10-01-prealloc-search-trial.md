# Bounded preallocation search trial (rejected)

Date: 2026-10-01 (local time; the UTC timestamps in the log and run IDs read 2026-10-02)
Beads issue: `full_spooky_proto-jjy.9`

`jjy.9` reports that `RECORD START` blocks the foreground for seconds on a nearly full
card, because FatFs `f_expand` walks the whole FAT when it cannot find a contiguous run
quickly. This session tried a fix that bounds the search before capture, and rejected
it: it made `RECORD START` fast but moved the allocation cost into the recording, where
it overran the audio queues.

## The trial change (not committed)

- `CM7/App/fat_run.[ch]`: a FAT32 free-run scanner fed one FAT sector at a time
  (kept, host-tested in `tests/fat_run_test.c`, not linked into the firmware).
- The storage service read at most 64 FAT sectors from the volume's next-free hint;
  if it found a run, it pointed the hint at it so `f_expand` succeeded at once.
- Otherwise the recorder skipped `f_expand` and grew the file dynamically (the existing
  `WARN RECORD contiguous preallocation unavailable` path), and it capped
  preallocation at the recordable space above the card reserve.

The complete trial source is `build/jjy9-fatscan-20261001a.patch`.

## Setup and provenance

| Item | Value |
| --- | --- |
| Baseline image | `build/bench-jjy9-baseline-a2fc188.json`, Debug, built from `a2fc188` in a separate worktree. Flash run `2026-10-02T043123.784819_0000-d9ce1273` |
| Trial image | `build/bench-jjy9-fatscan-20261001a.json`, Debug, `a2fc188` plus the trial patch. Flash run `2026-10-02T043241.426804_0000-bcc12f4f` |
| Card | 16 GB SDHC SanDisk `SC16G` (device-formatted FAT32, 32 KiB clusters), filled on a PC with 1,893 files of 8 MiB, then every other file of the last 12 deleted: 54.8 MiB free in small holes (fill script in the session notes) |
| Roomy-card check | 64 GB reference card, trial image, flash run `2026-10-02T040732.151771_0000-e4763e19` |
| Driver | `build/sd_failure_jjy4.py`; all CDC lines in `build/jjy9-session.jsonl` |

## Results

| Image | Card | `RECORD START` reply | Preallocation | 30 s recording |
| --- | --- | --- | --- | --- |
| Baseline | Nearly full 16 GB | 1.5 s (`LOOP_MAX_MS=1478`) | Contiguous run found after the long search | `REC004.WAV` PASS, max write 83 ms (one `FOREGROUND_BUDGET` fault) |
| Trial | Same card, after `REC004` (46 MiB free) | 0.3 s | Skipped: `WARN ... (fat-sectors=64)` | `REC005.WAV` **ABORT, radio queue overrun** after 4.0 s: one write took 1408 ms; queues 8/8; partial finalized; `margin=LOW` |
| Trial | 64 GB reference (57 GB free) | 0.2 s | Contiguous run found within the budget | `REC081.WAV`, 10 s, PASS, max write 31 ms |

Windows did not allocate the filler files in name order, so the deleted files left at
least one gap of 17.3 MB or more: the baseline's search succeeded, slowly.

## Finding

On a nearly full card with fragmented free space, a file that grows dynamically needs
FatFs to search the FAT for its next free cluster each time it fills a gap. That
search runs inside `f_write` on the recording path; here it took 1.4 s and overran the
eight-block queues. Preallocating before capture, however slowly, keeps that search out
of the recording. Skipping preallocation to make `RECORD START` responsive trades a
pre-capture delay, which loses no audio, for a mid-recording stall, which does
(Principle I). The same mid-recording stall can affect the current code whenever no
contiguous run exists at all, since it then also falls back to dynamic growth.

## Verdict

Trial rejected; the firmware keeps the current `f_expand` behavior. The `jjy.9` plan is
now a stepped search that completes before capture starts, with no allocation searches
left for the recording path.
