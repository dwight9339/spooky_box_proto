# Charging-monitor sleep revalidation

Date: 2026-09-30  
Beads issue: `full_spooky_proto-jjy.3`

This session revalidated the normal-build low-power charging monitor (`SLEEP START`)
after the logger extraction, following [BRINGUP](../../BRINGUP.md#charging-monitor-sleep-test)
and step 5 of the [logger/diagnostics bench procedure](../procedures/logger-diag-bench.md).
The operator confirmed the board was connected and free, powered from the LiPo and
Battery Babysitter path through `5V_EXT` with the 3.3 V regulator jumper fitted, the
fuel gauge connected, and the Spookyprobe on UART7. An ammeter was in series with
`3V3_VSYS`. Spooky Bench 0.7.0 ran on Windows with target CDC on COM3 and Spookyprobe
on COM6.

## Image and instrumentation

A spurious wake returns to sleep without printing, so the existing UART output could
not show whether a pending UART interrupt kept waking the core. This change adds one
line per RTC report:

```text
[sleep] wake report=N wakes=M last-drain=ok|aborted log-errors=E log-dropped=D
```

`wakes` counts every return from WFI since sleep entry and should equal `report`;
`last-drain` is the result of the bounded (400 ms) UART7 logger drain before the
previous sleep entry, and the log counters come from the target logger. The
[USB CLI design](../design/usb-cli.md#low-power-charging-monitor) documents the line.

| Preset | Manifest | Build ID | CM7 SHA-256 | CM4 SHA-256 |
| --- | --- | --- | --- | --- |
| Debug | `build/bench-jjy3-debug-20260930a.json` | `jjy3-sleepwake-debug-20260930a` | `4ab260e9057341a948713da8daebb0a5d9eeb527e74de280c03bc8f7f65f2259` | `d5ab1687e85e3d5a975b5e5c98bdfa8ca445b7aa8bbabc11c6dd008f94a91809` |

Built from `43a5908` plus the uncommitted change, archived as
`build/jjy3-source-20260930a.patch` (SHA-256
`993570854fa2627777a80ac246ea2e652d732770b750fa516a433d2ea894c4b0`), with GNU Tools for
STM32 14.3.1. Release also built; the only warnings were the existing unused CubeMX
`MX_*_Init` functions. Flash run `2026-09-30T184830.496410_0000-e5d04ca1` programmed
and verified both banks and left M7 running.

## Experiment builds reject sleep

Before flashing, with the `8lw22-splitwrite-ipcsmoke-20260930b` IpcSmoke pair
running, one manual `SLEEP START` on COM3 at 12:47:05 MDT returned:

```text
ERR SLEEP unavailable in IPC smoke build; use Debug
```

`DIAG IDENTITY` before and after was unchanged, so the rejection had no side effect.
IpcMismatch uses the same build condition and was not separately exercised.

## Sleep entry and drain

After flashing, `DIAG IDENTITY` (run `2026-09-30T184848.191683_0000-ec292f8b`)
reported `BUILD=jjy3-sleepwake-debug-20260930a BOOT=2 CAPS=15` and `DIAG STATUS` was
healthy. The UART7 capture, run `2026-09-30T184917.051673_0000-bd70de38`, recorded
720 s with 1,053 bytes received and none dropped by the host. At 12:50:16 MDT the
host read:

```text
OK LOG QUEUED=0 PEAK=1412 DROP_WRITES=0 DROP_BYTES=0 TX_LOST=0 TX_BYTES=2832 TX_ERRORS=0 CONTEXT=0 FLIGHT=0
OK DIAG V=1 CORE=7 COUNT=1 OVERWRITTEN=0 SD_MAX_MS=0 LOOP_MAX_MS=0 RADIO_OVR=0 PDM_OVR=0 SD_ERR=0 AUDIO_ERR=0 HAS_FAULT=0
```

and then sent `SLEEP START`. The host's CDC read ended with a Windows serial error
because the target stopped USB CDC as designed; the command's acknowledgement was not
captured. UART7 received the complete entry sequence:

```text
[sleep] preparing low-power charging monitor
[sleep] reporting once before shutdown
[fuel] update: SOC=0%, 3589 mV, discharging at -194 mA / -696 mW, 0/3399 mAh, 26.4 C
[sleep] USB CDC stopped; AUX UART7 remains active
[sleep] RTC wake self-test in 10 seconds, then reports every 5 minutes
[sleep] 3V3_VSYS turns on only while reading the gauge
[sleep] 5V_VSYS and the Babysitter power path remain on
[sleep] press the blue USER button or RESET to reboot
[sleep] 3V3_VSYS disabled; CM7 entering SLEEP mode
```

Every line arrived intact, and the next report confirmed the drain completed
(`last-drain=ok`) with no logger transport errors or dropped bytes.

## Wake cadence and spurious wakes

| Report | Host receive (s after entry chunk) | Interval | UART line |
| ---: | ---: | ---: | --- |
| 1 | 10.13 | 10.13 s | `wake report=1 wakes=1 last-drain=ok log-errors=0 log-dropped=0`, then `RTC wake self-test passed; five-minute cadence armed` |
| 2 | 310.08 | 299.95 s | `wake report=2 wakes=2 last-drain=ok log-errors=0 log-dropped=0` |
| 3 | 610.11 | 300.03 s | `wake report=3 wakes=3 last-drain=ok log-errors=0 log-dropped=0` |

Times are host read-completion timestamps from `chunks.jsonl`, not target
timestamps. `wakes` equalled `report` at every report, so over about ten minutes the
core woke only for the RTC: no pending UART or other interrupt prevented or broke
sleep. Each report was followed by a fuel-gauge update.

## Current

| Measurement | Awake, before `SLEEP START` | Asleep |
| --- | ---: | ---: |
| `3V3_VSYS` rail, series ammeter (operator) | about 20 mA | about 0 mA |
| Battery, fuel-gauge average current | -194 mA (-696 mW) at 3589 mV | -140 mA (-504 mW) at 3596-3597 mV, reports 1-3 |

The switched `3V3_VSYS` rail turned off in sleep as designed. The operator's meter
did not resolve the brief rail pulse at each report. Battery draw fell by about
54 mA (28%); the remaining 140 mA is dominated by loads outside this firmware's
control that [low-power topology](../design/prototype-hardware.md#low-power-topology)
keeps powered: `5V_VSYS` and its converter, the Nucleo's on-board ST-LINK, and the
connected Spookyprobe. The gauge values are averages, and its capacity and SOC fields
(`SOC=0%`, `0/3399 mAh`) are the preliminary, unlearned values BRINGUP describes, not
an empty cell. The gauge reported discharging throughout: no charge input was
applied, so charging behavior during sleep was not exercised.

## Wake recovery

The operator pressed the blue USER button after the third report. `DIAG IDENTITY`
(run `2026-09-30T190252.336398_0000-026e49bb`) then reported
`BUILD=jjy3-sleepwake-debug-20260930a BOOT=3 RESET=21364736`: reset flags
`0x01460000` (C1RSTF, C2RSTF, PINRSTF, SFT1RSTF), the software system reset the
button path issues. `DIAG STATUS` (run `2026-09-30T190259.422864_0000-82667e8a`) was
healthy with `COUNT=1`; the diagnostic history is RAM-resident and restarted with the
new boot, so sleep events are evidenced by the UART log rather than `DIAG DUMP`.

The board ended the session running `jjy3-sleepwake-debug-20260930a`.

## Not covered

- Charging while asleep: no charge input was connected.
- The brief `3V3_VSYS` pulse at each report was not captured by the meter.
- Whole-board sleep current: only the switched rail and the gauge's battery-side
  average were measured.
- `RESET`-button wake: only the USER button path was exercised.

Constitution check at handoff: Principles I, V and VI were touched. The change adds
observation only, preserves the normal Debug image's behavior otherwise, records
dirty-image provenance, and lists unmeasured cases rather than inferring them. No
departure is recorded.
