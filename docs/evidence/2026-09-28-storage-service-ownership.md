# Storage-service ownership regression

Date: 2026-09-28
Beads issue: `full_spooky_proto-8lw.5`

This session validates the M7 storage-service extraction. The recorder, SD
status/stress client, and WAV transfer client now share one exclusive FatFs
volume owner instead of maintaining independent mount lifecycles.

## Image and setup

| Item | Value |
| --- | --- |
| Manifest | `build/bench-8lw5-ipcsmoke-20260928a.json`, preset `IpcSmoke` |
| Source revision | `0d2858d792ce8dddb213d4c34936c1b576b25267` plus uncommitted changes, snapshot `build/8lw5-source-snapshot.zip` (SHA-256 `3eca8043cde2281e3d6b4f75fbf883f86edc8052ca8c899301afcfd361932b84`) |
| CM7 SHA-256 | `8a2d15f14d86c3fc27edcb11b8e0f0c1875e79ca2cc5c78613dd423b91250842` |
| CM4 SHA-256 | `25547123ef1fe068cfca3ce0638acef4de6c53db48cdfa4a3dabbf0b0fcefa13` |
| Compiler | GNU Tools for STM32 14.3.1, `-g`, `SPOOKY_IPC_SMOKE=ON` |
| Board | Nucleo prototype, target CDC `335A34763533`, probe `E66540F0A345382D` |
| SD | SDHC/SDXC, 59,344 MiB, 121,536,512 blocks |
| Bench power | Not recorded for this session |

The normal Ninja build remains affected by `full_spooky_proto-8lw.15`; the
preset-equivalent Debug, Release, and IpcSmoke image pairs built with NMake.

## Recording and WAV accounting

The unattended baseline regression flashed the paired image, recorded under IPC
load, retrieved the exact reported file, verified transport CRC and WAV structure,
matched recorder/WAV accounting, and checked health after the transfer.

Run directory:
`%LOCALAPPDATA%/SpookyBench/runs/2026-09-28T214218.641331_0000-ba97d87f`.
Result: `pass`, `human_required=false`, 444.8 s.

| Stage | Result | Duration |
| --- | --- | ---: |
| `boot_smoke` | pass | 41.9 s |
| `prerequisites` | pass | 5.6 s |
| `recording` | pass | 108.8 s |
| `wav_inspect` | pass | 265.2 s |
| `accounting` | pass | 0 s |
| `post_transfer_health` | pass | 21.5 s |

`REC028.WAV` contained 2,883,584 frames and 17,301,504 data bytes, exactly
matching the recorder PASS response. The transferred file was 17,301,548 bytes,
CRC32 `754c1516`, SHA-256
`dda8ded01308e265bd8386677f7614c1a64e965d8a99ead6cac42e656beb37f6`.
Queue high-water was 1/8 for both radio and PDM, maximum SD write time was 50 ms,
and all SD, audio, radio, PDM, logger, and diagnostic fault counters remained
clear. Listening was not performed.

## SD cancellation and immediate reuse

A 64 MiB, two-pass stress run used a 45-second deadline to force the supervised
cleanup reserve. This produced the expected `sd_timeout` result after entering
the write phase. The target accepted `SD STRESS STOP`, closed and removed
`SDTEST.BIN`, and reported 11,272,192 bytes written, zero verified,
`cleanup=stopped_and_removed`, and `human_required=false`.

Run directory:
`%LOCALAPPDATA%/SpookyBench/runs/2026-09-28T215153.882986_0000-5c88007e`.

The immediate reuse run then wrote, read, compared, and removed an 8 MiB scratch
file. It passed baseline health, write, verify, cleanup, card identity, and final
health checks. Exact accounting was 8,388,608 bytes written and verified; free
space returned to 58,607 MiB and every recorded diagnostic fault remained zero.

Run directory:
`%LOCALAPPDATA%/SpookyBench/runs/2026-09-28T215433.599617_0000-76762b21`.
Result: `pass`, `human_required=false`.

An earlier 30-second invocation was rejected before stress began because it could
not preserve the setup and cleanup budgets. It is not cancellation evidence.

## Software gates and limits

- Native C tests: 8/8 passed, including exclusive acquisition, busy rejection,
  invalid release, and sequential reuse in `storage_lease_test`.
- Spooky Bench Python tests: 82/82 passed with an external temporary directory.
- Firmware: Debug, Release, and IpcSmoke pairs built successfully with NMake.
- The lease is a foreground ownership boundary. ISR/DMA callback cadence and
  file-format-specific I/O remain unchanged.
- This session does not qualify physical card removal, card-full behavior,
  open-ended recording, or the 4 GiB WAV boundary.
