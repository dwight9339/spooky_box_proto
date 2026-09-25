# Spooky Box prototype bring-up procedure

Checks for powering, flashing and exercising the prototype. This document holds the
steps and pass criteria only:

- Installed hardware, bodges, wiring and clocks:
  [prototype hardware](docs/design/prototype-hardware.md).
- Command syntax and responses: [USB CLI contract](docs/design/usb-cli.md).
- Known `.ioc` discrepancies: [CubeMX register](docs/design/cubemx-reconciliation.md).
- Opt-in dual-core experiment: [IPC smoke test](docs/procedures/ipc-smoke-test.md).
  Normal builds keep M4 asleep; `IPC STATUS` reports whether the experiment is enabled.
- Automated equivalents of several checks: [Spooky Bench](docs/procedures/spooky-bench-setup.md).

Record results as dated files in `docs/evidence/`. Qualification status is tracked in
Beads (`full_spooky_proto-jjy`).

## Unpowered checks

Disconnect the Spooky Probe USB, the battery babysitter USB and any other cables
before resistance/continuity checks.

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

Apply the intended external 5 V source with JP8 fitted; the Spooky Probe may also be
connected for flashing and UART capture. Before flashing the test, verify these
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

## Build, flash, and observe

Build and flash both core images from the same preset (see the
[README](README.md#build-and-flash)). For the Debug preset, the artifacts are:

- `build/Debug/firmware/CM7/full_spooky_proto_CM7.elf`
- `build/Debug/firmware/CM4/full_spooky_proto_CM4.elf`

The CM4 image is the generated dual-core synchronization companion; all test
logic runs on CM7.

Open the Spooky Probe UART at 115200 baud, 8 data bits, no parity, one stop bit,
and no flow control. On reset it must begin with:

```text
[uart] Spooky Probe console on UART7: PE8 TX, PE7 RX, 115200 8N1
```

A complete run ends with one of:

```text
[summary] PASS: radio audio is routed to the headphone codec
```

or:

```text
[summary] FAIL: radio/audio stream did not start; hardware quiesced
```

Check the Nucleo LEDs against the [status LED meanings](docs/design/prototype-hardware.md#status-leds).
Hearing clean audio establishes the prototype's radio digital-audio wiring,
native SAI2 framing, DMA bridge, codec playback path, and headphone output. It
does not establish RF sensitivity, final audio quality, storage reliability,
speaker-amplifier behavior, or final power integrity.

## USB CDC enumeration test

1. Keep backplane JP8 fitted and ensure the external 5 V source can supply the
   combined backplane, Nucleo, and shield load.
2. With the babysitter USB cable disconnected, select USB500 (switch 1 OFF,
   switch 2 ON).
3. Connect the babysitter USB connector through a powered hub or a port known to
   support 500 mA, using a data-capable cable.
4. Confirm Windows creates a second COM port, distinct from the Spooky Probe's serial
   port, and that opening it prints the CLI banner.

Pass: the yellow Nucleo LED turns on and UART7 prints:

```text
[usb] PASS: host configured Spooky Box USB CDC CLI
```

This proves VBUS sensing, D+/D-, the Nucleo solder-link changes, USB device
clocking, enumeration, and bidirectional bulk transfers. Fuel telemetry can
confirm charging direction and current, but this test does not by itself
qualify the battery, babysitter power path, or either regulator over load and
temperature.

## UI-board Stage 1 test

1. Fit JP9 so the backplane's `3V3_MCU` pull-ups for the buttons and encoder A/B
   contacts are powered.
2. Run `UI STATUS` and confirm the idle levels listed in
   [prototype hardware](docs/design/prototype-hardware.md#ui-board).
3. Run `UI WATCH START`, then press and release each button and rotate each encoder
   slowly in both directions. Require a press and release event for every button
   and `DIR=CW`/`DIR=CCW` events for each encoder in the matching direction.
4. Measure one `ENCx_BTN` net released and pressed. Do not accept the encoder
   pushbuttons merely because this prototype happens to cross the digital threshold
   (see the known margin issue in the hardware document).
5. Only with the correct LED series-resistor values fitted at R18 and R22, run
   `UI LEDS` and confirm each of the fourteen channels lights in turn.
6. Run `UI MATRIX PROBE`; require `OK UI MATRIX ACK address=0x30 EN-restored=0`.
7. Run `UI MATRIX ANIMATE`; confirm the comet visits all 81 logical pixels with
   columns 0, 1, 11 and 12 dark, and require `OK UI MATRIX ANIMATE PASS`.

`UI OFF` returns all UI hardware to a safe state at any point.

## UI-board SSD1309 display test

Run `UI DISPLAY TEST` and require the `OK UI DISPLAY TEST PASS` line. Check that
all four border edges of the test image are visible and that its asymmetric marks
appear in the corners stated in the
[CLI contract](docs/design/usb-cli.md#ui-board). Finish with `UI DISPLAY OFF` or
`UI OFF`.

## Three-channel radio and microphone recording test

Start with a short functional capture while radio audio is already running:

```text
RECORD START 60
```

Retrieve the finished file with the card left inserted, using firmware that
implements WAV transfer protocol v1:

```powershell
host/.venv/Scripts/python.exe -m spookybench --json --profile host/bench.local.json wav inspect --file REC000.WAV --timeout 600
```

The host saves the exact WAV plus SHA-256, validates RIFF sizes and PCM format,
and reports per-channel range, peak, mean, RMS, zero/clipped sample counts, and
correlations. A structurally valid file with a constant channel fails inspection.

A pass requires `OK RECORD PASS`, no DMA/write/queue-overrun abort, nonzero
peaks for every source that had an audible signal, and queue high-water marks
below 8. The important timing margin is `max-write`: it should normally be
well below the approximately 683 ms queue capacity.

Only after the short capture passes, run the ten-minute recording/power
benchmark: measure JP8 with the meter's 10 A current input during
`RECORD START 600`. A ten-minute run also exercises filesystem growth beyond the
initial 60-second contiguous preallocation. Keep the host reading the CDC output
during the run, then inspect the three extracted channels.

The retrieved artifact can be cross-checked with `ffprobe`, which should report
48,000 Hz, three channels, and signed 16-bit PCM. Individual tracks can be
extracted on a computer with:

```text
ffmpeg -i REC000.WAV -map_channel 0.0.0 radio-left.wav
ffmpeg -i REC000.WAV -map_channel 0.0.1 radio-right.wav
ffmpeg -i REC000.WAV -map_channel 0.0.2 microphone.wav
```

## SDMMC current-load test

Back up valuable card contents first: the test does not intentionally touch other
files, but an SD-card or power failure during any filesystem write can still damage
FAT metadata.

1. Run `SD STATUS` and record the card type, capacity and free space.
2. Run `SD STRESS [size-MiB] [passes]`, for example `SD STRESS 256 4`, and do not
   remove the card.
3. Require `OK SD STRESS PASS` with written and verified byte counts equal and
   `file-removed=1`. Record throughput and maximum write/read call times.

If the test fails, `SDTEST.BIN` is retained for inspection; remove it with
`SD CLEAN` before the next run. `SD STRESS STOP` cancels a run in progress. The
automated bounded variant is the [SD basic procedure](docs/procedures/spooky-bench-sd-basic.md).

## Charging-monitor sleep test

1. Connect the Spooky Probe UART bridge and open its port at 115200 baud so the
   sleep reports remain visible. The Battery Babysitter USB cable may remain
   connected as the charging input.
2. Confirm the Nucleo is powered through `5V_EXT` and the 3.3 V regulator output
   jumper is fitted (see [low-power topology](docs/design/prototype-hardware.md#low-power-topology)).
3. Run `SLEEP START` from the USB CLI with a normal (non-IPC) build.
4. Require the [expected UART sequence](docs/design/usb-cli.md#low-power-charging-monitor),
   including `RTC wake self-test passed`.
5. Measure `3V3_VSYS` after sleep entry and confirm it falls near 0 V; it pulses
   on briefly at each five-minute report. Compare the fuel-gauge current before
   and after entry. The Nucleo's on-board debug hardware, the always-on 5 V
   converter, and the connected Spooky Probe remain part of that measurement.
6. Press the blue Nucleo USER button or RESET to return to the normal image.

## Fuel-gauge configuration boot

Use this procedure the first time the gauge must learn this prototype's cell (see
[prototype hardware](docs/design/prototype-hardware.md#i2c2-devices)):

1. Leave the LiPo and the Spooky Probe connected but unplug the battery babysitter
   USB cable.
   This removes charge current during the gauge's OCV measurement and resimulation.
2. Boot and wait for `[fuel] CONFIG PASS` on UART7.
3. Reconnect the babysitter USB cable.

Until configuration succeeds and the gauge has had time to learn the cell, treat
voltage/current as useful live measurements but SOC, SOH, and mAh values as
preliminary.

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

For a USB failure, first check the UART7 console's `[usb]` lines. With the
babysitter cable connected, PA9 should be high and the log should say
`VBUS=present`. Check that PA11/PA12 reach D-/D+ respectively, SB21/SB26/SB27/
SB29 are open, and backplane JP8 is fitted for the shared 5 V source. Do
not reconnect PA10 to USB ID: it is the RF reset bodge on this prototype.
