# Field session folders and manifest on hardware

Date: 2026-10-16 (local, UTC-6; Spooky Bench and log timestamps are UTC, 20:44 to
21:12)  
Beads issue: `full_spooky_proto-hpq.10` (decision 0030 items 1-6 and 19, without part
continuation, which is `full_spooky_proto-hpq.12`)

This session checks the firmware's Field session folders, index and manifest from
[decision 0030](../decisions/0030-field-session-folder-and-event-log.md), and the
M1 recording regression on the new path. The operator confirmed the board was
connected and free, powered up battery first, with a 59,344 MiB SDHC/SDXC card that
already held earlier root recordings. Spooky Bench 0.7.0 ran the regression,
flashed the Debug image, captured UART7 for 600 s and fetched the manifests. CLI
checks ran through an ad hoc pyserial script (`build/hpq10-cli.py`, not part of
Spooky Bench), logged in `build/hpq10-cli.log`.

**Result: pass.**

- **Regression:** the M1 recording regression passes on the new path, including its
  new manifest stage.
- **Folders:** every recording landed in its own `/FIELD/Fnnnn` folder, numbered in
  order from `INDEX.TXT`.
- **Manifests:** each was fetched with `state=ended` and the recorder's frame count.
- **Cancelled start:** a start cancelled after its folder existed left no folder.
- **Missing index:** with `INDEX.TXT` deleted, the next start searched from 1 and
  took the first free number.
- **Not exercised:** Button 0, because the Debug image on `main` has no
  physical-control path.

## Images

Both built clean from `354ac77` (`claude/hpq.10-session-folders`) with GNU Tools for
STM32 14.3.1 (14.3.rel1.20251027-0700).

| Build ID | Preset | CM7 SHA-256 | CM4 SHA-256 |
| --- | --- | --- | --- |
| `hpq10-ipcsmoke-20261016a` | `IpcSmoke` | `7f8f77391e0c29f474a0c51bdca17ed10416666cde047cb958b6bb409a4dffb2` | `7d9d1116cb20cd8367de3d86a2ec8f067b1cf0fa0ea9220279c6cf9492489584` |
| `hpq10-debug-20261016a` | `Debug` | `7f32e27a6e1a49d95f0a0e87446f313f90841859c6116c62036206f4dd3d96b2` | `d5ab1687e85e3d5a975b5e5c98bdfa8ca445b7aa8bbabc11c6dd008f94a91809` |

Debug, Release and IpcSmoke build. Native host tests pass 32/32 (MSVC), including the
new `session_files_test`, and the Spooky Bench Python tests pass 100/100. Host tests
are not hardware evidence.

## Recording regression

```text
spookybench test recording-regression --manifest build/hpq10-ipcsmoke-20261016a.json --seconds 60
```

Run `2026-10-16T204432.302960_0000-01998518`: **pass**. Stages: boot smoke,
prerequisites, recording, WAV inspection, accounting, manifest and post-transfer
health.

- **Recording:** `FIELD/F0001/AUDIO000.WAV`, 2,883,584 frames (60.074 s),
  `duration complete`. Queues reached at most 1/8, the longest write was 49 ms and the
  storage margin stayed `OK`.
- **Transfer:** 17,301,548 bytes with a matching CRC, SHA-256
  `b480900604dcf2c86c0338c7f0abf7dae7280c1a065166f9f65648784d71750f`. The WAV frames
  match the recorder's accounting.
- **Health:** no radio or PDM overrun, SD error, audio error or fault; no logger drops.
- **Manifest** (`session/FIELD_F0001_MANIFEST.TXT`):

```text
format=spooky-session
format_version=1
type=field
state=ended
end_reason=duration complete
build=hpq10-ipcsmoke-20261016a
boot=7
start_uptime_ms=66996
start_time=unknown
sample_rate_hz=48000
channels=radio-L,radio-R,mic
frames=2883584
audio_parts=1
part_frames=715825152
events=none
retune_settle_frames=FM:0,AM:0,SW:512,LW:512
origin=1:3204408
mic_compensation_frames=0
```

## Folder numbering, cancellation and the index

On the Debug image, after the regression's F0001:

| Step | Result |
| --- | --- |
| `RECORD START 10` | `FIELD/F0002/AUDIO000.WAV`, `PASS`, 483,328 frames |
| `RECORD START`, then `RECORD STOP` after capture began | `FIELD/F0003/AUDIO000.WAV`, `PASS`, 12,288 frames, `stopped` |
| `RECORD START 10` | `FIELD/F0004`, `PASS` |
| `RECORD START` and `RECORD STOP` in one write | cancelled before the folder existed; no number used |
| `RECORD START 10` | `FIELD/F0005`, `PASS` |
| `RECORD START`, `RECORD STOP` 120 ms later | `cancelled before capture; no file kept`; it had taken F0006 |
| `RECORD START`, `RECORD STOP` 90 ms later | cancelled before the folder existed |
| `RECORD START 10` | `FIELD/F0007`, `PASS` |

`spookybench session manifest` fetched each folder's manifest:

| Folder | Result | `state` | `end_reason` | `frames` | `origin` |
| --- | --- | --- | --- | --- | --- |
| F0002 | pass | ended | duration complete | 483,328 | `1:763032` |
| F0003 | pass | ended | stopped | 12,288 | `1:1538999` |
| F0004 | pass | ended | duration complete | 483,328 | `1:1901266` |
| F0005 | pass | ended | duration complete | 483,328 | `1:3542352` |
| F0006 | `ERR WAV open failed result=5` | | | | |
| F0007 | pass | ended | duration complete | 483,328 | `1:5013860` |

Result 5 is FatFs `FR_NO_PATH`: the cancelled session's folder no longer existed. A
folder without its manifest would give `FR_NO_FILE` (4).

The operator then deleted `FIELD/INDEX.TXT` on a PC and put the card back:

- the next `RECORD START 10` searched from 1 and took `FIELD/F0006` (folder step
  101 ms);
- the one after that used the rebuilt index, found F0007 taken, and took
  `FIELD/F0008`.

Both passed with 483,328 frames. `DIAG STATUS` stayed clean:
`RADIO_OVR=0 PDM_OVR=0 SD_ERR=0 AUDIO_ERR=0 HAS_FAULT=0`.

## Preparation time

`OK RECORD PREPARED` reports each preparation phase. On this card:

| Recording | `folder` | `manifest` | Longest step | Total |
| --- | ---: | ---: | ---: | ---: |
| F0001 (regression, first `/FIELD`) | 137 ms | 23 ms | 71 ms | 239 ms |
| F0002 to F0005, F0007, F0008 | 76-85 ms | 25-32 ms | 65-74 ms | 168-188 ms |
| F0006 (no index; five numbers taken) | 101 ms | 25 ms | 91 ms | 208 ms |

The longest single step, 65 to 91 ms, is one FatFs call that creates a folder. It is
most likely `f_mkdir` clearing the new folder's 32 KiB cluster. That exceeds the
32 ms preparation step target (`SPOOKY_RECORD_PREPARE_STEP_MS`). It happens before
capture starts, so it cannot affect a recording. It raised `LOOP_MAX_MS` to 98 ms
outside recording. It is noted on `full_spooky_proto-jjy.18`, which tracks
directory calls in recording start.

## Not exercised

- **Button 0:** the operator pressed it, but the Debug image on `main` has no
  physical-control path (`UI STATUS` reports raw states only), so no session started.
  The CLI and Button 0 feed the same session machine and recorder path.
- **Folder tree on a PC:** the operator removed the card for the index check but did
  not report the folder listing. F0006's removal is shown by `FR_NO_PATH` above.
- **Manifest at the moment of preparation:** the card is owned by the recorder until
  it ends, so a `state=recording` manifest could not be fetched. Only an interrupted
  session, such as a power loss or card removal during a recording, would leave one
  behind.
- **Other limits:** the WAV size limit, card-full and removal during a recording.

## Artifacts

Spooky Bench result files in `build/`, each naming its run directory under
`%LOCALAPPDATA%\SpookyBench\runs\`:

- `regression-hpq10-20261016a.json`
- `flash-hpq10-debug-20261016a.json`
- `console-hpq10-debug-20261016a.json`
- `manifest-F0002-hpq10.json` to `manifest-F0007-hpq10.json`

The local bench profile's artifact quota was raised from 2 to 4 GiB first, because a
flash was refused with `storage_quota` at 1.8 GiB of runs.
