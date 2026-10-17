# Silence fill and gap events during band switches (54w.12.1)

Date: 2026-10-16 (local, UTC-6; Spooky Bench and log timestamps are UTC, 2026-10-16
23:54 to 2026-10-17 00:17)  
Beads issues: `full_spooky_proto-54w.12.1`; bug found and fixed:
`full_spooky_proto-hpq.13`

This session checks
[decision 0004](../decisions/0004-radio-track-continuity-across-transitions.md)'s silence
fill and the exact gap events of
[decision 0030](../decisions/0030-field-session-folder-and-event-log.md):

- while a band switch has the radio stream gated, each radio half-buffer reaches the
  recorder as digital silence of the same length;
- the monitor plays silence;
- the gap's first and last silenced frames are stored in `EVENTS.BIN` and logged as a
  `[gap]` line.

It uses the opt-in `BandSwitchQual` preset, which allows band changes while recording
for this qualification only. Normal builds still refuse them until
`full_spooky_proto-54w.12`.

The operator confirmed the board was connected and free, powered up battery first,
with the SD card in. Spooky Bench 0.7.0 flashed the images, captured UART7, and
fetched the event logs and WAVs. An ad hoc pyserial script drove the CLI
(`build/w12a-bench.py`, logged in `build/w12a-bench.log`; and `build/w12a-bench-b.py`
with Classic at 400/min, logged in `build/w12a-bench-b.log`). A second ad hoc script
compared each event log with the UART lines (`build/hpq11-compare.py`). Neither is
part of Spooky Bench.

**Result: pass for the silence fill and gap events.**

- **Recordings:** four 60 s sessions, each with five band switches through FM, AM, SW
  and LW, recorded with exact accounting and no overrun. Before this change, the
  microphone queue overran after two or three switches (`p04.26`).
- **Gap records:** match the `[gap]` lines exactly.
- **Gap audio:** every gap in the two WAVs checked is exact silence on both radio
  channels.
- **Bug found:** the bench found an event log record after the session's last frame.
  It is fixed and confirmed by a rerun (`hpq.13`).
- **Blocking switch:** as expected, each blocking switch held the foreground for up to
  349 ms and raised the recorder queues to 4 or 5 of 8. That latched the storage
  margin warning and the foreground budget fault, which `54w.12.2` is to remove.

## Images

GNU Tools for STM32 14.3.1 (14.3.rel1.20251027-0700), `BandSwitchQual` preset, clean
builds on `claude/54w.12.1-silence-fill`:

| Build ID | Source | CM7 SHA-256 | CM4 SHA-256 |
| --- | --- | --- | --- |
| `w12a-bandqual-20261016a` | `b762eff` | `c8dda29c99fefd0d243912790aea4ae035ce0a5f271f44339fd5ef74611dc0e4` | `02865bdcfcdee3b4057ce8db47e807d31b654769f438b02c5d35804663685cfb` |
| `w12a-bandqual-20261016b` | `a5f6c3e`, with the `hpq.13` fix | `166680b41ce28deeeb19290ce35d47048f93ec25a5acb2504dae4e8369f8eb4e` | `02865bdcfcdee3b4057ce8db47e807d31b654769f438b02c5d35804663685cfb` |

Debug, Release and BandSwitchQual build. Native host tests pass 34/34 (MSVC),
including the new `radio_gap_test`, the band-qualification variant of the policy test,
and the `hpq.13` horizon cases in `session_event_log_test`. The Spooky Bench Python
tests pass 125/125. Host tests are not hardware evidence.

## First run (image a)

Classic scanned at its default 120/min. Before the sessions, two band switches outside
a session (FM to AM and back) produced `[gap]` lines with `missed=0`.

| Session | Origin | Band switches | Result | Queues | `RECORD EVENTS` |
| --- | --- | --- | --- | --- | --- |
| F0016 | `1:878523` | FM→AM, AM→SW, SW→LW, LW→FM, FM→AM | `PASS`, 2,883,584 frames | radio 4/8, PDM 4/8, margin `LOW` | `written=258 lost=0` |
| F0017 | `1:4443945` | AM→SW, SW→LW, LW→FM, FM→AM, AM→FM | `PASS`, 2,883,584 frames | radio 4/8, PDM 4/8, margin `LOW` | `written=258 lost=0` |

- **Health:** `RADIO_OVR=0 PDM_OVR=0 SD_ERR=0 AUDIO_ERR=0`. `HAS_FAULT=1` came from
  `FOREGROUND_BUDGET`. `DIAG LATENCY` reported the loop at most 342 ms against its
  75 ms budget, and the dispatcher at most 322 ms against 10 ms, with 10 violations
  each: one per band switch. The longest SD write was 52 ms.
- **Gap edges:** every gap start and gap end in both event logs matches the `[gap]`
  line at the same session frame, five of each per session.
- **The bug (`hpq.13`):** F0016's log also held a tune end at frame 2,885,635, after
  the last audio frame 2,883,583, and `spookybench session events` reported "a record
  lies after session end". The WAV lags real time by a block or two, so a tune that
  ended while the last block waited was stamped past the end. Decision 0030 item 10
  forbids that.

### Gaps in the audio

Both WAVs were transferred with passing CRCs:

| Session | SHA-256 |
| --- | --- |
| F0016 | `f3a381a10516f23f159c7390cdf0dbd9c18cbcb520afdd9ce1a25850dfeb058c` |
| F0017 | `86419c7a207c859cb420e4e24f9f0dbda5d3b5fa808bca4eaa732f0563a5a2fc` |

`spookybench wav retune --events` checks every frame of each gap:

| Session | Gaps (session frames) | Lengths (frames) | Nonzero frames inside |
| --- | --- | --- | ---: |
| F0016 | 616,517–627,780; 1,108,037–1,119,300; 1,596,485–1,611,332; 2,091,077–2,097,732; 2,576,965–2,592,324 | 11,264; 11,264; 14,848; 6,656; 15,360 | 0 |
| F0017 | 616,663–627,926; 1,105,623–1,120,470; 1,597,655–1,605,334; 2,086,615–2,101,974; 2,582,743–2,589,398 | 11,264; 14,848; 7,680; 15,360; 6,656 | 0 |

The gap lengths, 139 to 320 ms, are the band switch times. Switches into AM and LW are
the longest.

The analysis first counted a tune's mute running straight into a gap as tune overhang
(11,305 frames on AM). It also hid FM and AM behind the many SW runs. The tool now
splits a run at a gap and counts unexplained runs per band. With that, FM, AM and LW
have no zero run outside a tune or gap and no overhang. Every unexplained run is in an
SW stretch (174 in F0016, 1 in F0017), where there is no usable signal, as in the
[retune qualification](2026-10-09-retune-qualification.md).

## Second run (image b, with the fix)

Classic scanned at 400/min, so more tunes end around the session end.

| Session | Origin | Result | Queues | `RECORD EVENTS` |
| --- | --- | --- | --- | --- |
| F0018 | `1:1355431` | `PASS`, 2,883,584 frames | radio 4/8, PDM 5/8, margin `LOW` | `written=568 lost=0 queue-max=23/64` |
| F0019 | `1:4921047` | `PASS`, 2,883,584 frames | radio 4/8, PDM 4/8, margin `LOW` | `written=522 lost=0 queue-max=18/64` |

Both event logs pass the reader rules. No record lies after session end (frame
2,883,583), sequence numbers are contiguous, and every record matches the UART log:

| Session | Tune starts | Tune ends | Classic | Gap starts | Gap ends |
| --- | --- | --- | --- | --- | --- |
| F0018 | 273 / 273 | 272 / 272 | 10 / 10 | 5 / 5 | 5 / 5 |
| F0019 | 250 / 250 | 249 / 249 | 10 / 10 | 5 / 5 | 5 / 5 |

F0019's last tune started at frame 2,883,339, 244 frames before the end. Its end was
observed after the last frame and is correctly absent. Health was as in the first run:
no overrun, SD or audio error, and the loop at most 349 ms during the switches.

## Not exercised

- **Monitor silence:** the monitor's silence during gaps was not listened to. The
  codec's transition mute is still applied around each switch.
- **A failed band switch:** belongs to `54w.12.3`.
- **The second run's WAVs:** not transferred. The firmware's silence fill is unchanged
  from the first run, whose gaps were checked.

## Artifacts

Spooky Bench result files in `build/`, each naming its run directory under
`%LOCALAPPDATA%\SpookyBench\runs\`:

- `flash-w12a-bandqual-20261016a.json` and `-b.json`
- `console-w12a-bandqual-20261016a.json` and `-b.json`
- `events-F0016-w12a.json` to `events-F0019-w12a.json`
- `wav-F0016-w12a.json` and `wav-F0017-w12a.json`
- the retune results `retune-F0016-w12a-b.json` and `retune-F0017-w12a-b.json`, from
  the refined tool

The UART logs are `build/uart-w12a-20261016a.log` and `build/uart-w12a-20261016b.log`.
