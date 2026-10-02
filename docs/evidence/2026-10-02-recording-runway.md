# Recording runway past the preallocation

Date: 2026-10-02 (bench timestamps 17:19-17:25 UTC)
Beads issue: `full_spooky_proto-jjy.17`

A recording longer than its contiguous preallocation (open-ended, or timed beyond
60 s) grows one cluster at a time. FatFs then takes the first free cluster after the
file, scanning forward through used clusters inside that block write. On a fragmented,
nearly full card such a scan took 1,408 ms and overran the queues
([trial](2026-10-01-prealloc-search-trial.md), `REC005`). FatFs's allocation internals
are private and its scan always starts at the file's last cluster, so it cannot be
steered. The fix predicts it instead.

## The change

- While recording, in idle foreground passes, the recorder reads the FAT ahead of the
  file in FatFs's allocation order, one sector (128 clusters) at a time. It keeps
  about 30 s of audio ahead of the write position.
- The scan counts the free clusters the file can reach across used stretches of at
  most `SPOOKY_RECORDING_MAX_GAP_FAT_SECTORS` FAT sectors (default 16, 2048 clusters).
  A longer stretch, or one lap around the FAT, is a barrier.
- The recording may fill only the verified space. At the barrier it finalizes before
  the next block would need another cluster: `ERR RECORD ABORT ... reason=free space
  fragmented finalized=1`.
- `RECORD LATENCY` gains `runway=`, `fat-ahead=` and `gap-max=`.
- Timed preallocations now round up to the last block (a 60 s request writes 60.075 s).

## Setup and provenance

All images are Debug, built from `cf1e4b5` plus `build/jjy17-runway-20261002a.patch`
(uncommitted working tree). `DIAG IDENTITY` confirmed each build ID after flashing.

| Image | Build flags | Flash run |
| --- | --- | --- |
| `build/bench-jjy17-runway-20261002a.json` | none (production defaults) | `2026-10-02T171912.059472_0000-f425977c`; flashed again at the end, `2026-10-02T172520.745594_0000-94e8d96e` |
| `build/bench-jjy17-gap1-20261002a.json` | `SPOOKY_RECORDING_PREALLOC_SECONDS=10`, `SPOOKY_RECORDING_MAX_GAP_FAT_SECTORS=1` | `2026-10-02T172254.587421_0000-ee9d06af` |
| `build/bench-jjy17-prealloc10-20261002a.json` | `SPOOKY_RECORDING_PREALLOC_SECONDS=10` | `2026-10-02T172354.857912_0000-dad704f3` |

The experiment images shorten the preallocation so that a recording can start in the
test card's 8 MiB free gaps, which the production 60 s preallocation refuses
([stepped preallocation](2026-10-02-stepped-preallocation.md)). The gap-1 image allows
a used stretch of 128 clusters, shorter than the 256-cluster (8 MiB) filler files
between the gaps.

| Card | Free space | Notes |
| --- | --- | --- |
| 64 GB reference `SD64G` | 57,536 MiB | Contiguous free space after the existing files |
| 16 GB `SC16G`, the nearly full test card | 39 MiB in 8 MiB gaps between 8 MiB filler files | 32 KiB clusters; 60 s card reserve leaves about 22 MiB recordable |

Driver `build/sd_failure_jjy4.py` on COM3; all CDC lines are in
`build/jjy17-session.jsonl`.

## Results

| Image | Card | Command | Outcome | Runway (`RECORD LATENCY`) | Max write / queues |
| --- | --- | --- | --- | --- | --- |
| Production | 64 GB | `RECORD START 120` | `REC084.WAV` PASS, 120.064 s; prepared in 199 ms | `runway=open fat-ahead=5 gap-max=0` | 24 ms, 1/8; `LOOP_MAX_MS=0` since boot |
| Gap 1 | 16 GB | `RECORD START` | `REC007.WAV` ABORT `reason=free space fragmented finalized=1` at 8,380,416 data bytes, 29.1 s | `runway=end fat-ahead=4 gap-max=0` | 41 ms, 1/8 |
| Prealloc 10 | 16 GB | `RECORD START` | `REC008.WAV` ABORT `reason=card full finalized=1` at 16,072,704 data bytes, 55.8 s | `runway=open fat-ahead=10 gap-max=256` | 45 ms, 1/8; histogram 0,517,130,5,2,0,0,0 |

`DIAG STATUS` reported `LOOP_MAX_MS=137` and `116` on the 16 GB runs: the
`SD STATUS` mount and the `f_open` during preparation on the 1,900-entry root
directory (`full_spooky_proto-jjy.18`), not the recording.

## Findings

- **Growth past the preallocation no longer stalls.** On the fragmented card the
  recording grew from a 2.8 MiB preallocation to 15.3 MiB, more than one 8 MiB gap
  holds, crossing used stretches of up to 256 clusters (`gap-max=256`). Its longest
  write was 45 ms, against the 1,408 ms stall
  of dynamic growth in the trial. It ended at the card reserve as before.
- **The barrier ends the recording cleanly.** With crossing disallowed, the file
  filled its gap exactly (8,380,416 data bytes plus the 44-byte header is 256
  clusters) and was finalized with an explicit fault.
- **Roomy cards are unaffected.** A 120 s recording on the 64 GB card read 5 FAT
  sectors ahead, crossed no used cluster and had a 24 ms longest write.
- Each runway step reads one FAT sector, and with 32 KiB clusters a step covers
  14.5 s of audio.

## Verdict

`jjy.17` passes on the cards tested: past their preallocation, recordings grow only
into space verified ahead, and FatFs's scan inside a block write is limited to the
configured used stretch. The 16-sector default was not exercised at its limit; the
gaps crossed here were 2 sectors. Its cost estimate (about 25 ms) is arithmetic, not
measurement, so the setting stays configurable. Host tests are not part of this
evidence.
