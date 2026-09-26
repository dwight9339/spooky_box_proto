# Ten-minute recording baseline

Date: 2026-09-26  
Beads issue: `full_spooky_proto-jjy.1`

This session qualified the current normal Debug image and the IpcSmoke image under
ten-minute recording load. The run used Spooky Bench 0.6.0 on Windows with target
CDC selected on COM3 and Spookyprobe selected on COM6. Firmware sources matched
Git revision `ff7292f613f49a23b26f1907ae19e8412d42011d`; the dirty working-tree
changes during this session were host/documentation changes for WAV alignment, not
firmware changes.

The top-level preset build commands had a Windows nested-build orchestration stall,
but direct inner Ninja checks for Debug CM7/CM4 and IpcSmoke CM7/CM4 reported no
work to do. The archived manifests below identify the exact ELFs that were flashed.

## Image manifests

| Preset | Manifest | CM7 SHA-256 | CM4 SHA-256 |
| --- | --- | --- | --- |
| Debug | `build/bench-jjy1-debug-20260926a.json` | `db778559ebadeeb24b256beb499e0e7f2c9cdd7d74deeba15acc8fe327a43358` | `d5ab1687e85e3d5a975b5e5c98bdfa8ca445b7aa8bbabc11c6dd008f94a91809` |
| IpcSmoke | `build/bench-jjy1-ipcsmoke-20260926a.json` | `3515acbbf8291a0e08002e8cc42e61c6772aa425b90b33a2246bd5a58b03dd59` | `8f6e005ffefdf6896fcaf85d5a51554aa64e97ecae3672b412a94ed29ba377cc` |

Compiler recorded in both manifests: GNU Tools for STM32 14.3.1
(`arm-none-eabi-gcc.exe`, 20250623). The IpcSmoke manifest includes
`SPOOKY_IPC_SMOKE=ON`.

## Normal Debug recording

Debug flashing passed in Spooky Bench run
`2026-09-26T163648.871252_0000-fbd8061d`. Both core images were verified before
reset/run.

The ten-minute recording was run from the debug-baseline console log at
`build/debug-baseline-20260926.log`. A host monitor reconnect occurred around
95 seconds because the terminal session was interrupted, but the target recording
continued. Final firmware accounting passed:

| Evidence | Value |
| --- | ---: |
| File | `REC016.WAV` |
| Frames | 28,803,072 |
| Data bytes | 172,818,432 |
| Audio duration | 600.064 s |
| Elapsed time | 600,105 ms |
| Queue high-water during progress | radio 1/8, PDM 0/8 |
| Max SD write | 50 ms |
| Final diagnostics | `COUNT=128`, `OVERWRITTEN=6916`, `SD_MAX_MS=50`, `LOOP_MAX_MS=288` |
| Error counters | `RADIO_OVR=0`, `PDM_OVR=0`, `SD_ERR=0`, `AUDIO_ERR=0`, `HAS_FAULT=0` |
| Final logger | `QUEUED=0`, `PEAK=1377`, `DROP_WRITES=0`, `DROP_BYTES=0`, `TX_LOST=0`, `TX_ERRORS=0`, `CONTEXT=0`, `FLIGHT=0` |
| Diagnostic dump | complete, `COUNT=128`, `GAPS=0` |

The first host-side `wav inspect` transfer for `REC016.WAV` was interrupted by an
agent/session interruption and left a partial artifact at 22,472,352 bytes in run
`2026-09-26T164838.242032_0000-cc489673`. This was not a target failure. The retry
completed in run `2026-09-26T174057.320285_0000-e4e6b47f`:

| Inspection evidence | Value |
| --- | --- |
| Result | pass; target healthy and running after transfer |
| File bytes | 172,818,476 |
| Transfer frames | 171,447 |
| Transfer CRC32 | `562bc365` |
| SHA-256 | `6277bd906cc4c1bdac7f424771587a9e43678538477405fbc9553465da24150e` |
| PCM format | 48,000 Hz, signed 16-bit, 3 channels, 6-byte alignment |
| Audio data | 172,818,432 bytes, 28,803,072 frames, 600.064 s |

| Channel | Range | Peak | RMS | Mean | Zero samples | Clipped |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Radio left | -1669..1651 | 1669 | 328.683 | -5.411 | 34,571 | 0 |
| Radio right | -1668..1651 | 1668 | 328.734 | -7.469 | 34,854 | 0 |
| Microphone | -676..665 | 676 | 4.087 | -0.004 | 5,773,625 | 0 |

Radio left/right correlation was 0.999972. Radio/microphone correlations were near
zero. The microphone was intentionally not driven by the earbud loopback for this
baseline; the low microphone RMS is recorded as channel statistics, not a listening
quality verdict.

## IpcSmoke recording under IPC load

IpcSmoke flashing passed in Spooky Bench run
`2026-09-26T182445.816673_0000-d2b38a3a`. Both core images were verified before
reset/run.

The ten-minute IPC-load run
`2026-09-26T182506.365233_0000-ef2e203e` passed with complete evidence and target
health healthy:

| Evidence | Value |
| --- | ---: |
| File | `REC017.WAV` |
| Frames | 28,803,072 |
| Data bytes | 172,818,432 |
| Audio duration | 600.064 s |
| Elapsed time | 600,107 ms |
| Progress records | 119 |
| Last progress audio time | 596.3 s |
| Max observed queues | radio 1/8, PDM 0/8 |
| Diagnostic queue high-water | radio 1/8, PDM 1/8 |
| Max SD write | 53 ms |
| Final diagnostics | `COUNT=128`, `OVERWRITTEN=6914`, `SD_MAX_MS=53`, `LOOP_MAX_MS=72` |
| Error counters | `RADIO_OVR=0`, `PDM_OVR=0`, `SD_ERR=0`, `AUDIO_ERR=0`, `HAS_FAULT=0` |
| Final logger | `QUEUED=0`, `PEAK=1377`, `DROP_WRITES=0`, `DROP_BYTES=0`, `TX_LOST=0`, `TX_ERRORS=0`, `CONTEXT=0`, `FLIGHT=0` |
| Diagnostic dump | 128 rows, 128 events, `GAPS=0` |

IPC was linked before, during and after recording. All snapshots reported
`LINK=UP`, ABI version 1/1, peer and ACK seen, `ERROR=0`, `PEER_ERROR=0`, and
`BUSY=0`.

| Interval | TX | RX | ACK | ROUNDTRIPS |
| --- | ---: | ---: | ---: | ---: |
| Before to recording-1 | +1943 | +2073 | +1943 | +1943 |
| Recording-1 to recording-2 | +1876 | +2004 | +1876 | +1876 |
| Recording-2 to after | +1906 | +2033 | +1906 | +1906 |

Host-side `wav inspect` for `REC017.WAV` passed in run
`2026-09-26T183612.286424_0000-880892b5`:

| Inspection evidence | Value |
| --- | --- |
| Result | pass; target healthy and running after transfer |
| File bytes | 172,818,476 |
| Transfer frames | 171,447 |
| Transfer CRC32 | `533823b3` |
| SHA-256 | `f42c47998865de0e41345281c8ea50257ad40e5a33a59b93ffe12b305743a0f9` |
| PCM format | 48,000 Hz, signed 16-bit, 3 channels, 6-byte alignment |
| Audio data | 172,818,432 bytes, 28,803,072 frames, 600.064 s |

| Channel | Range | Peak | RMS | Mean | Zero samples | Clipped |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Radio left | -1676..1629 | 1676 | 326.761 | -5.411 | 35,022 | 0 |
| Radio right | -1673..1628 | 1673 | 326.812 | -7.469 | 35,060 | 0 |
| Microphone | -665..684 | 684 | 4.975 | 0.008 | 5,042,501 | 0 |

Radio left/right correlation was 0.999972. Radio/microphone correlations were near
zero.

## Power and listening check

Bench power setup for both ten-minute runs: SYSOFF switch in the ON position,
babysitter USB plugged in, and no other power sources connected.

The operator listened to `REC016.WAV` and `REC017.WAV` after retrieval. Both WAVs
checked out; the captured audio was static, which is acceptable for this baseline.
This listening check is recorded as subjective audio-quality evidence separate from
the automated channel statistics above.

## Interpretation

Both ten-minute recordings completed with valid 600.064-second WAV files, no
clipped samples, no recording abort, no radio/PDM overruns, no SD/audio errors,
no diagnostic gaps, and no logger loss/error/context counters. IpcSmoke also
showed M4 IPC liveness and counter progress before, during and after the
recording load.

The earbud acoustic loopback was not required for this baseline. It was required
for the separate `full_spooky_proto-hpq.1` alignment/drift evidence, where REC014
and REC015 supplied the loud known-stimulus measurements.

Constitution check at handoff: Principles I, III, IV, V and VI were touched. No
departure is recorded.
