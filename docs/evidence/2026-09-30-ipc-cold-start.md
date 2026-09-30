# IPC cold start observations

Date: 2026-09-30  
Beads issue: `full_spooky_proto-jjy.2`

This session completed the explicit power-removal cold start left open by
[the 2026-09-29 reset matrix](2026-09-29-ipc-reset-matrix.md). The operator confirmed
the board was connected and free before the session. Spooky Bench 0.7.0 ran on
Windows with target CDC on COM3 and Spookyprobe on COM6. Firmware sources matched
clean Git revision `c4ab94c7e5911de5d1f4e790c5b6befe8d14dcc5`.

## Image manifest

| Preset | Manifest | Build ID | CM7 SHA-256 | CM4 SHA-256 |
| --- | --- | --- | --- | --- |
| IpcSmoke | `build/bench-jjy2-ipcsmoke-20260930a.json` | `jjy2-coldstart-ipcsmoke-20260930a` | `69c15f042a2c856bc1bea03248677961167f36f2c91c3d774d4d776271092ca7` | `7d9d1116cb20cd8367de3d86a2ec8f067b1cf0fa0ea9220279c6cf9492489584` |

The pair was built with the STM32Cube bundle toolchain (GNU Tools for STM32 14.3.1,
`14.3.rel1.20251027-0700`, CMake 4.3.1, Ninja 1.13.2) and the explicit
`SPOOKY_BUILD_ID` above. Build flags are null in the manifest; they were not inferred.

## Pre-cold-start baseline

Boot smoke passed in run `2026-09-30T162937.652625_0000-25203961`: both banks
programmed and verified, reset completed, M7 left running, diagnostics healthy and
IPC `LINK=UP` ABI 1/1 with deltas TX +62, RX +63, ACK +62, ROUNDTRIPS +62. The
target reported build `jjy2-coldstart-ipcsmoke-20260930a`, boot epoch 4 and reset
flags `0x01460000` (`RCC_RSR` C1RSTF, C2RSTF, PINRSTF, SFT1RSTF: the software system
reset issued by the flash step).

## Power-removal cold start

The operator removed every supply and USB connection to the Nucleo, including the
Spookyprobe USB so the SWD/UART lines could not back-power the target, waited at
least 10 seconds, and reconnected without pressing reset. No host command reset or
flashed the target afterwards.

The first host contact was `DIAG IDENTITY` in run
`2026-09-30T163732.587460_0000-71d1ecb3`:

```text
OK IDENTITY V=1 CORE=7 BUILD=jjy2-coldstart-ipcsmoke-20260930a BOOT=1 RESET=16646144 CAPS=31
```

`RESET=16646144` is `0x00FE0000`: C1RSTF, C2RSTF, D1RSTF, D2RSTF, BORRSTF, PINRSTF
and PORRSTF, the power-on reset signature. Boot epoch 1 shows the RTC backup
registers holding the epoch were cleared, consistent with loss of the backup domain
on this board without VBAT backup. The build ID matches the manifest, so the
flashed pair survived power removal.

Each non-resetting query was issued twice by the host script (a scripting error, not
part of the procedure); every duplicate returned the same result:

| Run | Command | Result |
| --- | --- | --- |
| `2026-09-30T163732.587460_0000-71d1ecb3`, `2026-09-30T163739.433704_0000-19656ede` | `diag identity` | pass, as above |
| `2026-09-30T163746.341185_0000-c49be796`, `2026-09-30T163753.185851_0000-39eaa191` | `diag status` | pass, `COUNT=3 HAS_FAULT=0`, all overrun/error counters 0 |
| `2026-09-30T163800.183292_0000-26caca50`, `2026-09-30T163801.927373_0000-ad99670c` | `probe` | pass, CM7 `running`, CM4 `unavailable` |

The three retained diagnostic events at this point were `BOOT`, `IPC_LINK A=0`
(waiting) at 907 ms and `IPC_LINK A=1` (up) at 1009 ms after boot, both with error 0:
the link came up without host intervention and never entered stale or incompatible. As in earlier sessions,
the debugger's `unavailable` CM4 state supports no M4 liveness conclusion; M4
liveness is inferred only from IPC progress below.

## Post-cold-start IPC under load

`test ipc-load --seconds 10` ran in run
`2026-09-30T163840.658322_0000-8aa2a2ae` and **failed** with reason
`diag_unhealthy`. Its baseline-health, recording and IPC-under-load checks each
passed; only the final diagnostic health check failed.

IPC was `LINK=UP`, ABI 1/1, `ERROR=0 PEER_ERROR=0 BUSY=0` at every sample:

| Phase | TX | RX | ACK | ROUNDTRIPS | Delta TX/RX/ACK/RT |
| --- | ---: | ---: | ---: | ---: | --- |
| before | 1487 | 1524 | 1486 | 1486 | |
| recording-1 | 1585 | 1627 | 1584 | 1584 | +98/+103/+98/+98 |
| recording-2 | 1616 | 1660 | 1615 | 1615 | +31/+33/+31/+31 |
| after | 1687 | 1734 | 1686 | 1686 | +71/+74/+71/+71 |

`REC043.WAV` finalized with 483,328 frames, 2,899,968 data bytes, 10.069 s of audio
and 10,110 ms elapsed. Radio and PDM queue high-water marks were 1/8, maximum SD
write was 26 ms, and radio/PDM overrun, SD and audio error counters were zero.
The WAV was not retrieved or inspected.

The post-recording `DIAG STATUS` reported `LOOP_MAX_MS=127 HAS_FAULT=1`. `DIAG LAST`
(run `2026-09-30T163937.871423_0000-a456e3e9`) and a complete, gap-free `DIAG DUMP`
of 125 events (run `2026-09-30T163944.842430_0000-d43664bb`) show:

```text
DIAG EVENT SEQ=4 MS=158160 EVENT=RECORD_START A=10 B=48000
DIAG EVENT SEQ=5 MS=158167 EVENT=FOREGROUND_BUDGET A=0 B=127
DIAG EVENT SEQ=6 MS=158167 EVENT=LOOP_STALL A=127 B=0
```

The remaining events were 118 `SD_WRITE` and one `RECORD_END`. A single manual
`DIAG LATENCY` request on COM3 afterwards reported the whole loop (ID 0) at
`MAX_MS=127` with one violation against its 75 ms budget, while every individual
service stayed within budget: recorder 39/70 ms, UI 2/10, dispatch 2/10, USB,
diagnostics and magnetometer 1/10, all others 0.

This is the same signature as the `FOREGROUND_BUDGET A=0 B=114` violation recorded
in [the UI service split qualification](2026-09-30-ui-service-split.md): an
aggregate loop interval well above the sum of measured service maxima, here in the
first interval after `RECORD START`. It is tracked by
`full_spooky_proto-8lw.21` and is not an IPC fault; it is recorded here as a failed
`ipc-load` verdict rather than excused. Because `HAS_FAULT` is retained until
reset, a repeat run in this boot would stop at its health precheck, so no repeat
was attempted.

## Result for the reset-state matrix

| Reset method | Final observable state |
| --- | --- |
| Flash-and-reset (2026-09-29, this session) | M7 running; IPC `UP`; CM4 debugger state unknown |
| Paired system reset (2026-09-29) | M7 running; IPC `UP`; CM4 debugger state unknown |
| Power-removal cold start (this session) | POR/BOR flags, boot epoch 1; M7 running; IPC `UP` 1009 ms after boot and throughout a 10 s recording; CM4 debugger state `unavailable` |
| Independent M4 halt/stale/resume | Unsupported by the current debugger exposure (2026-09-29) |

The board ended this session running the `jjy2-coldstart-ipcsmoke-20260930a`
IpcSmoke pair with the loop-budget fault retained.

Constitution check at handoff: Principles III, V and VI were touched. The session
used a clean, provenance-linked paired image, distinguished inferred M4 liveness
from debugger visibility, kept the normal Debug image unchanged, and reports the
failed `ipc-load` verdict as failed. No departure is recorded.
