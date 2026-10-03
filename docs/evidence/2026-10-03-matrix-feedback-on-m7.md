# Matrix feedback on the M7

Date: 2026-10-03 (local, UTC-6; Spooky Bench and CLI log timestamps are UTC)  
Beads issue: `full_spooky_proto-54w.8`

This session ran the decision 0013 matrix feedback from the M7 on the NUCLEO-H755ZI-Q
bench with the UI board's IS31FL3741 matrix and TMAG5273 magnetometer. The operator
confirmed that the board and the UI board were connected and free, with the radio path
and an SD card installed. Spooky Bench 0.7.0 ran on Windows with target CDC on COM3 and
Spookyprobe on COM6. The matrix was driven over the USB CLI (`UI MATRIX FEEDBACK`,
`UI MATRIX TRAIL`, `UI MATRIX ORIENT`, `EMF STATUS`) by an ad hoc pyserial script (not
part of Spooky Bench); raw exchanges are in the untracked local
`build/matrix-54w8-*.jsonl` files.

**Result:** the feedback runs on the hardware with no failed I2C2 writes, and a 60 s
recording regression passes with the matrix writing throughout the capture (opt-in
qualification build). The user judged the onset kicks right, rejected indigo and the
original green, chose four EMF buckets with new bounds and the trail off, and found
that the always-adapting baseline left a false reading after a held magnet. With the
baseline gated to a quiet field, the matrix returned to cyan straight after every
magnet pass. These results are the evidence for
[decision 0019](../decisions/0019-matrix-emf-four-buckets-and-quiet-baseline.md). The
session also found the matrix mounted rotated 180 degrees, raised I2C2 to fast mode so
kick frames are written whole, and found a tick loss in `main` while recording
(`full_spooky_proto-akw`).

## Images

All images were built from `eb1d9ca9f9d809259efa1d7540ba4fa441ee4607` plus the
uncommitted `54w.8` changes, archived as a source patch per build (`build/<build ID>.patch`,
SHA-256 in the manifest), with GNU Tools for STM32 14.3.1+st.2. Every flash passed.

| Build ID | EMF buckets and bounds (µT) | Baseline | CM7 SHA-256 | Source patch SHA-256 |
| --- | --- | --- | --- | --- |
| `54w8-matrix-ipcsmoke-20261003a` | 5: 32 / 128 / 512 / 2,048 (decision 0013) | always adapts | `1b38edb2c869c17a1da874aaa176f1b3862ac3ea77de9aefa913a55dd690ac04` | `ab91f364a918f2a37efbfd253c904a39b2b7f7e71b06cedfcd736b87537ce53b` |
| `54w8-matrix-ipcsmoke-20261003b` | 4: 80 / 600 / 3,000 | always adapts | `2df6ddaa37347202ca0c5bae57605fac4c90306e321ae5dc79a13a4ede74d56f` | `f4b3cfe1c9041fda0b0c1a46a94632e8f1151d18d7b7f2d0e149d253f049c1f9` |
| `54w8-matrix-ipcsmoke-20261003c` | 4: 150 / 750 / 4,000 | always adapts | `69eec6410281fa3566ccb1b105536f0b00a80491ace0198500967151de2701ad` | `425f8c8d540f5d7aaac501aab15931692906c337cae9b44c073ca1b8e0867722` |
| `54w8-matrix-ipcsmoke-20261003d` | 4: 150 / 400 / 1,200 | adapts below 100 µT | `f81d2c34b6e916e764e5a41e2ce3a847ffc1bc4efbfb0761c998462ec53e008c` | `f59162018188402e819506a043f2a99d7e0f8199952cf004f6371a8609eaf678` |

Builds b to d use cyan (0, 175, 230), green (0, 255, 0), amber (255, 165, 0) and red
(255, 30, 20) at 160, 125, 95 and 70 ms per step. Later builds keep d's EMF settings:

| Build ID | Change from the previous build | CM7 SHA-256 | Source patch SHA-256 |
| --- | --- | --- | --- |
| `54w8-matrix-qual-ipcsmoke-20261003e` | d plus `SPOOKY_MATRIX_RECORDING_QUALIFICATION=ON` (matrix runs during capture, feedback starts at boot) | `408455dc18ab328b5fa54f0a74a6f6af843ef71713c8f0ff10fca754ebb9a24a` | `d720b977288635bbffe77c49c38eaaf5b42da4ee0140502fb93094bbb01539f4` |
| `54w8-matrix-ipcsmoke-20261003f` | normal image; trail off by default; `UI MATRIX ORIENT` | `6380998bc74c58563f30bee385bcc5ed88dbb9938f61f1cdd24c2fac6d18de15` | `d7387d4209313826fd016726d50fcd278ce5b24e496288337aa2e5e604ca1e40` |
| `54w8-matrix-ipcsmoke-20261003g` | 180-degree rotation; I2C2 fast mode; runs written for up to 3 ms per pass | `65463bf604aab4c0a8a37b1f2216b8008f5f8dfcec2a1a556558e2b12972cd98` | `d865f7296cdc39e1e61c8489adca148ae84212deda7da7ac57a8af8998a661c1` |
| `54w8-matrix-qual-ipcsmoke-20261003h` | g as a qualification build | `9e4b9d0bcba200c526b62fea813f27dc682966ccff0e99cc6290e044e795542b` | `d865f7296cdc39e1e61c8489adca148ae84212deda7da7ac57a8af8998a661c1` |
| `54w8-matrix-qual-ipcsmoke-20261003i` | h plus the `akw` radio interrupt priority change | `8d0a6a4294ff2c4386a541468a987ee5971fde186a78db44d6d12fc8dac13080` | `37a4ed3d935900ed32efe910b13776868005d612d42938ef315e5b5a2f4a5e03` |
| `54w8-matrix-qual-ipcsmoke-20261003j` | i with runs written for up to 2 ms per pass | `1b636730f5be6d12d90c3885898e495e7b811caa97206de5ac0ff11c434e8904` | `e80e63938f47f0060f610636a414a43424f7632a84ea06641fec213717b74465` |
| `54w8-matrix-ipcsmoke-20261003k` | j as a normal image; left on the board | `e5e515b27bda596346752abd6f50987a2901e4245900d8a846113d777ecfbe8b` | `e80e63938f47f0060f610636a414a43424f7632a84ea06641fec213717b74465` |

All are IpcSmoke images. The CM4 image was
`7d9d1116cb20cd8367de3d86a2ec8f067b1cf0fa0ea9220279c6cf9492489584` for the normal images
and `9200fe3675df827dff1e8ee4ac3090c6ab63ae52495f6baa639d15bbe7618767` for the
qualification images. Native host tests passed 30/30 (MSVC 14.29) for each build; host
tests are not hardware evidence.

## Write cost

The writer sends register runs, per row 24 bytes for columns 0 to 7 and 3 bytes for
column 8, skipping runs that have not changed. Every status read in the session showed
`FAILED=0`. Runs were timed with the DWT cycle counter.

Builds a to f sent one run per foreground pass with I2C2 at the generated standard-mode
timing (about 100 kHz). The longest run was 3.6 to 4.0 ms with no recording running
(`RUN_US_MAX` 3,614 to 3,972 µs; about 223,000 runs on build a had none longer than
3,849 µs) and 5.9 ms during capture on build e. On build a, 5,020 of 22,284 frames (23%)
were replaced by a newer frame before they were fully written (`SUPERSEDED`): a kick
changes every row, and its 18 runs took longer than the 80 ms a one-pixel kick lasts.
During a recording on build e, this showed as the user seeing the recording border
blink only along one edge.

Builds g to i run I2C2 in fast mode (`0x00F61F37`, about 360 kHz) and send runs for
up to 3 ms per pass; builds j and k for up to 2 ms (see the border blink below). With Classic scanning and 13 onsets in 20 s on build g, the longest
run was 1,756 µs and none of 132 frames was superseded. The magnetometer
(`MAG STATUS`, `MAG READ`, `EMF STATUS`), the fuel gauge (`BATTERY READ`) and the matrix
(`UI MATRIX PROBE`) all answered normally at the new speed.

## Orientation (builds f and g)

`UI MATRIX ORIENT` lights logical (0,0) red, (8,0) green and (0,8) blue. On build f
the user saw them rotated 180 degrees: the matrix is mounted upside down relative to
the panel mapping proven by `UI MATRIX ANIMATE`. Build g rotates logical coordinates
in the writer, and the user then saw red at top-left and green at top-right.

## Animation and onset kicks (build a)

With Classic scanning, the user judged the onset kicks to have the intended effect:
"noticeable enough to add visual interest but subtle enough not to draw too much
focus". The user then turned the trail off (`UI MATRIX TRAIL OFF`) to compare, and the
remaining trials ran without it. The user chose to make the trail off by default.

## EMF colours and bounds

Each trial logged the bucket or the field and baseline about twice a second while the
user moved a magnet toward the sensor and away.

**Build a (decision 0013).** Indigo looked "too pinkish" (purplish magenta), and the
green (40, 225, 95) looked pale. The user never noticed green. At rest the reading
ranged from 0 to 31 µT and crossed the 32 µT bound into bucket 1 five times in
72 s. On a slow approach the reading stayed in bucket 0 until the magnet was close,
then went through green (169 to 440 µT) and amber (528 to 1,332 µT) in about 2 s each
to red (up to 5,210 µT).

**Build b (four buckets, 80 / 600 / 3,000 µT).** Every bucket showed for a few seconds
per pass. The user preferred the wider buckets, but the matrix oscillated between cyan
and green with the magnet well away. A check at rest read 11 to 60 µT; the 90 s log
reached 72 to 76 µT early on.

**Build c (150 / 750 / 4,000 µT).** The matrix started cyan at boot and progressed as
expected, but stayed green for a long time after the magnet was taken away. A phone held
against the sensor reached only amber. The baseline explains the first: it moved 1/16
of the way to the measured field every 5 s, including the magnet's field, so after a
hold the reading stayed raised until the baseline drifted back.

**Build d (150 / 400 / 1,200 µT, baseline adapts only below 100 µT).** In a 2-minute
log the baseline stayed between 206 and 226 µT through four passes reaching amber and
red (peaks 2,421, 3,930 and 1,542 µT), and every pass dropped back to cyan. In a
3-minute log the baseline stayed between 182 and 191 µT through holds in red of about
10 s (peak 15,414 µT), 24 s (peak 5,616 µT) and 27 s (peak 7,072 µT, still held when
the log ended). Within about 3 s of the first two holds ending the reading was back in
cyan, at 3 to 145 µT. The user reported that a phone held very close with its screen on
reached red, and that the matrix "dropped right back down to cyan even after holding
the magnet directly on the sensor for a long time". The user judged these bounds a good
starting point, to be tuned after field testing.

Resting readings in build d's logs mostly stayed below 110 µT, but reached 119 to
146 µT in a few quiet stretches, close to the 150 µT bound. Whether that was the
sensor, the magnet lying nearby or the user's hand was not established.

## Recording

**Normal image (build d), 20 s recording `REC106`.** With feedback on, the matrix was
suspended (`SUSPENDED=1`, one suspension) within a second of the capture starting,
with no writes during the capture and the EMF state `STALE`, and resumed within a
second of the end; the user saw it go dark and come back. `MATRIX` took at most 1 ms
per pass and the loop at most 36 ms while recording, with no budget violations. The
resume added 5 to `DROPPED_STEPS` as the animation caught up its tempo.

**Qualification build e, 60 s regression** (run
`2026-10-03T203021.699911_0000-5bf13d9e`): every stage passed. `REC107`, 60,074 ms of
audio in 60,127 ms, queues 1/8, no overruns or faults, CRC `5f4f7b94`. `MATRIX` at
most 6 ms per pass and the loop at most 44 ms while recording, no budget violations.
The radio was on an idle channel, so no onsets fired and the user saw the yellow border
without blinks. Two more recordings on e (`REC108` with Classic scanning, `REC109` on
98.5 MHz) also passed; tuning is still refused during a recording on these builds
(decision 0003), so Classic stayed on one channel throughout `REC108`, and `REC109`
logged 10 small onsets.

**Qualification build h, 60 s regression** (run
`2026-10-03T211001.325770_0000-05485975`): **failed** `recording_duration`, 60,074 ms of
audio in 58,242 ms. The same tick loss then showed on committed `main` with no matrix
code, after a power cycle and on a static channel; it is the radio interrupt at
SysTick's priority, `full_spooky_proto-akw`, recorded in its own evidence file. During
the h run `MATRIX` took at most 6 ms per pass and the loop at most 59 ms, with no
budget violations.

**Qualification build i (with the `akw` change), 60 s regression.** The first run
(`2026-10-03T214839.814407_0000-b515eb9b`) recorded correctly (`REC127`, 60,074 ms of
audio in 60,123 ms) but ended in `io_error` during the WAV transfer: the operator
pressed the board's reset button by accident (`RESET` flags changed, `BOOT` advanced).
It is not counted. The rerun (`2026-10-03T215445.655416_0000-85c2ebbf`) passed every
stage: `REC128`, 60,074 ms of audio in 60,126 ms, queues 1/8, SD write maximum 27 ms,
no overruns or faults, CRC `a7e8a3f6`. While recording, `MATRIX` took at most 5 ms per
pass, the loop at most 36 ms and the recorder at most 27 ms, with no budget violations;
the longest run was 2,395 µs, with no failed writes or dropped steps.

**Border blink, builds i and j.** Classic starts scanning at boot, so it was paused
before tuning to 99.1 MHz, a channel the user had found active. On build i a 30 s
recording (`REC129`, 30,037 ms of audio in 30,091 ms) logged about 17 small onsets, and
the user saw the whole border blink with them: "it definitely looked more like the
full blink we'd been picturing". `MATRIX` then took at most 8 ms per pass against its
10 ms budget (longest run 4,438 µs, stretched by the microphone-copy interrupt), the
loop at most 60 ms. Build j cut the write time per pass to 2 ms. Its first two
recordings landed on quiet channels because Classic had moved on before the capture
(1 and 3 onsets; `MATRIX` at most 4 and 5 ms). With Classic paused on 99.1 MHz, `REC132`
(30,037 ms of audio in 30,088 ms) logged 46 small onsets, and `MATRIX` took at most
6 ms per pass, the loop at most 33 ms and the recorder at most 25 ms, with no failed
writes, no dropped steps and no budget violations.

## Not covered

- The 60 s regression ran on build i (3 ms per pass); build j's 2 ms change was checked
  with the 30 s recordings above, not a full regression.
- No listening check of the regression WAVs.
- Normal images keep the matrix dark and the magnetometer unread during a capture;
  lifting that is `full_spooky_proto-54w.36`.
- The EMF bounds are starting values to be tuned after field testing.
