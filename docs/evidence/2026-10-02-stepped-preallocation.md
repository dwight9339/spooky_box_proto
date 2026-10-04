# Stepped recording preallocation

Date: 2026-10-02 (bench timestamps 16:03-16:07 UTC)
Beads issue: `full_spooky_proto-jjy.9`; follow-ups `full_spooky_proto-jjy.17` and
`full_spooky_proto-jjy.18`

`jjy.9` reported that `RECORD START` blocked the foreground loop for 1.5-3.9 s on a
nearly full card while FatFs `f_expand` searched the FAT for a contiguous run. The
[rejected trial](2026-10-01-prealloc-search-trial.md) showed that the search cannot
simply be skipped: dynamic growth moves it into the recording and overruns the
queues. This session measured the fix: the recorder prepares the file in bounded
foreground steps before capture starts, and refuses the start when no contiguous run
exists.

## The change

- The Session machine gains a Preparing substate of Active. `RECORD START` replies
  `OK RECORD PREPARING`; capture starts on the recorder's `PREPARED` report, and
  `RECORD STOP` while preparing cancels and deletes the empty file.
- The recorder probes `REC###.WAV` names, creates the file and searches the FAT in
  steps of about 32 ms (`SPOOKY_RECORD_PREPARE_STEP_MS`), using the storage service's
  run search (`CM7/App/fat_run.c`). A found run is allocated with `f_expand`, which
  then finds it at once.
- No contiguous run anywhere: `ERR RECORD free space too fragmented ...`.
- Preallocation is capped at the free space above the card reserve.

## Setup and provenance

| Item | Value |
| --- | --- |
| Image | `build/bench-jjy9-prepare-20261002a.json`, Debug, `01ae1be` plus `build/jjy9-prepare-20261002a.patch` (uncommitted working tree). Flash run `2026-10-02T160255.710467_0000-f47213e8`, result pass; `DIAG IDENTITY` reported `BUILD=jjy9-prepare-20261002a` |
| Nearly full card | 16 GB SDHC SanDisk `SC16G` (`SD INFO` PSN `0x934591FE`), the trial card: device-formatted FAT32, 32 KiB clusters, about 1,900 filler files of 8 MiB in the root with gaps of 8 MiB, plus `REC004` and `REC005`. `SD STATUS` FREE=45MiB |
| Reference card | 64 GB `SD64G` (PSN `0x6CE776AE`), FREE=57545MiB, `REC000`-`REC081` present |
| Driver | `build/sd_failure_jjy4.py` on COM3; all CDC lines in `build/jjy9-prepare-session.jsonl` |

## Results

| Card | Command | Outcome | Preparation |
| --- | --- | --- | --- |
| Nearly full 16 GB | `RECORD START` (open-ended, needs 16,875 KiB) | `PREPARING` after 0.1 s; refused after 3.3 s: `ERR RECORD free space too fragmented largest-run-kib=8192 need-kib=16896 fat-sectors=3799` | The whole FAT (3,799 sectors) read in bounded steps. Recorder idle afterwards, no file kept |
| Nearly full 16 GB | `RECORD START 20` (needs 5,625 KiB) | `REC006.WAV` PASS, 20.053 s, max write 56 ms, queues 1/8, margin OK | `open=23ms name=169ms create=110ms search=1ms fat-sectors=2 allocate=6ms steps=6 step-max=110ms elapsed=346ms` |
| Nearly full 16 GB | `RECORD START`, `RECORD STATUS` at 1 s, `RECORD STOP` at 2 s | `OK RECORD PREPARING file=REC007.WAV fat-sectors=828 elapsed=1040ms`, then `OK RECORD STOP cancelled before capture; no file kept`. Recorder idle; free space fell only by `REC006` | Search interrupted by the cancel |
| 64 GB reference | `RECORD START 30` | `REC082.WAV` PASS, 30.037 s, max write 36 ms, queues 1/8, margin OK | `open=11ms name=119ms create=5ms search=2ms fat-sectors=3 allocate=8ms steps=7 step-max=33ms elapsed=187ms` |
| 64 GB reference | `RECORD START 5` | `REC083.WAV` PASS, 5.034 s | `step-max=33ms elapsed=183ms`; `DIAG DUMP` shows no `LOOP_STALL` event between the end of `REC082` and this start |

### Foreground stalls on the nearly full card

`DIAG DUMP` after the refused start listed these `LOOP_STALL` events (a loop gap of
50 ms or more):

| Time (ms since boot) | Gap | Step (from the timing of the reply lines) |
| --- | --- | --- |
| 21036 | 139 ms | `SD STATUS` mount before the test; maintenance command, unrelated |
| 37414 | 87 ms | Mount and checks in the `RECORD START` pass |
| 37479, 37541 | 65 ms, 62 ms | One `f_stat` name probe each |
| 37657 | 116 ms | `f_open` creating the file |
| 40698 | 66 ms | Discarding the file after the refusal (`f_unlink`, unmount) |

No stall was recorded during the 3.2 s FAT search between 37657 and 40698. The
`REC006` start reported `step-max=110ms` (the create step). The event ring had
already overwritten its preparation records by the time of the dump. A 63 ms stall
during the recording matches its 56 ms longest write.

## Findings

- **The FAT search no longer blocks the loop.** It used to block in one call for
  1.5 s (baseline, contiguous run found late) or 3.9 s (no run). Now it runs in steps
  of at most about 33 ms, with the loop serviced in between. A full pass over the
  16 GB card's FAT took 3.2 s and stalled nothing.
- **A start that would grow dynamically is refused instead of risking an overrun.**
  The nearly full card has no run of 16.5 MiB, so an open-ended start is refused with
  the largest run and the need. A shorter timed recording that fits an 8 MiB gap
  records normally.
- **Behavior on a card with contiguous free space is unchanged** apart from the extra
  `PREPARING` and `PREPARED` lines: `OK RECORD START` arrives about 0.2 s after the
  command, and recording results match earlier sessions.
- **Directory calls still block in proportion to the root directory.** On this
  1,900-entry root a name probe takes about 60 ms, `f_open` 110-116 ms, the mount
  pass 87 ms and the discard 66 ms. Each is a single FatFs call that a step budget
  cannot split. On the 64 GB card (83 `REC` files) no step exceeded 33 ms. No audio
  is at risk, since capture has not started. Tracked in `full_spooky_proto-jjy.18`.

## Verdict

`jjy.9` acceptance is met for the allocation search: the search is bounded and
measured, and a contiguous-free card behaves as before. It is partial for
directory-heavy cards: `RECORD START` on the nearly full test card still produced
loop gaps of up to 116 ms (previously 1.5-3.9 s), all from directory operations,
which `jjy.18` tracks. Recordings longer than their 60 s preallocation still grow
dynamically after it (`jjy.17`). Host tests are not part of this evidence.
