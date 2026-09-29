# Open-ended and storage-limit recording evidence

Date: 2026-09-29
Beads issue: `full_spooky_proto-8lw.16`

This session validates open-ended recording, explicit stop, timed completion, the
WAV-size ending, and the protected-reserve/card-full ending on the Nucleo prototype.
All recordings were append-only. No existing SD files were erased or overwritten.

## Setup and provenance

| Item | Value |
| --- | --- |
| Source revision | `09e55400e2219dc201cd8f567d1926a94b416117` plus uncommitted changes |
| Source snapshot | `build/8lw16-source-snapshot-20260929b.zip`, SHA-256 `2d0db56f530a3c424f3398407f1fd7b3ad955721797ed1e2b79a466bd447d996` |
| Normal manifest | `build/bench-8lw16-ipcsmoke-20260929b.json` |
| Normal CM7 / CM4 SHA-256 | `314048377b4b869476ed134dd1fdbf8f16b30323d8966685c88ee382fbfee025` / `7d9d1116cb20cd8367de3d86a2ec8f067b1cf0fa0ea9220279c6cf9492489584` |
| Compiler | GNU Tools for STM32 14.3.1, Debug, `SPOOKY_IPC_SMOKE=ON` |
| Board | Nucleo prototype; target CDC `335A34763533`; probe `E66540F0A345382D` |
| SD | SDHC/SDXC, 59,344 MiB; 58,514 MiB free before the limit trials |
| Stimulus | Ambient radio and microphone input |
| Bench power | Not measured |
| Listening check | Not performed; signal statistics below are not an audio-quality verdict |

The board was restored to the normal manifest after the two bounded limit trials.
Its final boot-smoke run passed at
`%LOCALAPPDATA%/SpookyBench/runs/2026-09-29T161333.597080_0000-e43ee6c7`.

## Normal timed regression

The normal 60-second recording regression passed every automated stage at
`%LOCALAPPDATA%/SpookyBench/runs/2026-09-29T155312.047577_0000-ccfea26a`:
paired boot, prerequisites, recording under IPC load, CRC/WAV inspection,
recorder-to-WAV accounting, and post-transfer health.

`REC034.WAV` finalized with `reason=duration complete`: 2,883,584 frames,
17,301,504 data bytes, and 60.074 seconds. The retrieved WAV contained PCM16 at
48 kHz with three channels and the exact recorder frame count. Radio/PDM queue
high-water marks were 1/8, maximum write time was 30 ms, and radio overrun, PDM
overrun, SD error, audio error, and fault counters were all zero.

The first attempt retained valid `REC033.WAV` but the old Spooky Bench parser did
not recognize the new trailing `reason=duration complete` field and timed out. The
captured serial log proved that firmware finalization succeeded. The parser and a
regression test were updated before the passing rerun; the failed run remains at
`%LOCALAPPDATA%/SpookyBench/runs/2026-09-29T154720.294115_0000-fc45c922`.

## Open-ended stop and sustained run

A short `RECORD START` with no duration reported `duration=open`, remained active
past 15 seconds, and stopped only after `RECORD STOP`. `REC035.WAV` finalized at
868,352 frames with `reason=stopped`. CRC transfer and WAV inspection passed at
`%LOCALAPPDATA%/SpookyBench/runs/2026-09-29T160204.217751_0000-cafb2927`;
the WAV contained exactly 868,352 aligned three-channel frames.

A second normal-limit open-ended session provided the long-run check. `REC038.WAV`
remained active at 603.4 seconds, beyond the old one-minute baseline, then stopped
at the next matched block:

```text
OK RECORD ACTIVE file=REC038.WAV audio=603.4s queues=0/8,0/8 max-write=51ms
OK RECORD STOP requested; finalizing next matched block
OK RECORD PASS file=REC038.WAV frames=29175808 bytes=175054848 audio=607.829s elapsed=607865ms reason=stopped
RECORD DIAG queues radio=1/8 pdm=1/8 max-write=51ms peaks=1654,1658,678
```

The completion line is emitted only after the WAV header is patched and the file is
closed successfully. A full retrieval of this 167 MiB file was skipped because the
diagnostic link would take roughly 45 minutes; synchronized frame/byte validity is
covered by the complete `REC034.WAV` retrieval and the two limit-file retrievals.

## WAV-size limit

The production default remains the RIFF/WAVE 32-bit size ceiling. To reach the same
decision and finalization path in a bounded bench run, an opt-in IpcSmoke pair set
`SPOOKY_RECORDING_WAV_MAX_FRAMES=480000`. Its manifest is
`build/bench-8lw16-file-limit-20260929a.json`; matched boot smoke passed at
`%LOCALAPPDATA%/SpookyBench/runs/2026-09-29T160552.606900_0000-ac1554c7`.

`REC036.WAV` stopped at 479,232 frames, the last complete 4,096-frame block below
the configured ceiling, and reported `reason=WAV size limit`. CRC and WAV inspection
passed at `%LOCALAPPDATA%/SpookyBench/runs/2026-09-29T160707.730541_0000-dfefce26`:
2,875,392 data bytes, 9.984 seconds, PCM16/48 kHz/three channels, with no clipped
samples.

## Protected-reserve/card-full limit

The card was not physically filled. Instead, an opt-in IpcSmoke pair set
`SPOOKY_RECORDING_CARD_RESERVE_SECONDS=213024`, leaving about 5.2--6.2 MiB above
the protected reserve based on the real FatFs free-space reading. This exercises
the production free-space arithmetic, matched-block stop, distinct Session outcome,
and partial-file finalization without writing tens of gigabytes of filler. The
manifest is `build/bench-8lw16-card-limit-20260929a.json`; matched boot smoke passed
at `%LOCALAPPDATA%/SpookyBench/runs/2026-09-29T160926.748086_0000-57c3beed`.

During `REC037.WAV`, `SD STATUS` reported `FREE_MIB=58512` and
`RESERVE_MIB=58508`. The recorder then stopped before crossing the reserve:

```text
ERR RECORD ABORT file=REC037.WAV frames=962560 bytes=5775360 reason=card full finalized=1
RECORD DIAG queues radio=1/8 pdm=1/8 max-write=24ms peaks=1496,1499,129
```

CRC and WAV inspection of the retained partial file passed at
`%LOCALAPPDATA%/SpookyBench/runs/2026-09-29T161059.379687_0000-69d19188`:
962,560 frames, 5,775,360 data bytes, 20.053 seconds, PCM16/48 kHz/three channels,
with no clipped samples.

## Verdict

Pass. Timed and open-ended sessions finalize valid synchronized outputs; a sustained
open-ended recording remains stable until explicit stop; the WAV boundary produces a
clean completion; and the protected-storage boundary produces a distinct finalized
card-full abort. The bounded limit images were fully identified in their manifests
and were not left on the board.
