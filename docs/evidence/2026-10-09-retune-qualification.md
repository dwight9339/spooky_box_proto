# Retune interval qualification (decision 0015 item 10)

Date: 2026-10-09 (local, UTC-6; Spooky Bench and log timestamps are UTC, 20:55 to
21:02 for the recordings)  
Beads issue: `full_spooky_proto-54w.6.1` (under `full_spooky_proto-54w.6`)

This is the qualification in [decision 0015](../decisions/0015-raw-radio-track-during-in-band-tunes.md)
item 10. Every exact-zero radio run of 1 ms or more after the first tune must lie
inside a stamped retune interval. Each band's settle margin is the largest overhang
past a tune's end stamp, rounded up to a whole half-buffer (512 frames).

The operator confirmed the board was connected and free, with an antenna attached
but no SW reception expected. Spooky Bench 0.7.0 flashed the image and captured
UART7 for 1,200 s. An ad hoc pyserial script (`build/q54w61-rec.py`, not part of
Spooky Bench) drove the CLI on COM3, logged in `build/q54w61-cli.log`. Classic
scanning produced the tunes.

**Result:**
- **FM and AM: pass.** Every zero run of 1 ms or more lies inside a stamped tune, and
  none extends past its tune's end stamp. Item 10's rule therefore gives a settle
  margin of 0 frames on both bands.
- **SW: not qualified.** It had no usable signal, so its radio track was mostly exact
  zeros unrelated to tunes. Item 10 requires SW only with usable reception.
- **LW:** not tested.

## Image

| Build ID | Preset | Source | CM7 SHA-256 | CM4 SHA-256 |
| --- | --- | --- | --- | --- |
| `54w61-retune-20261009a` | `RadioTuneQual` | `35d2ecc` (`claude/54w.6-retune-stamps`, clean) | `553a2cf231d8f556262a24573f385e55d898fdff9234ea58a819c48234165c68` | `5533e35d55d0d5d83f0b7bc0ce32d88bb322dc62754f8d281462eb62dfc5fb8f` |

GNU Tools for STM32 14.3.1 (14.3.rel1.20251027-0700). `35d2ecc` adds decision 0015
items 2 and 3 to `CM7/App/radio_adapter.c`:
- **Start stamp:** `AudioPath_GetPosition` immediately before
  `RadioControl_BeginTune`. That precedes the receiver-ready wait, which is capped at
  5 ms, and the command write.
- **End stamp:** taken when the outcome is published (tuned, failed or abandoned).
- **Log:** each issued tune produces one line:

  ```text
  [retune] start=1:34394 end=1:35443 unc=3600 band=FM target=99200 outcome=TUNED freq=99200
  ```

The same commit adds Spooky Bench's offline `wav retune` analysis, with host tests.
The `RadioTuneQual` preset allows in-band tuning while recording (decision 0003
guard, opt-in), so Classic kept scanning through each recording. Debug, Release and
RadioTuneQual build. The native host tests pass 31/31 and the Spooky Bench Python
tests pass. Host tests are not hardware evidence.

## Recordings

Each was a 60 s `RECORD START 60` while Classic scanned. The timeline origin is from
`RECORD TIMELINE` during the recording.

| File | Band | Rate (per min) | Origin | Frames | Result | Queues | Longest write |
| --- | --- | --- | --- | ---: | --- | --- | ---: |
| `REC149.WAV` | FM | 120 | `1:2245835` | 2,883,584 | `RECORD PASS` | at most 1/8 | 49 ms |
| `REC150.WAV` | FM | 400 (fastest) | `1:6294080` | 2,883,584 | `RECORD PASS` | at most 1/8 | 26 ms |
| `REC151.WAV` | AM | 180 (AM's limit) | `1:10508512` | 2,883,584 | `RECORD PASS` | at most 1/8 | 52 ms |
| `REC152.WAV` | SW | 240 (SW's limit) | `1:14584217` | 2,883,584 | `RECORD PASS` | at most 1/8 | 24 ms |

All were 60.074 s of audio. After the last recording, `DIAG STATUS` showed
`RADIO_OVR=0 PDM_OVR=0 SD_ERR=0 AUDIO_ERR=0 HAS_FAULT=0`. `LOG STATUS` showed no
dropped writes or bytes and no lost transmission. `LOOP_MAX_MS=327` came from the
CLI band switches made between recordings.

The UART log (saved as `build/uart-54w61-retune-20261009a.log`) holds 4,724
`[retune]` lines, one for each `[radio] tuned` line. Every line has both stamps and
outcome `TUNED`; no tune failed or was abandoned.

### Transfer

- **`REC149.WAV`** was transferred with `spookybench wav inspect`, with a passing CRC.
- **`REC150.WAV` and `REC151.WAV`** transfers stalled after `OK WAV START`, because
  the host PC went to sleep during them. Those runs are incomplete; it was not a
  device fault, and `DIAG STATUS` and `DIAG USB` were clean afterwards.
- **From the card:** the operator then moved the card to the PC, and all four files
  were copied from it. `REC149.WAV`'s SHA-256 from the card matches the CRC-checked
  transfer.

| File | SHA-256 |
| --- | --- |
| `REC149.WAV` | `3da4b47a6df4a63478c38749804085f3597f22fc89f4241d4842fdd1e4bad5bb` |
| `REC150.WAV` | `76110343a4e522af0ae3fface3a1957d9abcabe7984aca5476f30deae63e3c32` |
| `REC151.WAV` | `36ee106fd1a62c194d65a80c3fcaaa7a0ce0c7bd12076e0a2878748401216a24` |
| `REC152.WAV` | `b830d80e29e355b1b3890c9cdda881b9cfcde5bacbbc98b123a5731d648a9960` |

Each is 3 channels, 48 kHz, 16-bit, with 2,883,584 frames.

## Analysis

```text
spookybench wav retune --wav REC1nn.WAV --log build/uart-54w61-retune-20261009a.log --origin 1:<origin>
```

| File | Stamped tunes | Zero runs checked | Outside every tune | Largest overhang | Margin by item 10 |
| --- | ---: | ---: | ---: | ---: | ---: |
| `REC149.WAV` (FM 120) | 120 | 54 | 0 | 0 | 0 |
| `REC150.WAV` (FM 400) | 401 | 206 | 0 | 0 | 0 |
| `REC151.WAV` (AM 180) | 181 | 274 | 0 | 0 | 0 |
| `REC152.WAV` (SW 240) | 241 | 5,445 | 4,898 | 2,571 | not qualified |

Result files are in `build/retune-REC1nn-54w61.json`.

How the FM and AM zero runs sit inside their tunes, in frames:

| File | Run start after the start stamp (min, median) | End stamp after the run end (min, median, max) | Tune length (median, max) |
| --- | --- | --- | --- |
| `REC149.WAV` | 46, 48 | 371, 632, 2,263 | 1,033, 2,796 |
| `REC150.WAV` | 46, 48 | 340, 684, 3,180 | 1,143, 3,538 |
| `REC151.WAV` | 51, 2,118 | 81, 1,099, 7,403 | 8,314, 9,657 |

- **Start stamp:** on FM the receiver's mute begins about 1 ms after it, so the start
  stamp sits just before the receiver changes.
- **End stamp:** every run ends before it. The end stamp is a foreground observation
  of the completed tune, so it trails the end of the mute by at least 81 frames on AM
  and 340 on FM.
- **Runs per tune:** not every tune produced a zero run of 1 ms or more (54 runs for
  120 tunes in `REC149.WAV`).
- **AM:** the radio track was exact zero for 39.5% of `REC151.WAV`, all of it inside
  tunes, which took 50.9% of the time.

SW in `REC152.WAV` had no usable signal. The radio peak never exceeded 35 counts in
any 10 s window, and the track was exact zero for 70.7% of the time against 36.5% of
the time spent tuning. Those zero runs (median 117 frames) are spread through the
file with no relation to the tunes. They are the receiver's output with nothing to
receive, not a tune effect. Item 10 asks for SW only when an antenna gives a usable
signal, so SW stays unqualified and keeps the starting margin.

## Settle margin

By item 10's rule, the margin is 0 frames on FM and AM. The tested image uses item
5's starting value of one half-buffer for every band
(`RADIO_ACTIVITY_FEED_SETTLE_BLOCKS = 1` in `CM7/App/radio_activity_feed.h`), which
is larger than every overhang measured here. SW and LW keep the starting value until
they are qualified.

## Not exercised

- **LW**, and **SW with reception**.
- **Tune failures and abandoned tunes:** none occurred in these runs.
- **Rolling capture:** item 9 (rolling capture carrying the events) is not in this
  image.

## Artifacts

Spooky Bench result files in `build/`, each naming its run directory under
`%LOCALAPPDATA%\SpookyBench\runs\`:

- `flash-54w61-retune-20261009a.json`
- `console-54w61-retune-20261009a.json`
- `wav-REC149-54w61-20261009a.json` (pass)
- `wav-REC150-54w61-20261009a.json` and `wav-REC151-54w61-20261009a.json`
  (incomplete, host asleep)
- the analysis results `retune-REC149-54w61.json` to `retune-REC152-54w61.json`
- the WAV copies in `build/54w61-wav/`
