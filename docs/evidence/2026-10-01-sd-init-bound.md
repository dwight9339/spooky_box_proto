# Bounded SD card initialization

Date: 2026-10-01 (local time; the UTC timestamps in the log and run IDs read 2026-10-02)
Beads issue: `full_spooky_proto-jjy.15`

An 8 GB SDHC card that never finishes powering up blocked the foreground for about
38 s on every mount attempt ([SD format session](2026-10-01-sd-format.md)). This session
finds where the time goes, bounds it, and checks that working cards are unaffected.

## Setup and provenance

| Item | Value |
| --- | --- |
| Source revision | `84628d0` plus the change (`CM7/Core/Src/sd_diskio.c`, `CM7/Core/Inc/sd_diskio.h`, `CM7/Core/Src/sd_test.c`, `CM7/Core/Src/radio_recorder.c`, `CM7/CMakeLists.txt`, `Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_ll_sdmmc.h`) |
| Image A | `build/bench-jjy15-initwait-20261001a.json`: init report and a 1 s transfer-state wait only. Flash run `2026-10-02T033648.082836_0000-6e9c2a84` |
| Image B (rejected) | `build/bench-jjy15-volttrial-20261001a.json`: power-up limit defined for the application target only; the HAL, built in the generated `STM32_Drivers` library, did not receive it. Flash run `2026-10-02T034456.504605_0000-45cfd676` |
| Image C | `build/bench-jjy15-volttrial-20261001b.json`, patch `build/jjy15-volttrial-20261001b.patch`: limit also applied to `STM32_Drivers` (confirmed in `compile_commands.json`). Flash run `2026-10-02T034858.137346_0000-455cff37` |
| Committed source | Differs from the image C patch only in docs |
| Flash | Spooky Bench `flash`, pass, both cores verified; `DIAG IDENTITY` matched each build ID |
| Driver | `build/sd_failure_jjy4.py`; all CDC lines in `build/jjy15-session.jsonl` |

## Diagnosis

The old error, `hal=0x00000000`, hid the cause because the failed mount deinitializes
the HAL handle, which clears its error code. Image A keeps an init report from
`disk_initialize` and prints it. On the 8 GB card every path reported:

```text
stage=HAL_INIT hal=0x01000000 state=0 init_ms=37788 ready_ms=0
```

All the time is inside `HAL_SD_Init`, which fails with the invalid-voltage-range
error. The card answers the power-up handshake (CMD55 + ACMD41) but never reports
power-up complete. The HAL retries `SDMMC_MAX_VOLT_TRIAL` = 65,535 times, about
0.58 ms each, before giving up. The transfer-state wait suspected in `jjy.15` is never
reached.

## Change

- `SDMMC_MAX_VOLT_TRIAL` is made overridable in the vendored `stm32h7xx_ll_sdmmc.h`
  and set to 2000 (about 1.15 s, slightly more than the SD 1 s power-up allowance) for both
  the application and `STM32_Drivers`. The vendored change is recorded in the
  [repository layout](../design/repository-layout.md#local-changes-to-vendored-code).
- The wait for the transfer state after a successful `HAL_SD_Init` uses its own 1 s
  limit instead of the 30 s data-transfer timeout.
- Mount, `SD INFO`, `SD FORMAT` and `RECORD START` failures report the init stage,
  preserved HAL error, last card state and step durations.

## Results

### Unresponsive 8 GB SDHC card

| Command | Image A | Image C |
| --- | --- | --- |
| `SD STATUS` | 37.8 s, `stage=HAL_INIT hal=0x01000000 init_ms=37788` | 1.2 s, `init_ms=1155` |
| `SD INFO` | 37.8 s | 1.2 s, `init_ms=1156` |
| `SD FORMAT CONFIRM` | 37.8 s, nothing written | 1.2 s, `MS=1156`, nothing written |
| `RECORD START 10` | 37.8 s, `ERR RECORD mount failed ...` | 1.2 s, `init_ms=1156` |
| `DIAG STATUS` `LOOP_MAX_MS` | 37797 | 1162 |

Image B gave the same 37.8 s as image A, which is how the missing definition in the
HAL library was found.

### Working cards (image C)

| Card | Mount, `SD REINIT`, `SD INFO` | Recording |
| --- | --- | --- |
| 64 GB reference (`SD64G`) | Each answered within about 100 ms | `REC080.WAV`, 10 s, PASS, max write 21 ms, `margin=OK`, `HAS_FAULT=0` |
| 16 GB SDHC (`SC16G`) | First mount about 200 ms, then about 100 ms | `REC003.WAV`, 10 s, PASS, max write 24 ms, `margin=OK` |
| 2 GB SDSC (`SU02G`, 2010) | First mount about 300 ms, then about 100 ms | Refused as unsupported (decision 0014), as expected |

Each working card mounts well inside the new 1.15 s power-up limit.

## Verdict

Pass: an unresponsive card now fails every SD path in about 1.2 s with an error that
names the failing step, and all three working cards still mount and record normally.
Only these cards were tested. A healthy card that needs close to the full 1 s
power-up allowance would still pass, but none was available to show it.
