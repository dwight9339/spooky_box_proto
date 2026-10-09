# Prototype hardware configuration

The physical prototype that the firmware and the [bring-up procedure](../../BRINGUP.md)
assume. Update this document in the same change as any rework, bodge or jumper change.
Schematics are in [`reference/`](../../reference/). Command behavior is in the
[USB CLI contract](usb-cli.md).

## Installed hardware

- NUCLEO-H755ZI-Q and backplane powered from the same external 5 V source,
  with backplane JP8 connecting that source to Nucleo `5V_EXT`
- custom audio shield on the Zio headers
- RF module on the backplane
- SparkFun battery babysitter connected to the backplane USB data, VBUS sense,
  and `VSYS_RAW` paths
- 3.7 V, 3700 mAh LiPo connected to the battery babysitter
- Pololu S13V20F5 5 V regulator installed and producing `5V_VSYS`
- audio shield taking 3.3 V from the Nucleo Zio header; the added backplane
  3.3 V header is available as an alternate source after isolating the shield's
  Zio 3.3 V pin
- `3V3_MCU` and `3V3_VSYS` are not connected; JP9 connects the Nucleo's
  onboard 3.3 V rail only to the UI board pull-ups
- Spooky Probe (Raspberry Pi Pico running CMSIS-DAP debugprobe firmware) providing
  SWD for flashing and debugging and the UART7 log console; the Nucleo's on-board
  ST-LINK USB is not used
- UI board with two standalone buttons, four RGB push encoders, an IS31FL3741
  LED matrix and a 2.42-inch SSD1309 OLED
- PAM8302 mono amplifier breakout with a speaker, powered from `VSYS_RAW`, its
  audio input wired to the audio shield's line-out header J5 and its SD pin on
  the backplane `AMP_SD` net (PG8). The breakout pulls SD up to its own supply.

**Open:** whether to isolate the audio shield's Zio 3.3 V pin and jumper the shield to
the alternate backplane 3.3 V header. The SD stress and three-channel recording load
measurements inform this decision.

## Prototype bodges and Nucleo links

- The RF module's reset trace was cut away from PA5 and wired to PA10.
- PA5 is restored as the audio shield's active-low line-input jack detect.
- The audio shield's headphone-detect trace was cut away from PE14 and wired
  to PE0. PE14 remains available for the backplane's `TIM1_CH4` UI LED signal.
- Nucleo USB links SB21, SB26, SB27, and SB29 are open. SB22 and SB23 are
  closed so PA11/PA12 reach the ST morpho header and backplane; PA10 remains
  isolated from USB ID.
- The backplane VBUS jumper position is populated as a divider: 4.7 kOhm from
  VBUS to PA9 and 10 kOhm from PA9 to GND, yielding about 3.4 V at PA9 for a
  5 V VBUS input.

## Debug UART (Spooky Probe)

Prototype logging is routed through the backplane AUX UART at 115200 baud,
8 data bits, no parity, and one stop bit:

```text
STM32 PE8 / UART7_TX -> Pico GP5 / UART_RX
STM32 PE7 / UART7_RX <- Pico GP4 / UART_TX
STM32 GND            --- Pico GND
```

CM7 owns UART7 and uses it for all `printf` logs through the bounded logger described
in [logger and diagnostics](logger-diag.md). PE7 is configured as the receive side of
the link, but no command receiver exists on UART7; the interactive CLI is on the USB
CDC port.

## Audio path and clocking

CM7 configures and continuously runs this path:

```text
Si4735 48 kHz/16-bit stereo I2S
  -> SAI2A master RX (PD11/PD12/PD13) + circular DMA
  -> ping-pong copy in AXI SRAM
  -> SAI1A master TX + circular DMA
  -> SGTL5000 I2S input -> DAC -> headphone output

SPK0641 PDM microphone
  -> DFSDM1 (PC2 clock, PC3 data) + circular DMA

SAI2 radio L/R + DFSDM1 mic
  -> interleaved 48 kHz/16-bit/3-channel WAV on SDMMC1
```

The radio boots tuned to `99.10 MHz`. PA8 supplies its 32.768 kHz reference.
The SGTL5000 remains muted until the bodged PE0 headphone-detect input is low,
then opens at a conservative fixed `-30 dB`. Removing the plug mutes it again.
Firmware measures PD12 and PE4 directly and refuses to unmute unless both
frame-sync signals are between 47.5 and 48.5 kHz.

Normal images keep line out powered down and hold `AMP_SD` low, so the speaker
amplifier stays shut down. The opt-in `SpeakerMonitor` preset
(`SPOOKY_SPEAKER_MONITOR`, Beads `full_spooky_proto-jr0`) tests the speaker as the
default monitor. It powers SGTL5000 line out (VAG 1.65 V, level `0x1D`) and
sets PG8 as open drain, so the pin never drives the `VSYS_RAW` pull-up. With no
headphones present (PE0 debounced for 50 ms), the firmware mutes the headphone
amplifier and opens line out. The pot then sets the DAC volume over the headphone
range (0 to −51.5 dB). Plugging in headphones mutes line out, restores the DAC to
0 dB and returns the pot to the headphone amplifier. Both outputs are muted during
each path change. `AMP_SD` is released only while the speaker path is selected and
unmuted; with line out muted, the running amplifier remained faintly audible.
Evidence: [2026-10-09 speaker monitor experiment](../evidence/2026-10-09-speaker-monitor-experiment.md).

PLL3P is 24.576 MHz. SAI1 divides it to the codec's approximately 12.288 MHz MCLK;
SAI2 uses a direct /16 divider for 1.536 MHz SCK and 48 kHz FS. This also keeps the
64 MHz PCLK2 at least twice the SAI2 kernel clock. The PDM clock is 3.072 MHz from the
same PLL3 audio clock, so the microphone and radio share the nominal 48 kHz rate
without accumulating drift.

The audio-shield wheel potentiometer is on PF10/ADC3_INP6. The RF-board antenna
switch on PC6 selects the AM/LW loop path or the SW whip path; FM uses its separate
input.

Known-good device IDs from the sibling test projects:

- SGTL5000: `CHIP_ID=0xA011` (the smoke test accepts any `0xA0xx` revision)
- Si4735: part `0x23`, firmware `6.0`, component `7.0`, chip revision `D`

## Status LEDs

The green Nucleo LED means the codec, radio tune, SAI2 receive DMA, and SAI1
transmit DMA all started. Yellow means the host configured the USB CDC device.
Red means startup or a runtime SAI/DMA/USB initialization operation failed;
the UART log identifies the stage and the codec is muted on an audio failure.

## USB device

CM7 runs a full-speed USB CDC ACM device on the battery babysitter USB connector:

```text
PA9  <- USB VBUS sense
PA11 <-> USB D-
PA12 <-> USB D+
PA10 -> Si4735 reset only (never configured as USB ID)
```

The USB peripheral uses HSI48 at 48 MHz and clock recovery synchronized to
USB2 SOF. It advertises the prototype as self-powered because the MCU runs from
the external 5 V or battery-backed supply, not from VBUS. Its descriptor
advertises a maximum VBUS draw of 500 mA (`bMaxPower = 0xFA`, in 2 mA units)
for the battery babysitter's USB500 setting. The device uses ST's example CDC
VID/PID `0483:5740` and the product string `Spooky Box USB Test`; obtain a
project-owned VID/PID before distributing a product.

The Nucleo's fitted SB18/SB19 links externally supply VDD50USB and VDD33USB
from 3.3 V, so firmware leaves the internal USB regulator disabled, enables
the VDD33USB detector, and waits for `USB33RDY` before starting the PHY.

The babysitter selects its current limit in hardware and cannot wait for USB
enumeration before allowing the full current. USB500 is switch 1 OFF and switch 2 ON.

## I2C2 devices

I2C2 on PB10/PB11 is owned by M7 (see [decision 0001](../decisions/0001-initial-ui-and-bus-ownership.md)):

| Device | Address | Notes |
| --- | --- | --- |
| IS31FL3741 LED matrix | `0x30` | Hardware enable on PB5. Physical columns 2 through 10 form a centered logical 9x9 canvas; columns 0, 1, 11 and 12 stay dark. |
| Adafruit TMAG5273A2 magnetometer | `0x35` | Verified by TI manufacturer ID `0x5449` and A2 variant. Configured for all three axes, +/-133 mT range, 32x conversion averaging; slept between reads. `MAG_INT` on PC7 is an input, unused by polling. |
| BQ27441-G1A fuel gauge (battery babysitter) | `0x55` | Powered by the connected LiPo; its I2C pull-ups are on `3V3_VSYS`. |

The fuel-gauge firmware holds an idempotent, persistent configuration step for this
prototype's 3.7 V, 3700 mAh cell. When the stored capacity is not 3700 mAh or
`ITPOR` is set, it preserves the complete State data block except for Design
Capacity (3700 mAh) and Design Energy (13690 mWh), verifies the old and new block
checksums and readback, exits CONFIG UPDATE using `SOFT_RESET`, and restores the
gauge's original sealed state. Default Design Capacity is explicitly preserved.
Later boots skip the write when the values are already active and `ITPOR` is clear.

## UI board

JP9 powers the backplane's `3V3_MCU` pull-ups for the ordinary buttons and encoder
A/B contacts. Idle levels are `BTN0=1`, `BTN1=1`, encoder A/B both high, and each
encoder switch (`S`) low. A pressed standalone button reads low; an encoder pushbutton
reads high.

**Known margin issue:** the UI-board 12 kOhm / 22 kOhm network and the backplane's
additional 10 kOhm pulldown predict only about 1.82 V at the MCU when an encoder
pushbutton is pressed. A particular prototype crossing the digital threshold does not
qualify this path.

The fourteen LED channels are the two standalone button LEDs and twelve encoder RGB
channels, driven through low-side drivers. They require the correct series-resistor
values at R18 and R22.

The 2.42-inch OLED is a 128x64 SSD1309 module in 4-wire SPI mode with a zero-column
offset. Firmware overrides the provisional CubeMX SPI6 settings with an 8-bit,
transmit-only, mode-0 configuration at 8 MHz. PA7 drives MOSI, PG13 drives SCK,
PG6 is chip select, PG7 is data/command, and PA15 is reset.

UI drivers run on CM7 as bring-up ownership; the staged transfer to M4 is recorded
in [decision 0001](../decisions/0001-initial-ui-and-bus-ownership.md).

## Low-power topology

`5V_VSYS` stays enabled during sleep because the installed Pololu module has no
enable input and that rail powers the Nucleo. `BAT_SYSOFF` also stays inactive:
asserting it would disconnect the battery from the system path and would prevent
reliable battery-powered wake-up and charging observation. Sleep is therefore a
subsystem-rail/CPU-sleep state, not the lowest achievable board-off state. The
Nucleo must be powered through `5V_EXT` with the 3.3 V regulator output jumper
fitted.
