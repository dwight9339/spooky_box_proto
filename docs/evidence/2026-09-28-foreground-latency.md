# Foreground latency qualification

Date: 2026-09-28 MDT (runs archived after 00:00 UTC on 2026-09-29)

Target: STM32H755 Nucleo prototype with the installed radio, PDM microphone,
microSD card, target CDC cable and Spooky Probe cable. The operator confirmed the
board was connected and free before flashing.

## Build and offline gates

The final paired IpcSmoke build used manifest build ID
`8lw6-latency-20260928-03`, source revision
`a7a700527585f980398da6275faf7d71197d4e4e`, dirty source snapshot SHA-256
`ed3e3b1213920a68130dd0f66aba896391480725d900a475102f8eb54c681ffb`, CM7
SHA-256 `71e867f84d6029ce9d76cc48429487599930a06a0a2814299f9335fe1bda431c`,
and CM4 SHA-256
`f9575bb9054a7ffda2c7a76752e854a6addafbab24119d156df5ddffed0d0e44`.
Spooky Bench flash run `2026-09-29T014005.637569_0000-dd8526bd` verified both
images and left M7 running.

All eight native CTest executables passed. Paired Debug, Release and IpcSmoke
firmware builds passed with ARM GNU 14.3.1 through the NMake fallback. The known
Windows Ninja launcher hang prevented using the normal Ninja build directory; it
did not produce a compiler or linker failure.

## Measurement-driven budget calibration

Two complete 60-second recordings were retained as failed qualification attempts,
because the new detector correctly made the initial assumptions visible:

- Run `2026-09-29T005344.367129_0000-2e955501` recorded `REC029.WAV` with
  queue high-water 1/8, maximum SD write 24 ms and no capture error. Its final
  fault was `FOREGROUND_BUDGET A=13 B=50`: dispatch included the expected final
  header/sync/close after DMA capture stopped. Measurement was corrected to
  require capture active at both timing boundaries; the 75 ms aggregate budget
  was not relaxed.
- Run `2026-09-29T010331.130442_0000-4fbf7c5c` recorded `REC030.WAV` with
  queue high-water 1/8, maximum SD write 52 ms and maximum full-loop gap 73 ms.
  Its only fault was `FOREGROUND_BUDGET A=3 B=66`: conversion plus the slowest
  write exceeded the provisional 65 ms recorder sub-budget by one tick. The
  recorder budget was raised to 70 ms, still below the 75 ms aggregate budget
  and the 85.33 ms producer interval. This is the slow-SD observation used for
  the final numeric bound; it had no radio/PDM overrun, SD error or audio error.

## Final recording and IPC load

Spooky Bench run `2026-09-29T014029.327189_0000-93741ce9` passed with complete
evidence, `target_health=healthy`, `final_target_state=running`, and no human
cleanup required. It recorded `REC031.WAV` for 60.074 seconds:

| Check | Result |
| --- | ---: |
| Radio queue high-water | 1/8 |
| PDM queue high-water | 1/8 |
| Maximum SD write | 24 ms |
| Maximum legacy loop gap | 57 ms |
| Radio/PDM overruns | 0 / 0 |
| SD/audio errors | 0 / 0 |
| Diagnostic fault | none |
| Logger loss/errors | none |

IPC TX, RX, ACK and round-trip counters advanced before, twice during, and after
recording. The final 128-row diagnostic dump was gap-free and contained 127
`SD_WRITE` events plus `RECORD_END`.

## Display admission and dense CLI load

A focused manual CDC run started `UI MATRIX ANIMATE`, immediately started a
10-second recording, and issued a safe CLI command every 250 ms, rotating through
`IPC STATUS`, `DIAG STATUS`, `BATTERY STATUS`, `UI STATUS`, and `VOLUME STATUS`.
The target reported `WARN UI MATRIX ANIMATE suspended recording=1 EN=0` on the
first recording loop. `REC032.WAV` completed with 483,328 frames, queue high-water
1/8 on both sources, maximum write 19 ms and no fault or error counter.

The post-run `DIAG LATENCY` table reported:

| Service | Budget | Maximum | Violations |
| --- | ---: | ---: | ---: |
| Complete capture loop | 75 ms | 57 ms | 0 |
| Recorder | 70 ms | 39 ms | 0 |
| Audio | 10 ms | 1 ms | 0 |
| UI | 10 ms | 3 ms | 0 |
| SD test | 10 ms | 3 ms | 0 |
| Magnetometer | 10 ms | 3 ms | 0 |
| Dispatch | 10 ms | 3 ms | 0 |
| Every other measured service | 10 ms | 0–1 ms | 0 |

`DIAG STATUS` ended with zero overruns, SD/audio errors and faults; `DIAG LAST`
returned `NONE`. The legacy `LOOP_MAX_MS=95` visible in this manual run includes
non-capture command settling and is intentionally distinct from the recording-only
57 ms aggregate reported by `DIAG LATENCY`.

The recorder still performs at most one FatFs data write per service pass. This
qualification does not claim that rule is a hard card-latency guarantee: future
cards and media states must continue to be judged by `DIAG LATENCY`, queue
high-water/overrun counters and SD error evidence.
