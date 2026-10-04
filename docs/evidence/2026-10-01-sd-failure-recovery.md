# SD failure, removal and card-full recovery

Date: 2026-10-01
Beads issues: `full_spooky_proto-jjy.4`, `full_spooky_proto-jjy.7`

This session exercises SD card removal during each storage client, a physically
filled card, retained-file inspection and repair, and multi-pass endurance. It
uses an expendable 2 GB SDSC card. On that card, the first attempt exposed an
out-of-spec SD bus clock (`jjy.7`), which was fixed and requalified before the
failure cases ran. `SD CLEAN` was used only on the test's own retained scratch
file.

## Setup and provenance

| Item | Value |
| --- | --- |
| Source revision | `413db6395580f4a47a2e76fe132030e464fdb747` |
| Initial image | `build/bench-jjy4-ipcsmoke-20261001a.json`, clean firmware, CM7 SHA-256 `321155e7c6e72b0109b6c5558ed5c63109e6ceac307b15eb9030b9f4d3c5f04d` |
| Fixed image | `build/bench-jjy7-ipcsmoke-20261001a.json`, build ID `jjy7-sdclk-20261001a`, patch `build/jjy7-sdclk-20261001a.patch` (SHA-256 `61aa5e5acfa6e4e99f7da9db3673e11ea004dcccc58854a438642de738ee9549`) |
| Fixed CM7 / CM4 SHA-256 | `c8bbe5d11fe3b4217a79799390b192e66d223dba337b887cf7b42ba42eb8a7e3` / `7d9d1116cb20cd8367de3d86a2ec8f067b1cf0fa0ea9220279c6cf9492489584` |
| Compiler | GNU Tools for STM32 14.3.1, IpcSmoke preset; Debug and Release pairs also built |
| Board | Nucleo prototype; target CDC `335A34763533`; probe `E66540F0A345382D` |
| Test card | 2 GB SDSC, `READ_BL_LEN` 1024, 3,862,528 logical blocks; FAT32, 4 KiB clusters, volume at sector 135, serial `68C1-26BB` |
| Reference card | 64 GB SDHC/SDXC used in earlier evidence, for the clock regression only |
| Driver | `build/sd_failure_jjy4.py`; every CDC line with host timestamps in `build/jjy4-session.jsonl` |
| Removal | Card pulled by hand from the audio-shield slot; times are those of the target responses |
| Bench power | Not measured |
| Listening check | Not performed |

Spooky Bench run directories are under `%LOCALAPPDATA%/SpookyBench/runs/`.

## SD transfer clock defect (jjy.7)

On the clean image, `sd-basic` failed before writing
(`2026-10-01T172408.890267_0000-71d99330`): `ERR SD file check failed
result=FR_DISK_ERR(1)`. `RECORD START` failed the same way during its filename
scan. `SD STATUS` worked, because mount and free space read only the boot sector
and FAT32 FSInfo.

A hardware breakpoint on `HAL_SD_DeInit` captured `hsd1.ErrorCode=0x2`
(`HAL_SD_ERROR_DATA_CRC_FAIL`) and `SDMMC1.CLKCR` divider 0. The RCC registers
read over SWD confirm HSI 64 MHz, M=4, N=9+3072/8192, Q=2 and
`D1CCIPR.SDMMCSEL=0`, so the SDMMC kernel clock is 75 MHz. The storage service
mounted at divider 2 (18.75 MHz) and then switched to divider 0, which bypasses
the divider and runs the bus at 75 MHz. The firmware never negotiates high speed,
so the limit is 25 MHz. The 64 GB card tolerated this; the SDSC card fails data
CRC on the first directory read.

The fix keeps divider 2 for mount and transfers. Results on the fixed image:

| Run | Card | Result |
| --- | --- | --- |
| Boot smoke `2026-10-01T172946.267796_0000-66e86e65` | SDSC | pass |
| `sd-basic` 64 MiB x 2 `2026-10-01T173056.922682_0000-29c1edf6` | SDSC | pass: 134,217,728 B written and verified, 533 s, max write 280 ms, max read 10 ms, file removed |
| Recording regression `2026-10-01T174621.280454_0000-9beb1668` | 64 GB SDHC | pass on every stage: `REC054.WAV` 2,883,584 frames, queues 1/8, max write 25 ms (baseline 30--51 ms) |
| Recording regression `2026-10-01T174038.218577_0000-94bbfab1` | SDSC | fail `diag_unhealthy`: `REC000.WAV` completed, but queues 7/8, max write 607 ms, `LOOP_MAX_MS=613`, `HAS_FAULT=1`; no overrun or SD error |

The lower clock adds no measurable write latency on the reference card. The
SDSC card's long write stalls are a property of the card and are tracked
separately in `jjy.8`.

## Removal during SD STRESS

`SD STRESS 64 1` was running its write phase when the card was pulled:

```text
ERR SD STRESS failed result=FR_DISK_ERR(1) offset=7323648 hal=0x00000000; SDTEST.BIN retained
```

The failure was reported 18 s after start. With the card out, every client
refused immediately: `ERR RECORD no SD card detected`,
`ERR WAV no SD card detected`, `ERR SD CLEAN failed result=FR_NOT_READY(3)` and
`ERR SD mount failed result=FR_NOT_READY(3)`. After reinsertion, `SD STATUS`
mounted without a reset, which shows that the lease was released. A new stress
run was refused with `SDTEST.BIN already exists; manual SD CLEAN required`.

## Removal during WAV transfer

`spookybench wav inspect --file REC000.WAV` (`2026-10-01T175722.747051_0000-e27aa313`)
received `WAV START` and then the target's `SD card removed` error. The host
ended in 10 s with `fail wav_transfer`, `human_required=false`. The target then
answered `WAV ABORT` with `already idle` and refused a new fetch with
`no SD card detected`.

## Removal during recording

The first timed attempt (`REC003.WAV`) completed all 60 s before the card was
pulled, so it is not removal evidence. An open-ended recording was then used so
that timing did not matter:

```text
OK RECORD START file=REC004.WAV duration=open ...
ERR RECORD write failed result=FR_DISK_ERR(1) bytes=468/24576
ERR RECORD ABORT file=REC004.WAV frames=524288 bytes=3145728 reason=three-channel file write failed finalized=0
```

The abort came 11.6 s after start, with `SD_ERR=1` and `HAS_FAULT=1`. The pull
landed during a write, so the reason names the failed write; the presence check
that reports `SD card removed` runs before each write. After reinsertion, the
card mounted and recorded without a reset.

## Retained-file inspection and repair

The card was read on a Windows PC after each removal. `chkdsk` was first run
without `/F`.

| Interrupted operation | Directory entry | Lost chain | Repair |
| --- | --- | --- | --- |
| SD STRESS at 7,323,648 B | `SDTEST.BIN`, 0 bytes | 7,315,456 B | `chkdsk /F`, then `SD CLEAN` on the target |
| Recording at 524,288 frames | `REC004.WAV`, 0 bytes | 17,281,024 B (the 60 s preallocation) | `chkdsk /F` and manual WAV reconstruction |

Neither client syncs before closing, so an interrupted write leaves a zero-length
directory entry. Its clusters stay allocated in the FAT as a lost chain. The
target cannot detect or reclaim these chains; they need host repair. Free space
reported by the target excludes them until repair.

The recovered recording chain (`FOUND.001/FILE0000.CHK`) starts with the
placeholder WAV header (`data` size 0), followed by the captured audio. The
first 524,288 frames give a plausible 10.92 s recording: peaks 1,518, 1,518 and
372, written to `build/jjy4-REC004-recovered.WAV` (SHA-256
`c28284f9fe7f7e7728bd226842e38d941dc6ffb9e3acd27687e7eda358e3d13e`). The
preallocated region after it holds stale full-scale data (peak 32,767, 99.9%
non-zero samples). **The end of valid audio cannot be determined from the file;
reconstruction needs the frame count from the target's `RECORD ABORT` line.**

After repair, `SD CLEAN` reported `SDTEST.BIN absent` and a 1 MiB `sd-basic`
passed (`2026-10-01T180333.143290_0000-07139dd2`). The first attempt was refused
at baseline health because of the fault latched by the overrun aborts below. No
diagnostic clear command exists, so the target was reset.

Finalized partial files were all valid when read on the PC:

| File | Ending | Frames | Size (B) |
| --- | --- | ---: | ---: |
| `REC000.WAV` | duration complete | 2,883,584 | 17,301,548 |
| `REC001.WAV` | radio queue overrun, `finalized=1` | 4,096 | 24,620 |
| `REC002.WAV` | radio queue overrun, `finalized=1` | 4,096 | 24,620 |
| `REC003.WAV` | duration complete | 2,883,584 | 17,301,548 |
| `REC005.WAV` | card full, `finalized=1` | 1,744,896 | 10,469,420 |

`REC001` and `REC002` were not removal cases. After the stress interruption, the
first block write took 780--810 ms and filled both eight-block queues before any
pull. The recorder aborted visibly and finalized the partial. See `jjy.8`.

## Physically full card

A PC filler file left 27,766,784 bytes free, 10 MiB above the 17,280,000-byte
reserve for 60 seconds. `SD STATUS` reported `FREE=26MiB`, and `SD STRESS 64 1`
was refused with `insufficient or unknown free space`.

```text
WARN RECORD contiguous preallocation unavailable; using dynamic growth
OK RECORD START file=REC005.WAV duration=open ...
ERR RECORD ABORT file=REC005.WAV frames=1744896 bytes=10469376 reason=card full finalized=1
```

The recording stopped after exactly the predicted 426 blocks (36.352 s). On the
PC, `REC005.WAV` was valid: 3 channels, 48 kHz, 16-bit, 1,744,896 frames, no
clipped samples, SHA-256
`a2f6e192008f6ca8a6af7a3a12f107a8271cf45d72e6f007a53c91dbef8725b8`. `chkdsk`
found no problems, and 17,293,312 bytes stayed free, so the reserve held. Timed
and open-ended starts were then refused with
`card full; recording reserve unavailable`.

`OK RECORD START` arrived 3.9 s after the command, and `LOOP_MAX_MS` was 3,921.
The contiguous-preallocation search scans the FAT before falling back. Capture
had not started, but the foreground loop was blocked. This is tracked in `jjy.9`.

## Subsequent operation

After the filler was deleted and the target reset, an 8 MiB `sd-basic` passed
every check (`2026-10-01T181653.104447_0000-342f7a87`). `REC006.WAV` then
recorded 20 s with `reason=duration complete` (962,560 frames). The diagnostic
fault was set again only by the card's 561 ms write stall; overrun and SD error
counters were zero.

## Verdict

Pass for the removal and card-full gates, with the defects found filed.

- Every removal produced a finite, specific failure and released ownership.
  Another client could mount without a reset.
- Card-full produced a finalized, valid partial file and later starts were
  refused.
- Interrupted writes leave unrecoverable zero-length entries and lost chains on
  the device. They need host `chkdsk /F`; a recording also needs the target's
  frame count for reconstruction.
- Multi-pass endurance (2 x 64 MiB) passed on the SDSC card.

Not covered: removal during finalization, removal on the 64 GB reference card,
and a card that fills beyond the reserve because the free-space estimate is
wrong.
