# UI service split bench qualification

Date: 2026-09-30  
Beads issues: `full_spooky_proto-8lw.9`, `full_spooky_proto-8lw.9.1`

This session exercised the M7 UI input/render service extraction on the
NUCLEO-H755ZI-Q prototype. The tested source was commit
`5aab3dff1cfa3d92e551a31300d45b7d7f4c38ae` plus the uncommitted critical-section
fix for `UI WATCH START`. The archived source snapshot SHA-256 was
`67217db74c95e3ebeac20aa9dcb94bbd79353eb5c1d20eb23f9bbd83825ae280`.

## Offline gates

- Fresh MSVC/Ninja native build: 12/12 CTest suites passed, including
  `ui_services_test`.
- Debug dual-core build passed: CM7 FLASH 198,464 B, RAM 57,216 B, RAM_DMA
  320 KiB; CM4 FLASH 7,592 B, RAM 1,664 B.
- Release dual-core build passed: CM7 FLASH 115,644 B, RAM 57,096 B, RAM_DMA
  320 KiB; CM4 FLASH 4,364 B, RAM 1,664 B.
- IpcSmoke dual-core build passed: CM7 FLASH 200,224 B, RAM 57,304 B, RAM_DMA
  320 KiB, RAM_IPC 64 B.

## UI hardware commands

Spooky Bench detected the configured target on COM3 and probe on COM6. The
Debug image flash passed in run
`2026-09-30T153844.306757_0000-459d9b68`. The manifest declared build ID
`ui-service-split-race-fix`; CM7 SHA-256 was
`fba64c6e59b66e49d64b103c005c18433f903b559fd48c8a0ec010eb4305c2c0` and CM4
SHA-256 was `0839d4a63086eff59f14a09e512e5c59a2fe312aee97ac0f316010f089fda5f1`.

The target reported:

- `UI STATUS`: all six switches released, all four encoders at AB=11/count 0,
  watch disabled, renderers idle, and zero dropped UI messages.
- `UI MATRIX PROBE`: ACK at address 0x30 with EN restored low.
- `UI LEDS`: all 14 named channels were sequenced and the target reported
  `PASS` with all outputs off afterward.
- `UI MATRIX ANIMATE`: the target reported `PASS` after 81 logical pixels and
  restored EN low.
- `UI DISPLAY TEST 0` and `UI DISPLAY TEST 2`: both target-side transfers
  reported `PASS` at 128x64 and 8 MHz.
- `UI DISPLAY OFF` and `UI OFF`: the target reported reset asserted, chip select
  high, watch/LED/matrix disabled, display off, and zero dropped messages.

No physical control events arrived during the three-minute `UI WATCH START`
capture. Button press/release, encoder direction, and the absence of a spurious
event immediately after watch start therefore remain **not exercised**, not
passed. The LED order, matrix mapping/appearance, and OLED patterns also remain
**not visually inspected**; the lines above are target-side command results
only.

## Recording regression

The IpcSmoke manifest declared build ID `ui-service-split-race-fix-ipc`; CM7
SHA-256 was `0d599cab7067380f019f8d606cb09ccfec29da23f3bf6f28836a3b6909fc8bba`
and CM4 SHA-256 was
`7d9d1116cb20cd8367de3d86a2ec8f067b1cf0fa0ea9220279c6cf9492489584`.

The first run,
`2026-09-30T154759.311248_0000-b9e2bc2a`, **failed** the diagnostic health gate.
`REC041.WAV` completed 60.074 s with queue high-water 1/8, maximum SD write
25 ms, and no radio, PDM, SD, audio, logger, or IPC errors, but one aggregate
foreground-loop interval was 114 ms against the 75 ms recording budget.
`DIAG LAST` retained `FOREGROUND_BUDGET A=0 B=114`; UI itself measured 1 ms
against its 10 ms budget and recorder measured 41 ms against 70 ms.

One clean repeat,
`2026-09-30T155253.683807_0000-9290f444`, **passed** boot smoke, prerequisites,
recording, CRC-verified WAV retrieval, recorder/WAV accounting, and
post-transfer health. `REC042.WAV` contained 2,883,584 frames / 17,301,504 data
bytes / 60.0747 s; transfer CRC32 was `33f2cf3f` and SHA-256 was
`e8a3853ee33796d80e542f6346276df27b814f74963626452f81e9ca96a7dade`.
Queue high-water was 1/8, maximum SD write was 24 ms, maximum loop gap was
74 ms, and all diagnostic, logger, and IPC error/loss counters were zero.
Listening remains pending; automated signal statistics are not an audio-quality
verdict.

The board ended this session running the IpcSmoke image.
