# CubeMX reconciliation register

Where `full_spooky_proto.ioc` and the generated code disagree with the proven runtime
configuration. The `.ioc` is useful as a pin map but is not authoritative until every
entry here is reconciled or generation is formally frozen (Constitution Principle VI).
Follow the [regeneration rule](repository-layout.md#regeneration-rule) before running
CubeMX, and restore the overrides below. Reconciliation work is tracked in Beads
`full_spooky_proto-8lw.8`.

## Findings

- Most non-peripheral GPIOs have no CM7 ownership attribute, so CubeMX omitted
  their definitions and initialization. The smoke test manually owns only the
  RF reset/status pins it needs.
- SAI2 is represented as I2S-standard, 16-bit stereo master receive with
  circular halfword DMA on DMA1 Stream 4, and its kernel clock is PLL3P at
  approximately 24.576 MHz. STM32CubeMX 6.17's H755 device data nevertheless
  limits the user-set SAI master divider to 15, while this hardware needs the
  valid divider 16 for 48 kHz. The `.ioc` therefore carries the nearest valid
  CubeMX placeholder (`Mckdiv=15`). CubeMX also serializes the UI's disabled
  `Master Clock No Divider` selection using an unrelated oversampling enum and
  consequently displays a 0 Hz derived rate. `RadioSai2Start()` deliberately
  replaces both fields with the tested HAL values before starting SAI2 and
  DMA. Do not treat generated SAI2 timing as authoritative until ST's device
  data can represent these settings. The sibling jumper test's experimental
  64-bit compensation captured at half rate on this board, making playback one
  octave high.
- DFSDM1 represents the proven SPK0641 path: the approximately 24.576 MHz
  PLL3 audio clock divided by 8, falling-edge Channel 0 input with a 9-bit
  right shift, and continuous Sinc4/OSR64 Filter 0 conversion. Its regular
  output uses circular word transfers on DMA2 Stream 0. CubeMX locks that DMA
  interrupt at priority 0 under the current project policy; this is safe
  because its callbacks do not call RTOS APIs, while the existing hand-written
  MSP initialization lowers it to priority 4. Both configurations produce the
  same 3.072 MHz PDM clock and 48 kHz PCM rate.
- SDMMC initializes immediately in generated code and can stop boot when a
  card/path is unavailable. Its generated initialization is deferred.
- SPI6 is generated for 4-bit data. Its generated initialization remains
  deferred; `ui_board_test.c` configures the working 8-bit SSD1309 SPI path
  explicitly. Preserve that override until the `.ioc` is reconciled.
- The test explicitly reapplies the proven SAI1 48 kHz/MCLK setup before
  configuring the codec. PLL3P in the `.ioc` is 24.576 MHz for the legal
  64 MHz PCLK2/SAI2 relationship, but the displayed CubeMX SAI timing remains
  non-authoritative.
- USB OTG FS is represented as an M7 device-only peripheral using HSI48 at
  48 MHz. PA9 is an M7 GPIO input because firmware monitors the divided VBUS
  signal in software; PA10 remains the radio-reset bodge rather than USB ID.
  CubeMX locks the enabled OTG FS interrupt at priority 0 under the current
  project policy, while the preserved USB MSP initialization lowers it to the
  intended FreeRTOS-safe priority 6 before enabling it. HSI48 CRS calibration
  from USB2 SOF is still configured explicitly at runtime.
- The generated M4 TIM16/PF6 configuration overlaps the M7 BTN0 LED bring-up driver
  and must be removed or reassigned before M4 UI integration
  ([decision 0001](../decisions/0001-initial-ui-and-bus-ownership.md)).
