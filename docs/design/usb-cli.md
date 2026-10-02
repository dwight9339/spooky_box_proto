# USB CDC CLI contract

The command interface on the target's USB CDC port. It is a bring-up implementation of
the application command model in the [architecture](architecture.md#application-model):
physical controls will eventually use the same command handling and safety policy.
Wiring and device configuration are in [prototype hardware](prototype-hardware.md).
Diagnostic commands (`LOG`, `DIAG`) are specified in
[Spookyprobe v1](spookyprobe-v1.md); `IPC STATUS` in the
[diagnostic IPC contract](ipc-diagnostic-contract.md).

## Transport

The port enumerates as a CDC ACM device, distinct from the Spooky Probe's serial port.
Windows may display it as `USB Serial Device`. The baud-rate setting is ignored. On
connection it prints:

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
RECORD RESULT
RECORD LATENCY
RECORD START 60
RECORD STOP
SD STATUS
SD INFO
SD REINIT
SD STRESS 64 1
SD STRESS STOP
SD CLEAN
SD FORMAT
SD FORMAT CONFIRM
WAV FETCH REC000.WAV
WAV ABORT
UI STATUS
UI WATCH START
UI WATCH STOP
UI LEDS
UI MATRIX PROBE
UI MATRIX ANIMATE
UI DISPLAY TEST
UI DISPLAY TEST 0
UI DISPLAY TEST 2
UI DISPLAY OFF
UI OFF
SLEEP START
DIAG IDENTITY
DIAG LATENCY
```

HELP is streamed in short lines.

## Target build identity

`DIAG IDENTITY` is a bounded read-only query owned by M7. Its single response is:

```text
OK IDENTITY V=1 CORE=7 BUILD=<token> BOOT=<u32> RESET=<u32> CAPS=<u32>
```

`BUILD` is the 1..128 character ASCII token supplied as `SPOOKY_BUILD_ID` when the
paired firmware was configured. `BOOT` is a nonzero counter retained in RTC backup
registers and advanced once per M7 startup; a changed value during one bench
operation proves that the target restarted. It is not a globally unique boot ID and
may restart after loss or reset of the backup domain. `RESET` is the startup snapshot
of `RCC_RSR`, taken before firmware clears the reset flags.

`CAPS` bit 0 declares identity reporting, bit 1 the retained boot epoch, bit 2 the
diagnostic service, bit 3 WAV protocol v1, and bit 4 the opt-in IPC smoke service.
Consumers must reject unsupported schema/core values and must not infer target or
probe firmware identity from USB descriptors or package versions.

## Command admission while recording

The CLI and future physical-control commands share the recording-safe policy from
[decision 0008](../decisions/0008-recording-safe-command-policy.md). `RECORD START`
and `RECORD STOP` are admitted to the bounded M7 event queue as external commands;
if its non-reserved capacity is exhausted, the CLI replies `ERR BUSY` and changes
nothing. Session actions then run through the authoritative Session machine.

`DIAG LATENCY` streams the recording-only maximum and violation count for every
foreground service. The budget table and queue-headroom rationale are in
[foreground latency](foreground-latency.md).

While Recording or Finalizing, sleep, another recording start, SD maintenance, WAV
transfer, `EMF ZERO`, the `UI LEDS`, `UI MATRIX ANIMATE` and `UI DISPLAY TEST`
patterns, and radio tuning/band changes are rejected before their service handlers
run. Status and diagnostic reads remain available. Every policy rejection is a
numeric `COMMAND_REJECTED` diagnostic and returns one stable `ERR` line. No command
is deferred until the recording ends.

## Radio

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

The antenna path follows the band automatically. `TUNE` always accepts an integer
frequency in kHz and validates it against the current band; FM values are rounded to
the Si4735's nearest 10 kHz command unit. A successful tune returns the band, actual
frequency, RSSI, SNR, and valid-channel flag, for example:

```text
OK RADIO BAND=FM FREQ=99100 kHz (99.100 MHz) RSSI=7 SNR=2 VALID=0
```

`TUNE`, `UP`, `DOWN` and `BAND <band>` go to the Radio machine
([RadioSm](behavior/RadioSm.puml)) through the bounded M7 event queue. If the queue
refuses the command, the reply is `ERR BUSY` and nothing changes. Otherwise the reply
comes when the command's outcome is known: a tune does not hold the CLI or the
foreground loop while the receiver settles. One tune is in flight at a time, and
one newer command waits behind it. A command that arrives while another is waiting
replaces it, and the replaced command is answered `OK RADIO SUPERSEDED`, so a client
that streams targets (for example a Pd slider) gets exactly one reply per line and
no backlog builds up. Other replies are `ERR RADIO tune failed` (bus error, or no
completion within 2 s), `ERR RADIO abandoned; radio fault` for a command still
waiting when the radio faults, and `ERR RADIO audio path is not running` once the
radio is out of service. A band switch is still one synchronous step.

Band and tuning changes are rejected while recording is active. The opt-in
`RadioTuneQual` preset admits in-band tuning while recording, for the bench
qualification in `full_spooky_proto-54w.6` only.

## Volume

The wheel potentiometer is sampled every 10 ms. Firmware applies a low-pass filter and
one-dB update hysteresis, maps the knob from mute through 0 dB headphone gain, and
continues to honor the headphone jack detect. `VOLUME READ` (also accepted as
`VOLUME` or `VOLUME STATUS`) forces a fresh ADC sample and reports its current level:

```text
OK VOLUME ADC=32768 LEVEL=50% ATTEN=-26.5 dB MUTED=0
```

## Magnetometer and EMF

`MAG READ` wakes the sensor, returns one X/Y/Z result in microtesla plus raw
codes and conversion status, then puts it back to sleep. `MAG STREAM START`
starts a 100 ms stream; an optional period from 50 through 60000 ms can be
provided after `START`. `MAG STREAM STOP` stops the stream and returns the
sensor to sleep. At boot, firmware prints one sample. Example output:

```text
OK MAG STREAM START period=100 ms
MAG X=24uT Y=-48uT Z=101uT RAW=6,-12,25 SET=3 READY=1 DIAG=0
OK MAG STREAM STOP; sensor sleeping
```

The prototype `EMF` value is a magnetic anomaly metric, not electromotive force in
volts. It is the absolute difference between the 3D field magnitude and a slowly
adapting ambient baseline:

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

## UI board

These commands exercise the M7 bring-up drivers; see
[decision 0001](../decisions/0001-initial-ui-and-bus-ownership.md) for ownership.

`UI STATUS` reports the unmodified GPIO levels and accumulated encoder counts:

```text
OK UI RAW BTN0=1 BTN1=1 ENC0=A1B1S0/0 ENC1=A1B1S0/0 ENC2=A1B1S0/0 ENC3=A1B1S0/0 WATCH=0 LEDS=IDLE MATRIX_EN=0 DISPLAY=OFF DROPPED=0
```

`UI WATCH START` resets counts and reports events. Pushbuttons are debounced for
15 ms. Encoder A/B is sampled by the 1 kHz SysTick interrupt; completed detents are
handed off to the foreground USB task. The observed phase order is reported as
`DIR=CW` or `DIR=CCW`, and `STEPS` groups any detents completed between foreground
service calls:

```text
OK UI WATCH START counts-reset=1 debounce=15-ms sample=1-kHz
UI EVENT BTN0 PRESSED RAW=0
UI EVENT BTN0 RELEASED RAW=1
UI EVENT ENC0 DIR=CW STEPS=1 COUNT=1 AB=11
UI EVENT ENC0_BTN PRESSED RAW=1
```

`UI LEDS` drives all fourteen channels one at a time for 350 ms: the two
standalone button LEDs first, followed by the twelve encoder RGB channels. It
then restores every low-side-driver input low. `UI OFF` aborts a chase, stops input
event reporting, disables the matrix, turns off and resets the display, and forces
all LED controls low.

`UI MATRIX PROBE` raises PB5, waits 10 ms, and probes only the IS31FL3741
address `0x30`. It does not initialize or illuminate the matrix, and always returns
PB5 low:

```text
OK UI MATRIX ACK address=0x30 EN-restored=0
```

`UI MATRIX ANIMATE` initializes the controller at a conservative global current and
runs a color-changing comet through all 81 logical pixels with a dim three-pixel
tail, taking about six seconds. It then clears the PWM registers, enters software
shutdown, and returns PB5 low. `UI OFF` aborts it immediately through the hardware
enable pin.

```text
OK UI MATRIX ANIMATE START logical=9x9 physical-cols=2..10 step=70-ms current=0x40
OK UI MATRIX ANIMATE PASS pixels=81 logical=9x9 blanked=1 EN=0
```

`UI DISPLAY TEST` draws a static test image with the confirmed zero-column mapping
and reports:

```text
OK UI DISPLAY TEST PASS controller=SSD1309 resolution=128x64 offset=0 spi=8-MHz
```

The image has a one-pixel border, both diagonals, a center cross, a filled square in
the upper-left corner, an outline square in the upper-right, three descending
horizontal bars at lower left, and vertical bars at lower right. `UI DISPLAY TEST 0`
explicitly selects the confirmed mapping; `UI DISPLAY TEST 2` selects the alternate
two-column mapping as a diagnostic option for other SSD1309 variants.
`UI DISPLAY OFF` sends display-off, asserts reset low, and leaves chip select high.

## Recording

`RECORD START [seconds]` records radio and microphone audio into one WAV file.
Without `seconds` it is open-ended and runs until `RECORD STOP` or a storage
limit. Supplying 1..3600 seconds keeps the bounded form used by bench tests. Files are named
`REC000.WAV` through `REC999.WAV`, selecting the first unused name, and are retained
on the card. The interleaved channel order is:

1. radio left
2. radio right
3. PDM microphone

The file is standard PCM at 48 kHz, 16 bits, and three channels: 288,000 bytes/s, or
about 17.3 MB/minute. Firmware combines both input streams into 4096-frame blocks and
writes 24 KiB every 85.33 ms. Each input has an eight-block queue, providing about
683 ms of write-stall tolerance. The initial block alignment of microphone and radio
may differ by up to one 512-frame radio DMA half (about 10.7 ms). The first
60 seconds are contiguously preallocated (or the whole requested duration when it
is shorter).

At open, the recorder queries free space once and then subtracts each successful
matched-block write from that cached budget. It does not run `f_getfree` on the
recording path. Before another block would leave less than the configured reserve,
it finalizes the current valid file and reports `card full` as a fault. The reserve
defaults to one minute of three-channel audio (17,280,000 bytes), plus the configured
rolling-capture window and any additional finalization allocation. The rolling
contribution is zero until `full_spooky_proto-hpq.2` chooses and implements that
window; finalization currently rewrites the allocated header and truncates, so its
additional data-allocation contribution is zero. These three build settings are
`SPOOKY_RECORDING_CARD_RESERVE_SECONDS`,
`SPOOKY_ROLLING_CAPTURE_RESERVE_BYTES`, and
`SPOOKY_RECORDING_FINALIZE_RESERVE_BYTES`.

Until session folders are implemented, a recording also finalizes cleanly before
another block would exceed the RIFF/WAV 32-bit size limit (about 4 hours 8 minutes).

The requested duration is rounded up to the next 4096-frame block, so a 60-second
request produces about 60.075 seconds of audio:

```text
OK RECORD START file=REC000.WAV duration=open format=48000Hz/16-bit/3ch [radio-L,radio-R,mic]
...
OK RECORD PASS file=REC000.WAV ... reason=stopped
RECORD DIAG queues radio=.../8 pdm=.../8 max-write=...ms peaks=...,...,... margin=OK|LOW
```

`RECORD STATUS` reports progress, current queue depths, the longest SD write and the
storage margin.

The storage margin (decision 0014) becomes `LOW` once either audio queue's high-water
mark reaches 4 of 8 blocks or a single SD write takes 341 ms or more, half the
queue headroom. It latches for the rest of the recording, appears in `RECORD STATUS`
and `RECORD DIAG`, and records one `STORAGE_MARGIN` diagnostic event. It is a warning,
not a fault: it does not set `HAS_FAULT` or stop the recording. The thresholds are the
build settings `SPOOKY_STORAGE_MARGIN_QUEUE_BLOCKS` and `SPOOKY_STORAGE_MARGIN_WRITE_MS`
until the media survey fixes them.
`RECORD LATENCY` reports, for the current or last recording, the number of block
writes, the longest `f_write`, the longest block conversion before it, and a
histogram of `f_write` durations in 10 ms bins (0-9 ms through 60-69 ms, then
70 ms and above). Counters reset at `RECORD START`. Its last two fields,
`usb-superseded` and `usb-lost`, are cumulative since boot and describe the recorder
reply queue below.

Recorder replies make one nonblocking submission attempt and otherwise wait in a
four-line queue that is drained one line per loop pass. Only the newest
`RECORD progress` line stays queued: a newer one replaces it, and any other reply that
finds the queue full evicts it. `usb-superseded` counts progress lines removed this
way. Only when the queue holds four non-progress replies does a new reply evict the
oldest one; that counts in `usb-lost` and records a `USB_BACKPRESSURE` fault. A host
that stops reading during a recording therefore receives the outcome (`OK RECORD PASS`
or `ERR RECORD ABORT`) and `RECORD DIAG` when it resumes, after at most one stale line
already in transfer and the newest progress line. Queued replies are sent as soon as
the host polls again, so a host that discards received data when it opens the port
(pyserial on Windows calls `PurgeComm` in `open()`) can lose them. Such a host
recovers the outcome with `RECORD RESULT`.

`RECORD RESULT` is a read-only query, accepted in every session state, that reports
the last finished recording since boot:

```text
OK RECORD RESULT NONE
OK RECORD RESULT seq=2 outcome=PASS file=REC075.WAV frames=286720 bytes=1720320 elapsed=6025ms finalized=1 reason=stopped
```

`seq` counts finished recordings since boot. `outcome` is `PASS` exactly when the
pushed line was `OK RECORD PASS`; otherwise it is `ABORT`, including a clean stop
whose finalization failed. `reason` is last and may contain spaces. The record is
written before the pushed outcome line, so it is never older than that line. While a
recording is active the reply still describes the previous one; a host that started
a recording matches `file` (and `seq`) before treating the result as its own.
`RECORD STOP` requests a clean stop after the next matched radio/mic block.
SD maintenance and stress commands, WAV transfer, and radio band/tuning changes are
rejected while recording is active. `SD STATUS` remains nonintrusive: while the
recorder owns the volume it reports card presence, owner, active file and written
frames and cached free/reserve MiB from recorder state rather than mounting or
querying the filesystem.

## WAV transfer (protocol v1)

`WAV FETCH REC###.WAV` emits a versioned binary stream intended for the bench client
rather than a text terminal. Each data frame requires an offset acknowledgement, has
its own CRC32, and contributes to a final whole-file CRC32. The target accepts only
`REC###.WAV`, rejects transfer during recording, limits files to 256 MiB, and closes
the file after ten seconds without an acknowledgement. `WAV ABORT` closes an active
transfer explicitly. No other CLI command may be issued during the binary exchange.
The host side is described in the
[WAV inspection procedure](../procedures/spooky-bench-wav-inspection.md).

## SD card

The SD commands initialize the audio-shield card slot only when requested.
Recorder, SD stress/status, and WAV transfer operations serialize through one
exclusive storage owner lease. A busy client is rejected without stopping or
unmounting the current owner.
`SD STATUS` mounts the volume and reports the card type, capacity, free space,
logical block count, bus width, and active clock divider. `SD REINIT` unmounts,
deinitializes, and mounts it again. The configuration uses four-bit mode, hardware
flow control, and `ClockDiv=2` (75 MHz / 4 = 18.75 MHz) for mounting and transfers.
The card is never switched to high speed, so the bus stays within the 25 MHz
default-speed limit. `ClockDiv=0` bypasses the divider and runs the bus at 75 MHz;
a 2 GB SDSC card fails data CRC there.

Card initialization is bounded. The SD power-up handshake (ACMD41) is tried at most
2000 times, about 1.2 s, slightly more than the SD 1 s power-up allowance. After that,
the card must reach the transfer state within 1 s. A card that fails either step fails
the mount in about 1.2 s instead of blocking the foreground for about 38 s. The working
cards tested mount within about 300 ms. A failed mount, `SD INFO`, `SD FORMAT` or
`RECORD START` reports the failing step, preserved before the handle is deinitialized:

```text
ERR SD mount failed result=FR_NOT_READY(3) stage=HAL_INIT hal=0x01000000 state=0 init_ms=1156 ready_ms=0
```

`stage` is `HAL_INIT` (identification or power-up failed; `hal=0x01000000` is the HAL's
invalid-voltage-range error, a card that never finished powering up), `NOT_READY` (it
never reached the transfer state; `state` is the last card state), `NO_CARD` or `OK`
(the card initialized and the failure is the filesystem itself, such as
`FR_NO_FILESYSTEM`).
`SD STRESS [size-MiB] [passes]` defaults to a 64 MiB, one-pass test. Each pass
overwrites `SDTEST.BIN` with a changing pseudorandom pattern in 16 KiB chunks,
syncs it to the card, reads the whole file back, and compares every byte. A
successful run deletes the test file and reports elapsed time and aggregate
read-plus-write throughput, exact written/verified byte counts, and maximum
individual write/read call times:

```text
SD STRESS 256 4
OK SD STRESS START size=256MiB passes=4 file=SDTEST.BIN chunk=16384; do not remove card
...
OK SD STRESS PASS size=256MiB passes=4 written=1073741824 verified=1073741824 ... file-removed=1
```

The test advances by one 16 KiB filesystem call from `SdTest_Service()` on each
foreground iteration; USB,
IPC, audio, sensor, and control services continue between chunks. `SD STRESS STOP`
closes and removes an in-progress scratch file and reports the bytes written and
verified before cancellation. Recording start and WAV transfer are rejected while the
test owns the card. If a transfer, verification, or cleanup step fails, `SDTEST.BIN`
is retained for inspection and the next stress command refuses to overwrite it.
`SD CLEAN` removes only that fixed test file.

`SD INFO` reads the card's identity and ratings without mounting a filesystem, so it
also works on a card with no readable volume. It reports the card type, whether the
recorder supports it (decision 0014: SDHC/SDXC only), capacity, the CID fields
(manufacturer ID, OEM ID, product name and revision, serial number, manufacturing
date) and the SD Status ratings (speed class, UHS speed grade, video speed class,
allocation unit). Ratings are recorded as evidence; they do not bound write stalls.
Command policy rejects it while recording.

```text
OK SD INFO TYPE=SDHC/SDXC SUPPORTED=1 CAPACITY=15193MiB MID=0x03 OID=SD PNM=SC16G PRV=8.0 PSN=0x934591FE MDT=2022-09 SPEED_CLASS=10 UHS_GRADE=0 VIDEO_CLASS=0 AU=4096KiB
```

`RECORD START` refuses an unsupported card before creating a file:
`ERR RECORD unsupported card TYPE=SDSC; SDHC/SDXC required`. Reading, transfer and
maintenance commands still work on such a card.

`SD FORMAT` provides an optional in-device format; cards formatted elsewhere remain
usable. It is two-step: `SD FORMAT` arms it, and `SD FORMAT CONFIRM` within 10 s
erases every file on the card. A confirm that is unarmed or late is refused, and
consumes the arm. Command policy rejects both while recording or finalizing. The
storage service formats under its own exclusive owner (`FORMAT`) and does not need
a readable filesystem, so a card with a damaged or foreign layout can be
formatted. Only SDHC/SDXC cards are formatted; SDSC is refused before anything is
written.

The layout is one FAT32 volume with 32 KiB clusters and a single FAT. FatFs R0.12c
places the partition at sector 63, and aligns the data area to the card's
allocation unit read from its SD Status register (`ALIGN_SECTORS`; 1 when the card
reports none). The service then mounts the new volume once to verify it. The format
blocks the foreground for its whole duration, about 2 s for a 16 GB card, and runs
only from idle maintenance:

```text
SD FORMAT
OK SD FORMAT armed; erases every file on the card. Send SD FORMAT CONFIRM within 10 s
SD FORMAT CONFIRM
OK SD FORMAT started
OK SD FORMAT FAT32 CLUSTER=32768 ALIGN_SECTORS=8192 SECTORS=31116288 FREE=15189MiB MS=1998
```

Failures report `ERR SD FORMAT unsupported card TYPE=SDSC; SDHC/SDXC required` or
`ERR SD FORMAT failed result=<FRESULT>(<n>) hal=0x<error> MS=<n>`.

## Low-power charging monitor

`SLEEP START` is a one-way low-power state entered from the CLI; it is rejected in IPC
experiment builds and while the Session state is Recording or Finalizing. An active
session receives `ERR SLEEP unavailable while recording` and is unchanged. On entry,
firmware prints one fuel-gauge update, mutes and stops
the audio DMA, powers down and resets the radio, stops USB CDC/HSI48, makes
externally-facing push-pull audio clocks high impedance, and pulls the 3.3 V
regulator enable low. CM4 is held in WFI. CM7 then uses hardware SLEEP with SysTick
suspended. The LSE-backed RTC wake timer performs one 10-second self-test wake and
then wakes every five minutes; firmware briefly enables `3V3_VSYS` to restore the
gauge's I2C pull-ups, prints one charging update on UART7, disables the rail, and
sleeps again. The blue USER button or RESET returns to the normal image through a
system reset. UART output begins with:

```text
[sleep] preparing low-power charging monitor
[fuel] update: ...
[sleep] USB CDC stopped; AUX UART7 remains active
[sleep] RTC wake self-test in 10 seconds, then reports every 5 minutes
[sleep] 3V3_VSYS disabled; CM7 entering SLEEP mode
[sleep] wake report=1 wakes=1 last-drain=ok log-errors=0 log-dropped=0
[sleep] RTC wake self-test passed; five-minute cadence armed
[fuel] update: ...
```

Each RTC report begins with a `wake` line. `wakes` counts every return from WFI since
sleep entry, so it should equal `report` unless another interrupt woke the core;
`last-drain` says whether the UART7 logger emptied before the previous sleep entry
(`aborted` means its remaining bytes were discarded and are counted in
`log-dropped`).

## Fuel-gauge reporting

At boot, firmware prints detailed gauge identification, configuration, and telemetry
on UART7. Runtime output is one compact battery update every five minutes containing
voltage, temperature, average current and power, state of charge, and
remaining/full capacity. Positive current is charging and negative current is
discharging. After USB has enumerated, host suspend/resume is quiet. Until the
gauge's configuration succeeds and it has learned the cell, SOC, SOH, and mAh values
are preliminary.

## Startup console

On reset, UART7 begins with:

```text
[uart] Spooky Probe console on UART7: PE8 TX, PE7 RX, 115200 8N1
```

Successful USB enumeration prints `[usb] PASS: host configured Spooky Box USB CDC CLI`.
A complete audio start ends with one of:

```text
[summary] PASS: radio audio is routed to the headphone codec
[summary] FAIL: radio/audio stream did not start; hardware quiesced
```

## Client notes

This line protocol is also the simplest Pure Data control path. Install the
`comport` serial external using Pd's **Help > Find externals**, open the Spooky
Box COM port, and send the ASCII bytes for `TUNE <kHz>` followed by byte 10. A Pd
slider can map 0..1 to 87500..108000, round the result, prepend `TUNE`, and append
the newline. USB MIDI or OSC can be added later if their ecosystem advantages
outweigh the extra USB-class or network framing work.
