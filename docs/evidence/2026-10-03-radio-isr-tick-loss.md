# Radio interrupt and lost ticks while recording

Date: 2026-10-03 (local, UTC-6; Spooky Bench and CLI log timestamps are UTC)  
Beads issue: `full_spooky_proto-akw`

During the `54w.8` matrix session, a 60 s recording regression failed
`recording_duration`: the recorder reported 60,074 ms of audio but only 58,242 ms
elapsed (recorded in the `54w.8` matrix evidence of the same date). This session found
the cause on committed `main`, changed one interrupt priority, and qualified the
change. The operator confirmed the board was connected and free and that nothing
physical changed apart from one full power cycle. Spooky Bench 0.7.0 ran on Windows
with target CDC on COM3 and Spookyprobe on COM6; CLI exchanges were sent by an ad hoc
pyserial script (not part of Spooky Bench), logged in the untracked local
`build/tick-*.jsonl` and `build/isr-*.jsonl` files.

**Result:** the radio half-buffer interrupt (`DMA1_Stream4`) runs longer than the 1 ms
tick period while recording, at the same priority as SysTick, so ticks are lost. With
the radio interrupts one priority level below SysTick, recordings time correctly and
the recording regression passes.

## Images

All built with GNU Tools for STM32 14.3.1+st.2, IpcSmoke preset. Every flash passed.

| Build ID | Source | Option | CM7 SHA-256 |
| --- | --- | --- | --- |
| `main-eb1d9ca-ipcsmoke-20261003-tickcheck` | `eb1d9ca` | none | `07ee07e8b22f47db1f9ad3458ab0107d4c3434a6717c8777abedbc3f856e139c` |
| `main-eb1d9ca-isrtime-20261003a` | `eb1d9ca` | `SPOOKY_ISR_TIMING_QUALIFICATION` | `887cd199e81d49d0895e750322e095d900ccb0754e6a60dc5a194f7fb3f2435c` |
| `akw-isrtime-20261003a` | `eb1d9ca` plus the fix | `SPOOKY_ISR_TIMING_QUALIFICATION` | `69a6b61f80d2a427c509ed32b38826e320385b152553204b8187222f896ef9f3` |
| `akw-ipcsmoke-20261003a` | `eb1d9ca` plus the fix | none | `86485902f824892884d6d0fb269cad84cb87b6043b55704555abeebc5b65dc13` |

The fix images were built from the patch `build/akw-isrtime-20261003a.patch`
(SHA-256 `c3a5889d6a3d51029c9c43110a5e2f04708d855d4b5706c1dcea40f0f8454645`).

## Symptom

`RECORD RESULT` reports the recorder's elapsed time from the millisecond tick and the
audio from the frame count. Before the fix, every recording from 21:10 UTC came out
short by 2.6 to 3.0%:

| Image | Recording | Audio | Elapsed |
| --- | --- | --- | --- |
| `54w8-matrix-qual-ipcsmoke-20261003h` | 60 s regression | 60,074 ms | 58,242 ms |
| `54w8-matrix-qual-ipcsmoke-20261003h` | 20 s, matrix feedback off, then on | 20,053 ms | 19,473 and 19,476 ms |
| `main-eb1d9ca-ipcsmoke-20261003-tickcheck` | 20 s, twice | 20,053 ms | 19,481 ms each |
| same, after a full power cycle (`BOOT=1`) | 20 s | 20,053 ms | 19,479 ms |
| same, tuned to static at 88.1 MHz | 20 s | 20,053 ms | 19,486 ms |
| same | 10 s | 10,069 ms | 9,808 ms |

Earlier the same day it was intermittent: `54w8-matrix-ipcsmoke-20261003d` lost time
at 20:28 UTC (19,478 ms for 20,053), while `54w8-matrix-qual-ipcsmoke-20261003e` timed
four recordings correctly between 20:30 and 20:42, and the morning's `54w.32`
regression passed (60,125 ms for 60,074).

Polling `RECORD STATUS` against the host clock, the audio advanced 19.5 s in 19.47 s,
so the audio clock was right and the tick was slow. Over a 70 s span containing one
10 s recording the tick lost only about 0.14 s, so the loss happens while capturing.
Stopping the VS Code `cube-cmsis-scanner` process, a power cycle and a static channel
made no difference, and `main` showed it as well as the matrix branch, so the `54w.8`
matrix work did not cause it.

## Cause

On `main-eb1d9ca-isrtime-20261003a`, one 20 s recording (`REC124`, elapsed 19,474 ms):

| Exception | Handler | Priority | Runs | Average | Max exclusive | Runs ≥ 500 µs |
| --- | --- | --- | --- | --- | --- | --- |
| 31 | `DMA1_Stream4`, radio half | 0 | 2,994 | 1,164 µs | 1,321 µs | all |
| 15 | SysTick | 0 | 30,997 | 27 µs | 30 µs | 0 |
| 72 | `DMA2_Stream0`, microphone block | 4 | 236 | 3,365 µs | 3,382 µs | all |

The M7 ran at 64 MHz with caches off (`CLOCK_HZ=64000000`). Every radio half took
longer than the tick period, and SysTick, at the same priority, cannot preempt it:
whenever two ticks fall due during one run, one is lost. On
[2026-10-01](2026-10-01-isr-timing.md) this handler measured at most 487 µs in the same
kind of Debug image; the decision 0012 timeline bookkeeping and the `54w.32` onset
level (a mean over 1,024 samples) were added to it since. Runs sitting just above
1 ms explain why the loss came and went.

## Fix

`AudioPath` sets `DMA1_Stream4_IRQn` and `SAI2_IRQn` to priority 1, one level below
SysTick. The radio interrupt still preempts the microphone copy (priority 4), and its
deadline is the 10.7 ms half period, so a SysTick inside it is harmless.

On `akw-isrtime-20261003a`, one 20 s recording (`REC125`): elapsed 20,101 ms for
20,053 ms of audio. The radio half still averaged about 1.24 ms (2,367 runs, max
1,336 µs exclusive), now with SysTick nested inside it (max 1,396 µs inclusive). No
overruns, `HAS_FAULT=0`.

On `akw-ipcsmoke-20261003a`, `test recording-regression --seconds 60` passed every stage
(run `2026-10-03T214024.643830_0000-1e4261e1`): `REC126`, 60,074 ms of audio in
60,122 ms, queues 1/8, SD write maximum 50 ms, loop maximum 56 ms, CRC `59bd13e5`,
no overruns or faults. Debug and Release built cleanly.

## Not covered

- The radio handler itself is still over 1 ms in the Debug image. Moving the copies
  and the onset level out of the DMA interrupts is `full_spooky_proto-jjy.13`.
- No listening check of the regression WAV.
