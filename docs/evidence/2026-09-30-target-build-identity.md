# Target build identity and retained boot epoch

Hardware evidence for `full_spooky_proto-5yv.1` on the NUCLEO-H755ZI-Q bench.
The operator confirmed the board was connected, powered and free before the flash.
Spooky Bench timestamps below are UTC.

## Declared image pair

- Preset: `IpcSmoke`
- Build ID: `target-id-fixes`
- Source revision: `8c4b92f14d07e2a5c4c434909584bbca950705f5`
- Dirty source snapshot SHA-256:
  `d911928dc62986fcf0cc995d1c827356d228b02b77873abfde8ddbb5538678d5`
- CM7 ELF SHA-256:
  `810c8cf4ed5bed5717bf5da096abd204eb36440eca79b6e7204384ed688081cb`
- CM4 ELF SHA-256:
  `1151a4652cccc2b93cbf282b2212742b65af83e877234b8c8992d32f87628625`
- Compiler: GNU Arm Embedded 14.3.1

The manifest preflight rechecked the image hashes, ELF ranges and embedded CM7
`SBID1:target-id-fixes` marker. The dirty source snapshot was archived with each
control/test run that consumed the manifest.

## Results

| Check | Result | Evidence |
| --- | --- | --- |
| Device discovery | PASS | `2026-09-30T151754.334083_0000-7502f909` selected target COM3 and probe COM6 by serial identity |
| Paired flash and verify | PASS | `2026-09-30T151805.706804_0000-b933310f`; CM7 and CM4 verified before reset/run, OpenOCD exit 0, M7 running |
| Boot smoke | PASS | `2026-09-30T151836.965050_0000-0b0d6f99`; all eight checks passed, target healthy, M7 running |
| Reported target identity | PASS | Boot smoke observed schema 1, core 7, build `target-id-fixes`, boot epoch 2, capabilities 31 |
| IPC liveness | PASS | TX +63, RX +64, ACK +63 and round trips +63; ABI 1/1, no local or peer error |
| Diagnostics/logger | PASS | No target fault, overrun, SD/audio error, logger loss or diagnostic-history gap |
| Controlled reset | PASS | `2026-09-30T151938.363711_0000-fbdbcdfa`; reset command completed and M7 returned running |
| Post-reset identity | PASS | `2026-09-30T151953.269020_0000-dfbd310e`; same build and capabilities, boot epoch advanced from 2 to 3 |

The epoch change across the explicit reset demonstrates that the M7 boot counter
is retained across this reset class and advances once per startup. The unchanged
build token demonstrates that the diagnostic result describes the flashed image.

## Limits

- Probe firmware identity remains explicitly unavailable because the pinned
  Spookyprobe protocol does not report it; USB/package versions were not used as
  a substitute.
- M4 liveness is inferred from validated IPC echo/ack progress, not direct M4
  debugger observation.
- This session qualifies identity reporting, paired boot and reset detection. It
  does not claim recording reliability or other pending bench gates.
