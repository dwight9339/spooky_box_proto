# Spooky Box radio/audio and USB prototype bring-up

For the opt-in dual-core communication experiment and its separate build,
see [IPC smoke test](docs/ipc-smoke-test.md). Normal builds retain the existing
M4 sleep behavior; `IPC STATUS` reports whether the experiment is enabled.

This checkout now extends the passed control-path smoke test into the first
end-to-end audio experiment on the hardware that is currently installed:

- NUCLEO-H755ZI-Q and backplane powered from the same external 5 V source,
  with backplane JP8 connecting that source to Nucleo `5V_EXT`
- custom audio shield on the Zio headers
- RF module on the backplane
- SparkFun battery babysitter connected to the backplane USB data, VBUS sense,
  and `VSYS_RAW` paths
- 3.7 V, 3700 mAh LiPo connected to the battery babysitter
- Pololu S13V20F5 5 V regulator installed and producing `5V_VSYS`
- audio shield currently taking 3.3 V from the Nucleo Zio header; the added
  backplane 3.3 V header is available as an alternate source after isolating
  the shield's Zio 3.3 V pin
- `3V3_MCU` and `3V3_VSYS` are not connected; JP9 connects the Nucleo's
  onboard 3.3 V rail only to the UI board pull-ups

The SD stress test and three-channel recording test below quantify the
highest-concern loads before deciding whether to isolate the audio shield's
Zio 3.3 V pin and jumper the shield to the alternate backplane 3.3 V header.

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

## Spooky Probe UART smoke test

Prototype logging is routed through the backplane AUX UART at 115200 baud,
8 data bits, no parity, and one stop bit:

```text
STM32 PE8 / UART7_TX -> Pico GP5 / UART_RX
STM32 PE7 / UART7_RX <- Pico GP4 / UART_TX
STM32 GND            --- Pico GND
```

The current bring-up firmware runs UART7 from CM7 and uses it for all `printf`
logs. PE7 is configured as the receive side of the link, but the interactive
CLI remains on the Spooky Box USB CDC port for now. The bounded, nonblocking
M7 logger and numeric diagnostics are implemented; see
[logger and diagnostics](docs/logger-diag.md). M4 UART ownership is still future
work. The [Spooky Bench audit](docs/spooky-bench-plan.md) describes automation
using these two separate serial paths. On reset, the Pico UART terminal should
begin with:

```text
[uart] Spooky Probe console on UART7: PE8 TX, PE7 RX, 115200 8N1
```

## Unpowered checks

Disconnect ST-LINK USB and any other cables before resistance/continuity
checks.

1. Confirm `3V3_MCU` and `3V3_VSYS` are isolated. If JP9 is fitted, confirm it
   connects `3V3_MCU` only to the UI pull-ups.
2. Confirm the 3.3 V regulator and Pololu 5 V module have the correct
   VIN/GND/VOUT orientation, and backplane JP8 is fitted for the intended
   shared external 5 V source.
   Confirm the battery and babysitter USB connector are unplugged while making
   resistance checks.
3. Confirm RF reset has continuity to PA10 and is isolated from PA5.
4. Confirm headphone detect has continuity to PE0 and is isolated from PE14.
5. Check for a hard short from `3V3_MCU`, `3V3_VSYS`, and `5V_VSYS` to GND.
   Capacitors may cause a brief low reading that rises; a persistent near-zero
   reading is a stop condition.
6. Check the Nucleo/audio-shield/backplane header alignment and pin-1
   orientation visually.

## Initial powered checks

Apply the intended external 5 V source with JP8 fitted; ST-LINK may also be
connected for programming/debugging. Before flashing the test, verify these
DC points with respect to a nearby GND test point:

| Point | Expected result |
|---|---|
| Nucleo `3V3` / audio TP1 | Approximately 3.3 V |
| Backplane `3V3_MCU` | Approximately 3.3 V |
| Backplane `3V3_VSYS` / alternate audio-power header | Approximately 3.3 V from the backplane regulator when enabled; isolated from `3V3_MCU` |
| Audio TP13 (`1V8_CODEC`) | Approximately 1.8 V |
| RF module 3.3 V rail(s) | Approximately 3.3 V |
| `5V_VSYS` | Unpowered while the battery and babysitter USB are disconnected; approximately 5 V after they are connected |
| `VSYS_RAW` | Approximately the babysitter output after its source jumper is fitted; distinct from the regulated 5 V shared with Nucleo `5V_EXT` through JP8 |

Disconnect power immediately if a rail is materially wrong or a component
heats unexpectedly.

## Firmware behavior

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

The radio is tuned to `99.10 MHz`. PA8 supplies its 32.768 kHz reference.
The SGTL5000 remains muted until the bodged PE0 headphone-detect input is low,
then opens at a conservative fixed `-30 dB`. Removing the plug mutes it again.
Firmware measures PD12 and PE4 directly and refuses to unmute unless both
frame-sync signals are between 47.5 and 48.5 kHz.

PLL3P is 24.576 MHz for this test. SAI1 divides it to the codec's approximately
12.288 MHz MCLK; SAI2 uses a direct /16 divider for 1.536 MHz SCK and 48 kHz
FS. This also keeps the 64 MHz PCLK2 at least twice the SAI2 kernel clock.

Expected known-good IDs from the sibling test projects are:

- SGTL5000: `CHIP_ID=0xA011` (the smoke test accepts any `0xA0xx` revision)
- Si4735: part `0x23`, firmware `6.0`, component `7.0`, chip revision `D`

The green Nucleo LED means the codec, radio tune, SAI2 receive DMA, and SAI1
transmit DMA all started. Yellow means the host configured the USB CDC device.
Red means startup or a runtime SAI/DMA/USB initialization operation failed;
the UART log identifies the stage and the codec is muted on an audio failure.

## USB CDC echo test

CM7 also starts a minimal full-speed USB CDC ACM device on the battery
babysitter USB connector:

```text
PA9  <- USB VBUS sense
PA11 <-> USB D-
PA12 <-> USB D+
PA10 -> Si4735 reset only (never configured as USB ID)
```

The USB peripheral uses HSI48 at 48 MHz and clock recovery synchronized to
USB2 SOF. It advertises the prototype as self-powered because the MCU can run
from ST-LINK or the battery-backed internal rail without VBUS. Its descriptor
advertises a maximum VBUS draw of 500 mA (`bMaxPower = 0xFA`, in 2 mA units)
for the battery babysitter's USB500 setting. The test currently uses ST's
example CDC VID/PID
`0483:5740`; obtain a project-owned VID/PID before distributing a product.
The Nucleo's fitted SB18/SB19 links externally supply VDD50USB and VDD33USB
from 3.3 V, so firmware leaves the internal USB regulator disabled, enables
the VDD33USB detector, and waits for `USB33RDY` before starting the PHY.

For the current shared-supply setup, keep backplane JP8 fitted and ensure the
external 5 V source can supply the combined backplane, Nucleo, and shield
load. With the babysitter USB cable disconnected, select USB500 using switch
1 OFF and switch 2 ON. Then connect the babysitter USB connector through a
powered hub or a port known to support 500 mA using a data-capable cable. The
babysitter selects its current limit in hardware and cannot wait for USB
enumeration before allowing the full current.
Windows should create a second COM port, distinct from the ST-LINK virtual COM
port; the device's USB product string is `Spooky Box USB Test` (Device Manager
may instead display the generic `USB Serial Device` name). Open it with any
conventional baud setting (the USB transport ignores the baud rate). It prints:

```text
Spooky Box USB CLI ready
Type HELP for commands.
```

Commands are ASCII lines terminated by CR, LF, or CRLF:

```text
HELP
STATUS
BAND
BAND FM
BAND AM
BAND SW
BAND LW
TUNE 99100
UP
DOWN
VOLUME READ
MAG READ
MAG STATUS
MAG STREAM START 100
MAG STREAM STOP
EMF READ
EMF STATUS
EMF ZERO
EMF STREAM START 100
EMF STREAM STOP
RECORD STATUS
RECORD START 60
RECORD STOP
SD STATUS
SD REINIT
SD STRESS 64 1
SD CLEAN
UI STATUS
UI WATCH START
UI WATCH STOP
UI LEDS
UI MATRIX PROBE
UI MATRIX ANIMATE
UI DISPLAY TEST
UI DISPLAY TEST 2
UI DISPLAY OFF
UI OFF
SLEEP START
```

`BAND` reports the current receiver band and its tuning range. `BAND FM`,
`BAND AM`, `BAND SW`, and `BAND LW` switch receiver function, restore the last
frequency used in that band, and briefly mute the headphones while the Si4735
is power-cycled. The defaults, ranges, and `UP`/`DOWN` increments are:

| Band | Default | Range (kHz) | Step |
| --- | ---: | ---: | ---: |
| FM | 99100 kHz | 87500..108000 | 100 kHz |
| AM | 1000 kHz | 520..1710 | 10 kHz |
| SW | 6000 kHz | 2300..23000 | 5 kHz |
| LW | 198 kHz | 153..279 | 9 kHz |

The RF-board antenna switch on PC6 selects the AM/LW loop path or the SW whip
path automatically. FM uses its separate input. `TUNE` always accepts an
integer frequency in kHz and validates it against the current band; FM values
are rounded to the Si4735's nearest 10 kHz command unit. A successful tune
returns the band, actual frequency, RSSI, SNR, and valid-channel flag, for
example:

```text
OK RADIO BAND=FM FREQ=99100 kHz (99.100 MHz) RSSI=7 SNR=2 VALID=0
```

The audio-shield wheel potentiometer is sampled on PF10/ADC3_INP6 every 10 ms.
Firmware applies a low-pass filter and one-dB update hysteresis, maps the knob
from mute through 0 dB headphone gain, and continues to honor the PE0 jack
detect. `VOLUME READ` (also accepted as `VOLUME` or `VOLUME STATUS`) forces a
fresh ADC sample and reports its current level, for example:

```text
OK VOLUME ADC=32768 LEVEL=50% ATTEN=-26.5 dB MUTED=0
```

This line protocol is also the simplest Pure Data control path. Install the
`comport` serial external using Pd's **Help > Find externals**, open the Spooky
Box COM port at 115200, and send the ASCII bytes for `TUNE <kHz>` followed by
byte 10. A Pd slider can map 0..1 to 87500..108000, round the result, prepend
`TUNE`, and append the newline. USB MIDI or OSC can be added later if their
ecosystem advantages outweigh the extra USB-class or network framing work.

The Adafruit TMAG5273A2 breakout shares I2C2 on PB10/PB11 with the fuel
gauge at its default address, `0x35`. At boot, firmware verifies the TI
manufacturer ID (`0x5449`) and A2 variant, enables all three axes at the
default +/-133 mT range with 32x conversion averaging, prints one sample, and
returns the sensor to sleep. The PC7 `MAG_INT` connection is kept as an input
but is not needed for this polling test.

`MAG READ` wakes the sensor, returns one X/Y/Z result in microtesla plus raw
codes and conversion status, then puts it back to sleep. `MAG STREAM START`
starts a 100 ms stream; an optional period from 50 through 60000 ms can be
provided after `START`. `MAG STREAM STOP` stops the stream and returns the
sensor to sleep. Example output:

```text
OK MAG STREAM START period=100 ms
MAG X=24uT Y=-48uT Z=101uT RAW=6,-12,25 SET=3 READY=1 DIAG=0
OK MAG STREAM STOP; sensor sleeping
```

The prototype `EMF` value is a magnetic anomaly metric, not electromotive
force in volts. Firmware calculates the 3D field magnitude and reports the
absolute difference from a slowly adapting ambient baseline:

```text
EMF = abs(sqrt(X*X + Y*Y + Z*Z) - baseline)
```

The initial boot sample seeds the baseline. A low-duty background reading
updates it every five seconds by 1/16 of the difference, giving an effective
time constant of roughly 80 seconds. This follows slow ambient drift while
preserving shorter magnetic disturbances. `EMF READ` returns only the single
metric, `EMF STATUS` also shows the current magnitude and baseline, and
`EMF ZERO` immediately resets the baseline to the current field. The EMF
stream accepts the same optional 50..60000 ms output period as the raw
magnetometer stream:

```text
EMF=37uT
OK EMF=37uT FIELD=4842uT BASELINE=4805uT
OK EMF ZERO BASELINE=4842uT
```

## UI-board Stage 1 test

The first UI-board test runs on CM7 so it can report directly over the
existing USB CDC CLI. This is temporary bring-up ownership rather than a
decision about the final M4/M7 split. Before testing, fit JP9 so the
backplane's `3V3_MCU` pull-ups for the ordinary buttons and encoder A/B
contacts are powered.

`UI STATUS` reports the unmodified GPIO levels and accumulated encoder counts.
The expected idle state is `BTN0=1`, `BTN1=1`, encoder A/B both high, and each
encoder switch (`S`) low. A pressed standalone button reads low; an encoder
pushbutton is expected to read high. For example:

```text
OK UI RAW BTN0=1 BTN1=1 ENC0=A1B1S0/0 ENC1=A1B1S0/0 ENC2=A1B1S0/0 ENC3=A1B1S0/0 WATCH=0 LEDS=IDLE MATRIX_EN=0 DISPLAY=OFF DROPPED=0
```

Start event reporting with `UI WATCH START`, then press and release each
button and rotate each encoder slowly in both directions. Pushbuttons are
debounced for 15 ms. Encoder A/B is sampled by the 1 kHz SysTick interrupt;
completed detents are handed off to the foreground USB task. The observed
phase order is reported as `DIR=CW` or `DIR=CCW`, and `STEPS` groups any
detents completed between foreground service calls:

```text
OK UI WATCH START counts-reset=1 debounce=15-ms sample=1-kHz
UI EVENT BTN0 PRESSED RAW=0
UI EVENT BTN0 RELEASED RAW=1
UI EVENT ENC0 DIR=CW STEPS=1 COUNT=1 AB=11
UI EVENT ENC0_BTN PRESSED RAW=1
```

Pay particular attention to the encoder pushbuttons. The UI-board 12 kOhm /
22 kOhm network and the backplane's additional 10 kOhm pulldown predict only
about 1.82 V at the MCU when pressed. Measure one `ENCx_BTN` net released and
pressed and do not accept this part of the test merely because a particular
prototype happens to cross the digital threshold.

`UI LEDS` drives all fourteen channels one at a time for 350 ms: the two
standalone button LEDs first, followed by the twelve encoder RGB channels. It
then restores every low-side-driver input low. Run this test only with the
correct LED series-resistor values fitted at R18 and R22. `UI OFF` aborts a
chase, stops input event reporting, disables the matrix, turns off and resets
the display, and forces all LED controls low.

`UI MATRIX PROBE` raises PB5, waits 10 ms, and probes only the IS31FL3741
default 7-bit I2C address `0x30`. It does not initialize or illuminate the
matrix. The command always returns PB5 low after the probe:

```text
OK UI MATRIX ACK address=0x30 EN-restored=0
```

`UI MATRIX ANIMATE` initializes the controller at a conservative global
current and treats physical columns 2 through 10 as a centered logical 9x9
canvas. Physical columns 0, 1, 11, and 12 remain dark. A color-changing comet
snakes through all 81 logical pixels with a dim three-pixel tail, then the
test clears the PWM registers, enters software shutdown, and returns PB5 low:

```text
OK UI MATRIX ANIMATE START logical=9x9 physical-cols=2..10 step=70-ms current=0x40
OK UI MATRIX ANIMATE PASS pixels=81 logical=9x9 blanked=1 EN=0
```

The animation takes about six seconds. `UI OFF` may be used at any time to
abort it immediately through the hardware enable pin.

## UI-board SSD1309 display test

The 2.42-inch OLED is a 128x64 SSD1309 module in 4-wire SPI mode. During this
bring-up test, firmware overrides the still-provisional CubeMX SPI6 settings
with an 8-bit, transmit-only, mode-0 configuration at 8 MHz. PA7 drives MOSI,
PG13 drives SCK, PG6 is chip select, PG7 is data/command, and PA15 is reset.

Run `UI DISPLAY TEST`. Prototype testing confirmed that this module uses a
zero-column offset. A successful transfer leaves a static test image visible
and reports:

```text
OK UI DISPLAY TEST PASS controller=SSD1309 resolution=128x64 offset=0 spi=8-MHz
```

The image has a one-pixel border, both diagonals, a center cross, a filled
square in the upper-left corner, an outline square in the upper-right, three
descending horizontal bars at lower left, and vertical bars at lower right.
Check that all four border edges are visible and that these asymmetric marks
appear in the stated corners. `UI DISPLAY TEST 0` explicitly selects the
confirmed mapping; `UI DISPLAY TEST 2` retains the alternate two-column
mapping as a diagnostic option for other SSD1309 module variants.

`UI DISPLAY OFF` sends display-off, asserts reset low, and leaves chip select
high. `UI OFF` performs the same display shutdown along with the other UI
safe-state actions.

## Three-channel radio and microphone recording test

`RECORD START [seconds]` records the same three audio tracks planned for a
session into one WAV file. The duration defaults to 60 seconds and may be
1..3600 seconds. Files are named `REC000.WAV` through `REC999.WAV`, selecting
the first unused name, and are retained on the card after a successful test.
The interleaved channel order is:

1. radio left
2. radio right
3. PDM microphone

The file is standard PCM at 48 kHz, 16 bits, and three channels: 288,000
bytes/s, or about 17.3 MB/minute. Firmware combines both input streams into
4096-frame blocks and writes 24 KiB every 85.33 ms. Each input has an
eight-block queue, providing about 683 ms of write-stall tolerance. The PDM
clock is 3.072 MHz from the common PLL3 audio clock, so the mic and radio have
the same nominal 48 kHz rate. Their initial block alignment may differ by up
to one 512-frame radio DMA half (about 10.7 ms); there is no accumulating
sample-rate drift because both are clocked from PLL3.

Start with a short functional capture while radio audio is already running:

```text
RECORD START 60
OK RECORD START file=REC000.WAV duration=60s format=48000Hz/16-bit/3ch [radio-L,radio-R,mic]
...
OK RECORD PASS file=REC000.WAV ...
RECORD DIAG queues radio=.../8 pdm=.../8 max-write=...ms peaks=...,...,...
```

The requested duration is rounded up to the next 4096-frame block, so a
60-second request produces about 60.075 seconds of audio. During recording,
`RECORD STATUS` reports progress, current queue depths, and the longest SD
write. `RECORD STOP` requests a clean stop after the next matched radio/mic
block. SD maintenance/stress commands and radio band/tuning changes are
rejected while recording is active.

With Spooky Bench 0.5.0 firmware, leave the card inserted and retrieve a finished
recording through the target CDC with:

```powershell
host/.venv/Scripts/python.exe -m spookybench --json --profile host/bench.local.json wav inspect --file REC000.WAV --timeout 600
```

The target command behind this utility is `WAV FETCH REC###.WAV`. It emits a
versioned binary stream and is intended for the bench client rather than a text
terminal. Each data frame requires an offset acknowledgement, has its own CRC32,
and contributes to a final whole-file CRC32. The target accepts only `REC###.WAV`,
rejects transfer during recording, limits files to 256 MiB, and closes the file
after ten seconds without an acknowledgement. `WAV ABORT` closes an active
transfer explicitly. Do not issue other CLI commands during the binary exchange.

The host saves the exact WAV plus SHA-256, validates RIFF sizes and PCM format,
and reports per-channel range, peak, mean, RMS, zero/clipped sample counts, and
correlations. A structurally valid file with a constant channel fails inspection.

A pass requires `OK RECORD PASS`, no DMA/write/queue-overrun abort, nonzero
peaks for every source that had an audible signal, and queue high-water marks
below 8. The important timing margin is `max-write`: it should normally be
well below the approximately 683 ms queue capacity. For the power benchmark,
measure JP8 with the meter's 10 A current input during `RECORD START 600`; a
ten-minute run also exercises filesystem growth beyond the initial 60-second
contiguous preallocation. Keep the host reading the CDC output during the run.

The retrieved artifact can be cross-checked with `ffprobe`, which should report
48,000 Hz, three channels, and signed 16-bit PCM. Individual tracks can be
extracted on a computer with:

```text
ffmpeg -i REC000.WAV -map_channel 0.0.0 radio-left.wav
ffmpeg -i REC000.WAV -map_channel 0.0.1 radio-right.wav
ffmpeg -i REC000.WAV -map_channel 0.0.2 microphone.wav
```

## SDMMC current-load test

The SD commands initialize the audio-shield card slot only when requested.
`SD STATUS` mounts the volume and reports the card type, capacity, free space,
logical block count, bus width, and active clock divider. `SD REINIT` unmounts,
deinitializes, and mounts it again. The configuration matches the passing
sibling SDMMC project: four-bit mode, hardware flow control, `ClockDiv=2` for
card initialization/mounting, and `ClockDiv=0` for file transfers.

`SD STRESS [size-MiB] [passes]` defaults to a 64 MiB, one-pass test. Each pass
overwrites `SDTEST.BIN` with a changing pseudorandom pattern in 16 KiB chunks,
syncs it to the card, reads the whole file back, and compares every byte. A
successful run deletes the test file and reports elapsed time and aggregate
read-plus-write throughput. For example:

```text
SD STRESS 256 4
OK SD STRESS START size=256MiB passes=4 file=SDTEST.BIN; do not remove card
...
OK SD STRESS PASS size=256MiB passes=4 ... file-removed=1
```

The stress command occupies the foreground loop until it finishes; interrupt-
driven USB and audio transfers continue, but periodic sensor and control
services wait. If a transfer, verification, or cleanup step fails,
`SDTEST.BIN` is retained for inspection and the next stress command refuses to
overwrite it. `SD CLEAN` removes only that fixed test file. Back up valuable
card contents first: the test does not intentionally touch other files, but an
SD-card or power failure during any filesystem write can still damage FAT
metadata.

## Charging-monitor sleep test

`SLEEP START` is a one-way low-power test entered from the USB CDC CLI. Before
running it, connect the Spooky Probe UART bridge and open its CDC port at
115200 baud so the sleep reports remain visible. The separate Battery
Babysitter USB cable may remain connected as the charging input.

The Nucleo must be powered through `5V_EXT`, and the 3.3 V regulator output
jumper must be fitted. `3V3_MCU` is not connected to `3V3_VSYS` in any jumper
configuration. JP9 connects the Nucleo's onboard 3.3 V rail only to the UI
board pull-ups; it does not join the two power rails.

On entry, firmware prints one fuel-gauge update, mutes and stops the audio DMA,
powers down and resets the radio, stops USB CDC/HSI48, makes externally-facing
push-pull audio clocks high impedance, and pulls the 3.3 V regulator enable
low. The unused CM4 is also held in WFI rather than its generated empty busy
loop. CM7 then uses hardware SLEEP with SysTick suspended. The LSE-backed RTC
wake timer performs one 10-second self-test wake and then wakes it every five
minutes; firmware briefly enables `3V3_VSYS` to restore the gauge's I2C
pull-ups, prints one charging update on AUX UART7, disables the rail,
and sleeps again.

`5V_VSYS` intentionally remains enabled because the installed Pololu module
has no enable input and that rail powers the Nucleo. `BAT_SYSOFF` also remains
inactive: asserting it would disconnect the battery from the system path and
would prevent reliable battery-powered wake-up and charging observation.
This is therefore a subsystem-rail/CPU-sleep test, not the lowest achievable
board-off state.

Expected UART output begins with:

```text
[sleep] preparing low-power charging monitor
[fuel] update: ...
[sleep] USB CDC stopped; AUX UART7 remains active
[sleep] RTC wake self-test in 10 seconds, then reports every 5 minutes
[sleep] 3V3_VSYS disabled; CM7 entering SLEEP mode
[sleep] RTC wake self-test passed; five-minute cadence armed
[fuel] update: ...
```

Press the blue Nucleo USER button or RESET to issue a system reset and return
to the normal radio/audio/USB bring-up image. Measure `3V3_VSYS` after sleep
entry to confirm it falls near 0 V; it will pulse on briefly at each five-minute
report. Also compare the fuel-gauge current before and after entry. Nucleo
debug hardware, the always-on 5 V converter, and ST-LINK remain part of that
measurement.

Successful enumeration also turns on the yellow Nucleo LED and prints this on
AUX UART7:

```text
[usb] PASS: host configured Spooky Box USB CDC CLI
```

This proves VBUS sensing, D+/D-, the Nucleo solder-link changes, USB device
clocking, enumeration, and bidirectional bulk transfers. Fuel telemetry can
confirm charging direction and current, but this test does not by itself
qualify the battery, babysitter power path, or either regulator over load and
temperature.

## Fuel-gauge test

The connected LiPo powers the battery babysitter's BQ27441-G1A gauge. CM7 uses
I2C2 on PB10/PB11 to identify
the gauge at address `0x55`, then prints detailed identification,
configuration, and telemetry at boot. Runtime output is reduced to one compact
battery update every five minutes containing voltage, temperature, average
current and power, state of charge, and remaining/full capacity. Positive
current is charging and negative current is discharging. Once USB has
successfully enumerated, normal host suspend/resume is quiet and does not
produce recurring USB diagnostic dumps.

The current bring-up image contains an idempotent, persistent configuration
step for this prototype's 3.7 V, 3700 mAh cell. When the stored capacity is not
3700 mAh or `ITPOR` is set, it preserves the complete State data block except
for Design Capacity (3700 mAh) and Design Energy (13690 mWh), verifies the old
and new block checksums and readback, exits CONFIG UPDATE using `SOFT_RESET`,
and restores the gauge's original sealed state. Default Design Capacity is
explicitly preserved. Later boots skip the write when the values are already
active and `ITPOR` is clear.

For the configuration boot, leave the LiPo and ST-LINK connected but unplug
the battery babysitter USB cable. This removes charge current during the
gauge's OCV measurement and resimulation. Reconnect the babysitter USB cable
after `[fuel] CONFIG PASS` appears. Until configuration succeeds and the gauge
has had time to learn the cell, treat voltage/current as useful live
measurements but SOC, SOH, and mAh values as preliminary.

## Build, flash, and observe

Build and flash both core images. For the Debug preset, the artifacts are:

- `build/Debug/firmware/CM7/full_spooky_proto_CM7.elf`
- `build/Debug/firmware/CM4/full_spooky_proto_CM4.elf`

The CM4 image is the generated dual-core synchronization companion; all test
logic runs on CM7.

Open the ST-LINK virtual COM port at 115200 baud, 8 data bits, no parity, one
stop bit, and no flow control. A complete run ends with one of:

```text
[summary] PASS: radio audio is routed to the headphone codec
```

or:

```text
[summary] FAIL: radio/audio stream did not start; hardware quiesced
```

Hearing clean audio establishes the prototype's radio digital-audio wiring,
native SAI2 framing, DMA bridge, codec playback path, and headphone output. It
does not establish RF sensitivity, final audio quality, storage reliability,
speaker-amplifier behavior, or final power integrity.

## Probe points if a stage fails

For an audio failure, check PF14/PF15 for idle-high I2C, then capture PE2 during
the probe and verify approximately 12.288 MHz. Recheck audio TP1, TP12/3.3 V,
TP13/1.8 V, and the shield header seating.

For an RF failure, check:

1. The RF 3.3 V rail.
2. PA8 for 32.768 kHz.
3. PA10 low at the beginning of the test, then high while streaming.
4. PB8/PB9 idle high while PA10 is low.

For a stream failure, check PD12 for approximately 48 kHz and PD13 for
approximately 1.536 MHz. PE4 and PE5 should show the corresponding SAI1 frame
and bit clocks. If streaming passes but the headphones are silent, confirm PE0
goes low when the plug is inserted; the log should print that the output was
unmuted.

For a USB failure, first check the AUX UART7 console's `[usb]` lines. With the
babysitter cable connected, PA9 should be high and the log should say
`VBUS=present`. Check that PA11/PA12 reach D-/D+ respectively, SB21/SB26/SB27/
SB29 are open, and backplane JP8 is fitted for the shared 5 V source. Do
not reconnect PA10 to USB ID: it is the RF reset bodge on this prototype.

## CubeMX findings to fix before broader bring-up

The current `.ioc` is useful as a pin map but should not be regenerated and
treated as production-ready yet:

- Most non-peripheral GPIOs have no CM7 ownership attribute, so CubeMX omitted
  their definitions and initialization. The smoke test manually owns only the
  RF reset/status pins it needs.
- SAI2 is now represented as I2S-standard, 16-bit stereo master receive with
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
- DFSDM1 now represents the proven SPK0641 path: the approximately 24.576 MHz
  PLL3 audio clock divided by 8, falling-edge Channel 0 input with a 9-bit
  right shift, and continuous Sinc4/OSR64 Filter 0 conversion. Its regular
  output uses circular word transfers on DMA2 Stream 0. CubeMX locks that DMA
  interrupt at priority 0 under the current project policy; this is safe
  because its callbacks do not call RTOS APIs, while the existing hand-written
  MSP initialization lowers it to priority 4. Both configurations produce the
  same 3.072 MHz PDM clock and 48 kHz PCM rate.
- SDMMC initializes immediately in generated code and can stop boot when a
  card/path is unavailable. It is deferred here.
- SPI6 is generated for 4-bit data. Its generated initialization remains
  deferred; `ui_board_test.c` configures the working 8-bit SSD1309 SPI path
  explicitly. Preserve that override until the `.ioc` is reconciled.
- The test explicitly reapplies the proven SAI1 48 kHz/MCLK setup before
  configuring the codec. PLL3P in the `.ioc` is now 24.576 MHz for the legal
  64 MHz PCLK2/SAI2 relationship, but the displayed CubeMX SAI timing remains
  non-authoritative.
- USB OTG FS is now represented as an M7 device-only peripheral using HSI48 at
  48 MHz. PA9 is an M7 GPIO input because firmware monitors the divided VBUS
  signal in software; PA10 remains the radio-reset bodge rather than USB ID.
  CubeMX locks the enabled OTG FS interrupt at priority 0 under the current
  project policy, while the preserved USB MSP initialization lowers it to the
  intended FreeRTOS-safe priority 6 before enabling it. HSI48 CRS calibration
  from USB2 SOF is still configured explicitly at runtime.

After the short capture is clean, run the ten-minute recording/power benchmark
and inspect the three extracted channels before extending the recorder toward
full session control and metadata.
