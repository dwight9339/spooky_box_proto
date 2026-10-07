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
CLASSIC
CLASSIC RUN
CLASSIC PAUSE
CLASSIC TOGGLE
CLASSIC DIR
CLASSIC RATE +1
CLASSIC DIST -2
CLASSIC EDGE 1
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
RECORD TIMELINE
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
UI MATRIX ORIENT
UI MATRIX FEEDBACK
UI MATRIX FEEDBACK ON
UI MATRIX FEEDBACK OFF
UI MATRIX TRAIL ON
UI MATRIX TRAIL OFF
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

## Command input and flow control

Commands are lines of at most 63 characters, ended by CR or LF. The firmware handles
one line per foreground pass, and only once the previous reply has gone out. Received
bytes wait in a 256-byte ring. The CDC OUT endpoint is re-armed only while another
full 64-byte packet fits in it; otherwise the device NAKs the host, which holds its
data until the firmware has drained the ring (`full_spooky_proto-8lw.24`). A client
that sends faster than the firmware answers is slowed down instead of losing bytes, so
every complete line it sends gets exactly one reply. A line longer than 63 characters is
answered `ERR command too long`. While the endpoint is paused, the host retries
continuously. The HAL enables an interrupt for every NAKed OUT token; the firmware masks
it when the interface is configured, so a pause costs no CPU. Unmasked, those
interrupts starved a recording into an overrun during a command flood
([evidence](../evidence/2026-10-02-usb-flow-hostless-battery.md)).

`DIAG USB` reports the receive counters since the last enumeration:

```text
OK DIAG USB RX_PACKETS=... RX_PAUSES=... RX_OVERRUNS=0 RX_QUEUED=... RX_PAUSED=0|1
```

`RX_PAUSES` counts how often flow control held the host back. `RX_OVERRUNS` counts
packets that did not fit and were truncated; it stays zero unless flow control fails.

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

While Preparing, Recording or Finalizing, sleep, another recording start, SD maintenance, WAV
transfer, `EMF ZERO`, the `UI LEDS`, `UI MATRIX ANIMATE`, `UI MATRIX ORIENT`,
`UI MATRIX FEEDBACK ON` and `UI DISPLAY TEST` patterns, and radio tuning/band changes are rejected before their service handlers
run. Status and diagnostic reads and `CLASSIC` parameter commands remain available. Every policy rejection is a
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

The radio is also tuned by the Classic scan engine, which runs from power-on (next
section). Band and frequency are shared: a CLI `TUNE`, `UP`, `DOWN` or `BAND` moves
them, and a running Classic continues from wherever the CLI tuned. Classic waits for
every CLI radio command to be answered before it computes its next jump. A bench step
that needs a fixed frequency sends `CLASSIC PAUSE` first.

## Classic scan engine

The device boots with the Classic engine running on FM
([spec 001](../../spec/specs/001-classic-scan-engine/spec.md),
[decision 0016](../decisions/0016-classic-scan-motion.md)). Until physical controls
are wired, the CLI issues the same Classic commands as the controls of the control
map, through the bounded M7 event queue:

| Command | Effect | Control |
|---|---|---|
| `CLASSIC` | Report the published state | |
| `CLASSIC TOGGLE` | Run or pause | C-103 |
| `CLASSIC RUN`, `CLASSIC PAUSE` | Run or pause; no change if already in that state | |
| `CLASSIC DIR` | Toggle the scan direction | C-105 |
| `CLASSIC RATE <n>` | Move the jump rate `n` detents (signed) | C-104 |
| `CLASSIC DIST <n>` | Move the current band's jump distance `n` detents | C-106 |
| `CLASSIC EDGE <n>` | Move the edge behavior `n` detents through wrap, bounce, stop | C-110 |
| `CLASSIC HOLD <n>` | Move the activity hold time `n` detents through 0 (off), 1, 2, 3, 5, 8, 10, 15, 20 and 30 s | C-111 |
| `CLASSIC ACTIVITY` | Report the radio onset measurement behind the hold (bench diagnostic) | |

Each command is answered after it is applied, with the state, and `CLASSIC` reports
the same line:

```text
OK CLASSIC STATE=RUNNING REASON=NONE BAND=FM FREQ=99100 CH=116/206 DIR=UP RATE=120 SET=120 LIMITED=0 DIST=1 DIST_KHZ=100 EDGE=WRAP HOLD=0 JUMPS=0 REFUSED=0 FAILED=0 HOLDS=0
```

`STATE` is `RUNNING`, `HOLDING`, `PAUSED`, `SWEEP_COMPLETE` or `UNABLE`. `UNABLE`
replaces `RUNNING` and `HOLDING` while Classic cannot retune, and `REASON` says why:
`RADIO_STOPPED`, `RADIO_FAULTED`, or `SESSION` while the command policy rejects tuning
during a session. Classic then issues nothing and does not retry; it resumes one jump
period after tuning is possible again. `RATE` is the rate in effect and `SET` the
setting; `LIMITED=1` means the band's maximum rate applies. `FREQ` and `CH` are the
last completed tune. `HOLD` is the hold time in seconds, 0 when off. `JUMPS` counts
jumps, `REFUSED` tune commands the queue refused (the landing waits one period),
`FAILED` tunes answered as failed, rejected, superseded or abandoned, and `HOLDS`
holds started. A full queue answers `ERR BUSY` and malformed arguments the usage
line. Classic commands are allowed during a session.

`HOLDING` means Classic is staying on its landing because the radio audio has onsets
(decision 0016 items 17 to 20): a hold starts on an onset of at least medium size,
lasts while onsets of any size follow within 1.5 s, ends at the hold time at most,
and happens at most once per landing. A hold never shortens a dwell: when it ends
before the next jump is due, the jump keeps its time. Onsets come from the decision
0013 detector, fed one level per radio half-buffer; from each tune or band switch
until one half-buffer after the radio is seen settled, blocks are not measured
([decision 0015](../decisions/0015-raw-radio-track-during-in-band-tunes.md)), so no
hold starts there. `CLASSIC ACTIVITY` replies:

```text
OK CLASSIC ACTIVITY VALID=1 RETUNING=0 RATIO_X100=104 ONSETS=12/3/1 BLOCKS=56230 MEASURED=55120 DROPPED=0 HIGH=5 RETUNES=310
```

`VALID=1` means the latest radio block was measured and arrived within 100 ms.
`RETUNING=1` while a retune interval is open. `RATIO_X100` is the detector's
fast/slow level ratio times 100. `ONSETS` counts small, medium and large onsets since
boot. `BLOCKS` and `MEASURED` count half-buffers given to the detector and those
measured. `DROPPED` counts half-buffers the 32-entry feed refused because the
foreground had not drained it, and `HIGH` is the most waiting at once. `RETUNES`
counts retune intervals started.

Every change of run state (with its reason), direction (including a bounce reversal),
rate, distance, edge behavior or hold time is published as one line on the AUX UART
log, with the millisecond tick and the radio sample-timeline position as `epoch:frame`
([decision 0012](../decisions/0012-common-audio-sample-timeline.md)):

```text
[classic] t=48211 pos=1:2312448 DIRECTION STATE=RUNNING REASON=NONE DIR=DOWN RATE=120/120 LIMITED=0 DIST=1 EDGE=BOUNCE HOLD=0 BAND=FM FREQ=107900
```

Routine jumps are not events; each completed tune is logged by the radio as usual. A
hold is two `RUN_STATE` lines, `STATE=HOLDING` and then `STATE=RUNNING` (or
`PAUSED`), whose ticks give its length. Each radio onset the Classic service receives
is also logged, with the frequency of the last completed tune, whether or not the
hold is on, so a bench trial can see where onsets fall (decision 0016 item 23):

```text
[activity] t=27347 ONSET=SMALL BAND=FM FREQ=89200
```

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
time constant of roughly 80 seconds, but only while the field is quiet: when
the change from baseline is 100 µT or more the update is skipped
([decision 0019](../decisions/0019-matrix-emf-four-buckets-and-quiet-baseline.md)).
This follows slow ambient drift, and a magnet held near the sensor cannot drag
the baseline and leave a false reading once it is taken away. A lasting change
of 100 µT or more stays shown until `EMF ZERO`. `EMF READ` returns only the single
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

`UI MATRIX ORIENT` initializes the controller and lights three logical corners: red
at top-left (0,0), green at top-right (8,0) and blue at bottom-left (0,8). It shows
how the matrix is mounted; `UI OFF` clears it. The matrix on the bench UI board is
mounted rotated 180 degrees from the original panel mapping, and the M7 matrix writer
corrects for it, so every logical coordinate appears the right way up.

```text
OK UI MATRIX ORIENT red=(0,0) green=(8,0) blue=(0,8); UI OFF clears
```

### Matrix feedback

`UI MATRIX FEEDBACK ON` starts the M7 matrix feedback of
[decision 0013](../decisions/0013-matrix-emf-radio-and-status-mapping.md), with the
EMF buckets, baseline and trail default of
[decision 0019](../decisions/0019-matrix-emf-four-buckets-and-quiet-baseline.md):
the expanding square coloured by the EMF bucket, row kicks on radio onsets, and the
recording border. Feedback is off at boot. It holds the matrix, so `UI MATRIX PROBE`,
`ANIMATE` and `ORIENT` answer `ERR UI MATRIX busy ... feedback=1` until
`UI MATRIX FEEDBACK OFF` or `UI OFF` releases it. While it runs, the magnetometer
samples every 100 ms for the EMF level.

The writer compares each frame with what the matrix shows and sends only the rows
that changed, as I2C2 register runs (24 bytes for columns 0 to 7 and 3 bytes for
column 8 of a row). A foreground pass keeps sending runs until 2 ms have been spent,
so one pass can send several. I2C2 runs in fast mode (about 360 kHz) for this; see the
[CubeMX register](cubemx-reconciliation.md).

On normal images the matrix goes dark while the recorder captures: the adapter turns
it off through the enable pin, with no I2C2 traffic, and writes the whole frame again
when the capture ends. The opt-in qualification build
(`-DSPOOKY_MATRIX_RECORDING_QUALIFICATION=ON`) keeps the matrix writing during a
capture, so the recording border shows, and starts the feedback at boot so a
`test recording-regression` run exercises it. The magnetometer is still not read
during a capture, so in that build the outline turns dim grey while recording.

`UI MATRIX FEEDBACK` alone reports the status. The `ON`, `OFF` and `TRAIL` commands
answer with the same line:

```text
OK UI MATRIX FEEDBACK=1 TRAIL=0 SUSPENDED=0 EMF=VALID BUCKET=0 EMF_UT=21 FRAMES=132 RUNS=1261 FAILED=0 SUPERSEDED=0 DROPPED_STEPS=0 PENDING=0 RUN_US_MAX=1756 SUSPENSIONS=0
```

| Field | Meaning |
| --- | --- |
| `FEEDBACK`, `TRAIL` | Feedback running; trail on (seven-step loop) or off (six steps) |
| `SUSPENDED`, `SUSPENSIONS` | Matrix dark for a capture now; captures since feedback started |
| `EMF`, `BUCKET`, `EMF_UT` | EMF level state (`VALID`, `NO_SAMPLE`, `NO_BASELINE`, `STALE`, `SENSOR_FAULT`), its bucket, and the newest change from baseline |
| `FRAMES`, `SUPERSEDED` | Frames composed; frames replaced before all their runs were written |
| `RUNS`, `FAILED`, `PENDING` | Register runs written, failed, and still to write for the current frame |
| `DROPPED_STEPS` | Animation steps skipped to keep the tempo, including the catch-up after a capture |
| `RUN_US_MAX` | Longest single run, timed with the cycle counter |

`UI MATRIX TRAIL ON` and `OFF` choose the trail, which is off by default; the setting is
kept while feedback stops and starts. Three consecutive failed runs stop the feedback
with `[matrix] feedback off: I2C2 writes failed`.

### Demo image (demo branch only)

The `Demo` preset on `demo/halloween-2026` boots into Field with the physical controls
resolved on the M7 ([decision 0011](../decisions/0011-halloween-2026-demo-build.md)
item 12, `full_spooky_proto-p04.3`). This is demo-only behavior and not proven product
behavior. The OLED, the button and encoder lights and the matrix render published
state. Matrix feedback starts at boot and keeps writing during a capture as in the
qualification build, but at most one run per pass while capturing. The `UI` test
commands still exist; they share the surfaces with the demo and are for bring-up
only.

`DEMO` or `DEMO STATUS` reports the demo's counters:

```text
OK DEMO SCREEN=CLASSIC INP=0 SHIFT=0 INPUTS=42 REFUSED=0 GESTURES=30/0 UNBOUND=0 RECONCILES=0 TICKS=12 CMD_REJECTED=0 CMD_REFUSED=0 QUEUE_HIGH=4 OLED=1 FRAMES=900 PAGES=310 PAGE_FAIL=0 PAGE_US_MAX=180 LIGHTS=1 MATRIX_REC_MS=20040 MATRIX_REC_FRAMES=190 MATRIX_REC_SUPERSEDED=3
```

| Field | Meaning |
| --- | --- |
| `SCREEN`, `INP`, `SHIFT` | What has the controls; the InputResolution state number; Shift held |
| `INPUTS`, `REFUSED` | Switch edges and detent counts the queue admitted and refused (a refusal reconciles) |
| `GESTURES` | Gestures posted to the Context machine, and those the queue refused |
| `UNBOUND`, `RECONCILES`, `TICKS` | Gestures with no binding; reconcile events; threshold and timeout ticks |
| `CMD_REJECTED`, `CMD_REFUSED` | Commands the policy rejected (shown on the display), and those the queue refused |
| `QUEUE_HIGH` | Event queue high-water mark |
| `OLED`, `FRAMES`, `PAGES`, `PAGE_FAIL`, `PAGE_US_MAX` | Display started; frames composed (every 50 ms, 100 ms while capturing); 128-byte pages written and failed; longest page write |
| `LIGHTS` | Button LED PWM (TIM16, TIM17) started |
| `MATRIX_REC_MS`, `MATRIX_REC_FRAMES`, `MATRIX_REC_SUPERSEDED` | Time the matrix ran while capturing, frames composed in it, and frames replaced before fully written: the matrix frame rate under capture |

`DEMO LIGHTS` reports the button LEDs' idle level, and `DEMO LIGHTS IDLE <0-1000>`
sets it, in permille of perceived lightness (CIE L*). The reply gives the level and
its PWM duty in permille; a pressed button is always at full duty, and Button 0
breathes from off up to a fixed peak during a session. The level resets to 400 at
boot.

```text
OK DEMO LIGHTS IDLE=400 DUTY=113 PRESSED_DUTY=1000
```

The demo image lifts the in-band tune guard, so Classic keeps scanning during a session;
`TUNE`, `UP` and `DOWN` are accepted while recording there too. Band changes stay
rejected during a session. Its bench qualification is demo-image evidence only
(`full_spooky_proto-p04.4`). `DEMO TUNES` reports how long Classic's tunes take, from the
Radio machine's tune start to its answer, per band, split by whether the recorder was
capturing when the tune started. `FAILED` counts tunes that were issued and then
failed; `ISSUE_FAILED` counts tunes the receiver would not accept, which the Radio
machine answers as failed without starting them. `DEMO TUNES RESET` clears it.

```text
DEMO TUNES BAND=FM CAPTURING=1 TUNES=401 FAILED=0 ISSUE_FAILED=0 MEAN_US=28394 MAX_US=86478
OK DEMO TUNES END
```

The demo image keeps a rolling window of the last minute outside sessions
(`full_spooky_proto-p04.5`, decision 0011 item 14, after decision 0010's shape).

- **Where it writes.** The recorder streams its three-channel blocks into thirteen
  reused 5.46 s WAV segments, `ROLL/SLOT00.WAV` to `SLOT12.WAV`. It always holds at least
  the newest 704 blocks (60.07 s).
- **Saving.** Shift plus Button 1 (C-009), or `ROLL SAVE`, saves the window. At the next
  block boundary it closes the segment and pins the newest segments that cover the
  window. The stream continues in a free segment without a gap. The pinned segments
  are renamed into `CAPS/C001S00.WAV`, `C001S01.WAV` and so on, and the descriptor
  `CAPS/C001.TXT` is written last. A capture is saved only once that descriptor is
  committed.
- **Outcomes.** A save is writing, saved, busy (one save at a time), unavailable (no
  window held) or failed. The display and the `ROLL` reply show the outcome.
- **The window after a save.** A save moves its segments out of the ring, so the next
  window starts from the save point.
- **During a session.** A session turns rolling capture off, and a save during a session
  is rejected with its reason. Rolling starts again, with an empty window, when the
  session ends.
- **Sensor reads and the matrix.** EMF and the fuel gauge are read while the rolling
  stream runs, and the matrix keeps its normal write budget. In the demo image their
  guards follow session capture only.

`ROLL` (or `ROLL STATUS`) reports the window and its counters. `ROLL OFF` stops it and
frees the card for `SD` and `WAV` commands, and `ROLL ON` lets it start again. `ROLL
OFF` is refused while a save or a clip load is in progress.

```text
OK ROLL STATE=RUNNING ENABLED=1 RETAINED_MS=65536 SEGMENTS=14 ROTATIONS=13 ROTATE_MS_MAX=40 STEP_MS_MAX=31 WRITE_MS_MAX=27 QUEUES=1,1/8 BLOCKS=880 STARTS=1 FAULTS=0 RECLAIMED=1 ALLOC_FAIL=0 SAVE=SAVED SAVES=1 FAILED=0 BUSY=0 UNAVAILABLE=0 SAVE_MS_MAX=420 LAST=C001 LAST_FRAMES=2883584
```

| Field | Meaning |
| --- | --- |
| `STATE`, `ENABLED` | `RUNNING`, `WAITING` (no card, a session, or a retry pending), `FAULT` or `OFF`; whether it is turned on |
| `RETAINED_MS` | Audio held for the window now |
| `SEGMENTS`, `ROTATIONS`, `ROTATE_MS_MAX` | Segments started, segment changes, and the longest change (close plus open, across passes) |
| `STEP_MS_MAX`, `WRITE_MS_MAX`, `QUEUES` | Longest single storage step, longest block write, and recorder queue high-water since the stream started |
| `BLOCKS`, `STARTS`, `FAULTS` | Blocks in this stream; streams started since boot; faults (each discards the window and retries after 5 s) |
| `RECLAIMED`, `ALLOC_FAIL` | Old segments overwritten; times no segment was free |
| `SAVE`, `SAVES`, `FAILED`, `BUSY`, `UNAVAILABLE`, `SAVE_MS_MAX` | Latest outcome and the count of each; longest save from request to commit |
| `LAST`, `LAST_FRAMES` | The newest capture committed since boot (`C000` for none) and its frames |

The demo image's C-010 chord takes a moment from Field into Instrument
(`full_spooky_proto-p04.6`, decision 0011 item 15). Like the rest of the demo, this is
demo-only behavior.

- **The chord.** Shift plus Buttons 0 and 1 outside a session saves the rolling window,
  switches to Instrument and loads a clip from that save. While the save commits,
  Instrument shows `SAVING`. While the clip loads, it shows `LOADING`.
- **The clip.** The clip is the last 3 s before the save point (or less, if less was
  held): the mean of the two radio channels, filtered and decimated to 24 kHz, 16-bit,
  at most 144,000 bytes in AXI SRAM. It is read from the newest one or two segments of
  the committed capture, 2,048 frames per step, between the rolling stream's block
  writes.
- **Playback.** A loaded clip plays on the headphones in place of the radio while
  Instrument is shown, through the granular voice below (`full_spooky_proto-p04.7`).
  `DEMO VOICE LOOP` plays it as a plain loop instead, and `DEMO VOICE GRAIN` goes back.
  The recorder still captures the raw radio channels unchanged.
- **Failure.** If the save is busy, unavailable or fails, or the load fails, the clip is
  not shown or played. Instrument returns to Field by itself and the display shows
  the reason (`CLIP: SAVE BUSY`, `CLIP: NO BUFFER`, `CLIP: SAVE FAILED` or
  `CLIP LOAD FAILED`). A new chord replaces the clip, and a save or a load in progress
  makes another save busy.
- **Back to Field and sessions.** Shift plus Button 0 (C-028) returns to Field and stops
  the voice. The clip stays loaded for the next visit. Holding Button 0 for a session in
  Instrument is refused with `SESSION: FIELD ONLY`. During a session the chord saves
  nothing (rolling capture is off) and the mode switch is rejected.

`DEMO CLIP` reports the clip, for example:

```text
OK DEMO CLIP STATE=READY FAULT=NONE CAPTURE=C004 SAMPLES=72000 MS=3000 PLAYING=1 LOOPS=12 LOADS=3 FAILURES=1 LOAD_MS=930 LOAD_MS_MAX=1010 STEP_MS_MAX=6 RETURNS=1 RETURNS_REFUSED=0 PROMPTS_REFUSED=0
```

| Field | Meaning |
| --- | --- |
| `STATE`, `FAULT` | `NONE`, `SAVING`, `LOADING`, `READY` or `FAILED`; the latest failure's reason (`SAVE_BUSY`, `SAVE_UNAVAILABLE`, `SAVE_FAILED`, `LOAD_FAILED`) |
| `CAPTURE`, `SAMPLES`, `MS` | Capture the clip comes from; its length at 24 kHz and in milliseconds, while ready |
| `PLAYING`, `LOOPS` | A voice is on the monitor; passes of the plain loop |
| `LOADS`, `FAILURES` | Clips loaded and chords that ended in a failure since boot |
| `LOAD_MS`, `LOAD_MS_MAX`, `STEP_MS_MAX` | Latest and longest load from the committed save to ready; longest single load step |
| `RETURNS`, `RETURNS_REFUSED` | Shift plus Button 0 gestures sent to return to Field after a failure; posts the queue refused, which are retried |
| `PROMPTS_REFUSED` | Session prompts refused in Instrument |

The granular voice reads the clip through at most 16 overlapping grains (decision 0011
item 16). Its controls are provisional and demo-only, and do not settle C-025.

- **Page 1:** Encoders 0 to 3 set position (where grains start, in the clip), grain size
  (10 to 500 ms), density (1 to 100 grains a second) and pitch (±24 semitones).
- **Page 2:** Encoders 0, 2 and 3 set spray (random spread of grain starts, as a share of
  the clip), envelope (2 ms ramps at 0 %, a triangle at 100 %) and level. Encoder 1 is
  unassigned: slice quantization was removed (decision 0020 item 6).
- **Page change and Button 1:** An Encoder 3 click (C-024) moves to the next page and
  wraps. Button 1 does nothing in Instrument.
- **Display.** The display shows the page's four parameters, the clip and the number of
  sounding grains. The values stay as set across visits and new clips. With no clip
  loaded, or with the plain loop, there is no page and the encoders do nothing.
- **Matrix.** The matrix shows grains instead of EMF. The nine columns span the clip.
  Each sounding grain lights the column it is reading, as bright as its envelope, and
  the position setting is a dim column. With no clip the matrix is dark. A session's
  recording ring still shows.

`DEMO GRAIN` reports the voice, for example:

```text
OK DEMO GRAIN VOICE=GRAIN PAGE=1 POS=500 SIZE_MS=80 DENSITY=20 PITCH=0 SPRAY=0 ENV=50 LEVEL=80 ACTIVE=2 HIGH=4/16 STARTED=1200 DROPPED=0 RENDERS=5600 RENDER_US_MAX=240 OVER_BUDGET=0 BUDGET_US=1500 GESTURES=35
```

| Field | Meaning |
| --- | --- |
| `VOICE`, `PAGE` | `GRAIN` or `LOOP`; the Instrument page shown |
| `POS` ... `LEVEL` | The parameters: position and spray in permille of the clip, size in ms, density in grains a second, pitch in semitones, envelope and level in percent |
| `ACTIVE`, `HIGH`, `STARTED`, `DROPPED` | Grains sounding now; the most at once, of the limit; grains started; grains due while every voice was busy |
| `RENDERS`, `RENDER_US_MAX`, `OVER_BUDGET`, `BUDGET_US` | Monitor halves (512 frames, 10.7 ms) a voice rendered in the radio interrupt; the longest render; renders over the budget, counted, not cut short |
| `GESTURES` | Encoder turns and Encoder 3 clicks the Instrument pages took |

`DEMO GRAIN RESET` clears `RENDER_US_MAX` and `OVER_BUDGET`.

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
683 ms of write-stall tolerance.

The start follows decision 0012. Immediately before the microphone DMA starts, with
interrupts masked for a few microseconds, the firmware reads the radio DMA position,
enables radio capture and starts the microphone DMA. The recorder then drops the radio
frames received before that position plus the microphone start latency *C*
(`SPOOKY_RECORD_MIC_LATENCY_FRAMES`). Decision 0017 keeps *C* at 0: radio frame 0 is
the radio frame in progress when the microphone DMA starts. Microphone sample 0
represents sound from a constant, unmeasured interval after that, estimated by
arithmetic at about 2 to 4 frames plus the microphone's own delay. The alignment is
qualified as repeatable: loopback recordings agreed within 0.3 frames in one boot and
1.4 frames across a reset, with no drift over ten minutes
([evidence](../evidence/2026-10-02-recording-start-alignment.md)). Before this change
the radio track could start up to one 512-frame radio DMA half (about 10.7 ms) before
the microphone.

Capture starts only after the file is preallocated, so no allocation search runs while
recording. `RECORD START` mounts and checks the card and replies
`OK RECORD PREPARING prealloc-kib=...`; the Session state is then Preparing, which
follows the recording command policy. The following foreground passes probe
`REC###.WAV` names, create the file and search the FAT for one contiguous free run
long enough for the preallocation. Each pass spends about `SPOOKY_RECORD_PREPARE_STEP_MS`
(32 ms by default) and always makes progress (one name probe or one FAT sector).
The preallocation is the first 60 seconds, or the whole requested duration when it is
shorter, but never more than the free space above the card reserve. The search starts
at the volume's next-free hint and wraps once around the volume. Once the run is
found, the file is allocated and its header written; `OK RECORD PREPARED` reports the
time spent in each step, and `OK RECORD START` follows when capture starts:

```text
OK RECORD PREPARED file=REC006.WAV prealloc-kib=16875 open=...ms name=...ms create=...ms search=...ms fat-sectors=... allocate=...ms steps=... step-max=...ms elapsed=...ms
```

When no contiguous run is long enough anywhere on the volume, the start is refused
and the empty file deleted:
`ERR RECORD free space too fragmented largest-run-kib=... need-kib=... fat-sectors=...`.
Without a contiguous allocation the file would grow one cluster at a time, and FatFs
would search the FAT inside a block write; on a nearly full, fragmented card one such
write took 1.4 s and overran the queues
([trial](../evidence/2026-10-01-prealloc-search-trial.md)). `RECORD STOP` while preparing cancels the start and
deletes the empty file (`OK RECORD STOP cancelled before capture; no file kept`).
`RECORD STATUS` reports `OK RECORD PREPARING file=... fat-sectors=... elapsed=...ms`
during preparation. Volumes other than FAT32 skip the stepped search and rely on
FatFs `f_expand`, whose FAT12/16 tables are at most 256 sectors.

A recording longer than its preallocation (open-ended, or timed beyond 60 s) grows
past it one cluster at a time. FatFs then takes the first free cluster after the file,
scanning forward through any used clusters inside that block write. To keep that scan
short, the recorder reads the FAT ahead of the file during the recording, one sector
(128 clusters, about 14.5 s of audio with 32 KiB clusters) at a time in idle
foreground passes, keeping about 30 s ahead of the write position. It counts the free
clusters the file can reach across used stretches of at most
`SPOOKY_RECORDING_MAX_GAP_FAT_SECTORS` FAT sectors (16 by default: 2048 clusters, about
25 ms of FatFs reading inside one write). The recording may fill only the space
verified that way. When a longer used stretch, or the end of one lap around the FAT,
leaves no further reachable cluster, the recording finalizes before the next block
would need one and reports the fault:
`ERR RECORD ABORT file=... reason=free space fragmented finalized=1`. `RECORD LATENCY`
reports `runway=none|open|end|failed`, the FAT sectors read ahead (`fat-ahead`) and the
longest used stretch crossed in clusters (`gap-max`). The preallocated length is the
build setting `SPOOKY_RECORDING_PREALLOC_SECONDS`, and the step budget is
`SPOOKY_RECORD_PREPARE_STEP_MS`.

The step budget bounds the FAT search but not a single directory call: each name
probe, the file creation and the deletion of a discarded file scan the directory in
one FatFs call. On a root of about 1,900 entries they took 60-116 ms, against 33 ms or
less with 83 files ([evidence](../evidence/2026-10-02-stepped-preallocation.md);
`full_spooky_proto-jjy.18`).

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
OK RECORD PREPARING prealloc-kib=16875
OK RECORD PREPARED file=REC000.WAV prealloc-kib=16875 ...
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
70 ms and above). Counters reset at `RECORD START`. Its fields `usb-superseded`,
`usb-lost` and `usb-detached` are cumulative since boot and describe the recorder
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

The queue holds lines only for a host that has the CDC interface configured
(`full_spooky_proto-8lw.20`). With no host attached, as in a field recording, the
recorder queues nothing. When the host goes away, the lines already queued are
discarded rather than delivered stale to the next host. Both cases count in
`usb-detached`, are not faults and leave `HAS_FAULT` unset; the lines still appear on
the UART log, and `RECORD RESULT` keeps the outcome.

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
`RECORD TIMELINE` is a read-only query that reports the radio stream timeline and the
start alignment of the current or last recording since boot:

```text
OK RECORD TIMELINE NONE epoch=1 now=123456789
OK RECORD TIMELINE file=REC076.WAV epoch=1 origin=123456789 mic-start=123456789 p=21 skip=533 c=0 tx-rx-phase=731 now=124000000
```

Positions are radio frames since the radio stream started in this `epoch`, which
restarts at boot. `mic-start` is the radio frame in progress when the microphone DMA
started and `origin` is that frame plus `c`, the first frame of the recording on both
tracks. `p` is `mic-start`'s offset into its 512-frame DMA half and `skip` the number
of radio frames dropped from the first half delivered after the start. `tx-rx-phase`
is the SAI2 radio receive DMA index minus the SAI1 monitor transmit index, in frames
modulo the 1024-frame buffer, or `none` without a running monitor; the loopback
qualification uses it to compare boots. `now` is the current position.

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

## Battery and charge status

`BATTERY READ` (also `BATTERY` or `BATTERY STATUS`) and `CHARGE STATUS` report the
BQ27441 fuel gauge. Outside recording each request reads the gauge, which takes a few
milliseconds of I2C. While recording, fuel-gauge I2C pauses
([foreground latency](foreground-latency.md)) and the reply carries the last
successful reading. `AGE_S` is that reading's age in seconds:

```text
OK BATTERY PRESENT=1 SOC=..% VOLTAGE=.. mV REMAINING=.. mAh FULL=.. mAh DESIGN=.. mAh SOH=..% FLAGS=0x.... AGE_S=0
OK CHARGE VBUS=1 STATE=CHARGING|DISCHARGING|IDLE CURRENT=.. mA POWER=.. mW FULL=0|1 AGE_S=0
```

`STATE` comes from the gauge's average current: above 5 mA is charging, below -5 mA
discharging. The average lags a charger change by a few seconds.

## Low-power charging monitor

`SLEEP START` is a one-way low-power state entered from the CLI; it is rejected in IPC
experiment builds and while the Session state is Preparing, Recording or Finalizing. An active
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
