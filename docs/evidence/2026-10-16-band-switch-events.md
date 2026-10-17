# Band switch started, completed and failed events (54w.12.4)

Date: 2026-10-16 (local, UTC-6; Spooky Bench and log timestamps are UTC, 2026-10-17
02:58 to 02:59)  
Beads issue: `full_spooky_proto-54w.12.4`

This session checks user decision Q7 of `full_spooky_proto-54w.12`. The M7 publishes
band switch started, completed and failed as semantic events. On main the CLI reply,
the log and the monitor's silence during the gap acknowledge them, and display and
lights follow with `54w.5`.

The events are:

- **Started:** `RAD_PUB_BAND_STARTED`, new. It is published when a switch has begun, and
  it does not answer the command.
- **Completed:** `RAD_PUB_BAND_CHANGED`.
- **Failed:** `RAD_PUB_FAULT_BAND`.

It uses the opt-in `BandSwitchQual` preset, whose `BAND FAIL` command induces a failed
switch; normal builds have no such command. The operator confirmed the board was
connected and free. Spooky Bench 0.7.0 flashed the image, captured UART7 and reset the
board afterwards. An ad hoc pyserial script drove the CLI (`build/w12d-bench.py`,
logged in `build/w12d-bench.log`); it is not part of Spooky Bench.

**Result: pass.**

- **Announced:** every band switch that began was announced as started exactly once, and
  before its outcome.
- **Not announced:** a command superseded while waiting was never announced.
- **Replies:** every command got exactly one CLI reply, its outcome.

## Image

GNU Tools for STM32 14.3.1 (14.3.rel1.20251027-0700), `BandSwitchQual` preset, clean
build of `7fd17b5` on `claude/54w.12.4-band-switch-events`:

| Build ID | CM7 SHA-256 | CM4 SHA-256 |
| --- | --- | --- |
| `w12d-bandqual-20261016a` | `a82f2eab0b95ca6c37f2ba75d1d880ebbb3890276598862ed093a9b999373564` | `23d8a1ac972ecc3f9bb2d56de274d1634861145a2b35ebc198177b758beae655` |

Debug, Release and BandSwitchQual build. Native host tests pass 34/34 (MSVC), including
the new `radio_test` scenario: started comes once, before completed or failed, and is
not published for a switch that cannot begin. The Spooky Bench Python tests pass
125/125. Host tests are not hardware evidence.

## Replies and log

| Commands | CLI replies | UART lines, in order |
| --- | --- | --- |
| `BAND AM` | `OK RADIO BAND=AM ...` | `band switch started FM->AM`, `[gap] ... FM->AM`, `switched to AM` |
| `BAND FM` | `OK RADIO BAND=FM ...` | `band switch started AM->FM`, `[gap]`, `switched to FM` |
| `BAND LW`, `BAND SW`, `BAND FM` back to back | `OK RADIO SUPERSEDED` (SW), `OK RADIO BAND=LW ...`, `OK RADIO BAND=FM ...` | `started FM->LW`, `[gap]`, `switched to LW`, then `started LW->FM`, `[gap]`, `switched to FM`. SW is never announced. |
| `BAND AM`, then `BAND FM`, during a 20 s session (F0043, `PASS`) | one `OK RADIO BAND=...` each | `started`, `[gap]`, `switched to` for each |
| `BAND FAIL`, then `BAND SW` | `OK RADIO next band switch fails at POWER_UP`, then `ERR RADIO band switch failed; reset required` | `band switch started FM->SW`, `[radio] FAIL: SW band switch at POWER_UP`, `[radio] FAIL: band switch; receiver reset, output muted, radio track silent until reset` |

The UART log has seven started lines: six followed by their completion and one by the
failure. Every `[gap]` line reports `missed=0`. `DIAG LAST` reported
`EVENT=RADIO_FAULT A=3 B=2` after the induced failure. A Spooky Bench reset restored
the radio, and `STATUS` answered `OK RADIO BAND=FM ...`.

## Not exercised

- **The monitor:** no one listened to it. Its mute during a switch is unchanged from
  `54w.12.1`.
- **Display and lights:** they come with `54w.5`.

## Artifacts

In `build/`:

- `flash-w12d-bandqual-20261016a.json`, `console-w12d-bandqual-20261016a.json` and
  `reset-w12d-20261016a.json`, each naming its run directory under
  `%LOCALAPPDATA%\SpookyBench\runs\`
- the UART log `build/uart-w12d-20261016a.log`
