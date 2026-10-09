# Speaker as the default monitor, headphones on insertion

Date: 2026-10-09 (local, UTC-6; Spooky Bench log timestamps are UTC)  
Beads issue: `full_spooky_proto-jr0`

The opt-in `SpeakerMonitor` preset was tested with a PAM8302 amplifier and speaker
wired to the audio shield's line-out header J5 (L to A+, GND to A−). The amplifier
runs from `VSYS_RAW`, and its SD pin is on `AMP_SD` (PG8). The operator confirmed
the board was connected and free. Spooky Bench 0.7.0 flashed each image pair and
captured the UART7 log through Spookyprobe for 300 s after each flash. Listening
checks are the operator's reports. The battery read 4187 mV, 100% charge, during
the session.

**Result:** pass, with one minor known issue. With no headphones, the radio plays
on the speaker. Inserting headphones moves it to the headphones and silences the
speaker; removing them returns it to the speaker. The pot controls both paths. The
first image left the speaker faintly audible at the bottom of the pot. The second
image shuts the amplifier down while muted, and the speaker is then completely
silent. A very quiet pop remains at the speaker's mute/unmute boundary; the
operator judged it acceptable for now.

## Images

Both built with GNU Tools for STM32 14.3.1 (14.3.rel1.20251027-0700) from `323918c`
plus the uncommitted experiment, `SpeakerMonitor` preset. Both flashes passed.

| Build ID | Change | CM7 SHA-256 | Source patch SHA-256 |
| --- | --- | --- | --- |
| `jr0-speaker-20261009a` | first experiment | `525a59c08d29c09eaadb1c7dd9267082b205a801cd0a583b9be6add36c0e45fb` | `0cfa3e03842a6fa2f3c6605396bb1da991f82af010b9c81c4d91c20d75fa28d8` |
| `jr0-speaker-20261009b` | amplifier off while muted | `83975e125839b791cee1cbd6acb94384b783101103cafa6dbd4459f4be1f6a07` | `191a4768335463b75a0824c6730c14bd9ddf7600c4787d4f98b9e6d3ad26695e` |

The CM4 image was identical in both builds
(`30cad630e930a96ff27e784412c74872187222d42697e25876154e3530beccb1`). The patches are
`build/jr0-speaker-20261009a.patch` and `build/jr0-speaker-20261009b.patch`.

## Image a

Flashed at 17:38 UTC. The boot log, with no headphones present:

```text
Jack detect: line-in PA5=empty, headphone PE0=empty
[audio] volume pot PF10/ADC3 ready; initial ADC=3714
[audio] SGTL5000 I2S -> DAC -> headphone setup
[audio] PE2 MCLK approximately 12288025 Hz
[audio] PASS: CHIP_ID=0xA011; headphone path configured muted
[audio] speaker experiment: line out on; monitor on speaker
[audio] volume ADC=3713 -49.5 dB
[summary] PASS: radio audio is routed to the headphone codec
```

During the 300 s capture the operator plugged and unplugged the headphones five
times. Each insertion and each removal logged exactly one event, with no path
chatter from plug bounce:

```text
[audio] headphones inserted; monitor on headphones
[audio] headphones removed; monitor on speaker
```

The pot swept −1.0 to −51.5 dB and reached mute twice (filtered ADC 939 and 1006).
The log showed no codec, I²C or audio-path failure.

The operator reported:

- speaker audible by default, headphones taking over on insertion and the speaker
  silent, speaker back on removal, as expected on every cycle
- the speaker still faintly audible with the pot all the way down

The log shows the firmware reached the muted state (line out and DAC muted) both
times. The remaining sound came from the amplifier, still enabled behind a muted
input.

## Image b

Image b releases `AMP_SD` only while the speaker path is selected and unmuted. On
mute, the codec mutes before the amplifier shuts down; on unmute, the amplifier
starts before the codec unmutes. Flashed at 17:45 UTC with the pot at its bottom
stop, which read ADC=1 at boot (`volume ADC=1 MUTED`).

During the 300 s capture the pot entered mute 12 times. The filtered reading crossed
the 1024 threshold at ADC 900 to 1023 on the way down. No headphone events occurred
in this window, and the log showed no failure.

The operator reported:

- the speaker completely silent with the pot at the bottom
- a very quiet pop on the speaker at the mute/unmute boundary

## Not covered

- Pop source: whether the pop comes from amplifier start-up or from the line-out
  unmute was not isolated.
- Supply voltage: speaker loudness and distortion at the top of the pot were judged
  only near full battery voltage.
- Recording: no recording ran, so capture under speaker monitoring is unverified.
- Band switches: none were exercised during the captures. The transition mute also
  shuts the amplifier down. See the addendum.
- Debug and Release behavior is unchanged by construction, since the code is under
  `SPOOKY_SPEAKER_MONITOR`. Both presets built, but neither was flashed in this
  session.

## Artifacts

Spooky Bench result files in `build/`, each naming its run directory under
`%LOCALAPPDATA%\SpookyBench\runs\`:

- `flash-jr0-speaker-20261009a.json`, `console-jr0-speaker-20261009a.json`
- `flash-jr0-speaker-20261009b.json`, `console-jr0-speaker-20261009b.json`

## Addendum, 2026-10-09: band switches

After the captures above, the operator switched bands on image b with the speaker
in use. The pop was audible only with an ear against the speaker at full volume,
and the operator judged it acceptable. No log was captured for these switches.
