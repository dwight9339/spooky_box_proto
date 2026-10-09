# 0029. Speaker monitoring in the demo image

- **Status:** Accepted 2026-10-09
- **Date:** 2026-10-09
- **Supersedes:** none. Adds one demo-only behavior to
  [0011](0011-halloween-2026-demo-build.md) items 12 to 16; the rest of 0011 stands.
- **Beads:** experiment `full_spooky_proto-jr0` (closed); pop follow-up
  `full_spooky_proto-rmg`; demo task `full_spooky_proto-p04.25`; affects
  `full_spooky_proto-p04.8` and `full_spooky_proto-p04.9`

## Context

The prototype monitors only on headphones. Normal images keep SGTL5000 line out
powered down and hold the backplane `AMP_SD` net (PG8) low. A PAM8302 amplifier
breakout with a speaker is now wired to the audio shield's line-out header J5
(L to A+, GND to A−). It is powered from `VSYS_RAW`, and the breakout pulls SD up to
that supply ([prototype hardware](../design/prototype-hardware.md)).

The opt-in `SpeakerMonitor` preset (`SPOOKY_SPEAKER_MONITOR`) tested the speaker as the
default monitor on 2026-10-09:

- with no headphones present, the monitor plays on the speaker;
- inserting headphones moves the monitor to them; removing them returns it to the
  speaker;
- the pot sets the volume on both paths;
- the speaker is silent at the bottom of the pot.

The amplifier runs only while the speaker path is selected and unmuted. A very quiet
pop remains when it starts and stops. The bench ran on a `main`-based image, not on the
`Demo` image.

The user wants the speaker in the demo, so the video can carry the radio and the
Instrument through the room instead of only through headphones. Roadmap demo-track
rule 1 requires every demo-only behavior to be named in an accepted record. No product
document defines a speaker or a default monitor output, so the product choice is open
(rule 5).

Facts that bear on the demo:

- **Band switches.** The demo script includes band switches (0011 item 1). A band
  switch sets the codec's transition mute, which in this build also shuts the
  amplifier down. Each band switch therefore stops and starts the amplifier.
  Classic tuning does not use the transition mute. On the `SpeakerMonitor` image,
  the user heard the pop at band switches only with an ear against the speaker at
  full volume, and judged it acceptable.
- **Microphone bleed.** In a session, the microphone records whatever the speaker
  plays. While PTT is held the radio fades out of the monitor
  ([0028](0028-monitor-only-ptt-without-mic-monitoring.md)), so the speaker is quiet
  while the user speaks. The raw radio and microphone tracks are still recorded
  unchanged (Principle I).
- **Camera feed.** Rig task `p04.8` plans a headphone or line-out feed to the camera
  recorder. Line out now drives the amplifier. A plug in the headphone jack silences
  the speaker. The shoot uses phone cameras, so the user plans to capture audio with
  field recorders or to use the session's own recorded audio, and to settle the
  path by testing.
- **Codec traffic.** A path change or mute change makes up to six checked codec
  register writes in the foreground. These are the same kind of bounded I²C
  transactions the volume service already makes.

## Options

1. **Headphones only, as now.** Rejected by the user.
2. **Speaker only, jack ignored.** Rejected. The jack detect is proven, and headphones
   stay useful for private listening and as a camera feed.
3. **Demo-only speaker code on the demo branch.** Rejected. It would duplicate code
   already built and tested against `main`, and that copy would drift from it.
4. **Chosen: the experiment lands on `main` as an opt-in build option, and the `Demo`
   preset turns it on.** `main`'s other presets are unchanged. The only demo-only
   change is the `Demo` preset setting.
5. **Speaker by default in Debug and Release.** Rejected for now. That is a product
   decision, and it needs a product-intent proposal (rule 5).

## Decision

1. **The demo image monitors on the speaker by default.** Headphones take over while
   they are plugged in. This is provisional and demo-only; it does not settle the
   product's monitor output.
2. **Mechanism.** The `SPOOKY_SPEAKER_MONITOR` option and the `SpeakerMonitor` preset
   land on `main` through the normal gates, off in every other preset. The `Demo`
   preset on `demo/halloween-2026` sets `SPOOKY_SPEAKER_MONITOR=ON`.
3. **Behavior** (as tested on 2026-10-09):
   - Path selection follows the PE0 headphone detect after it is stable for 50 ms.
     The image boots on the path the jack already selects.
   - On the speaker, line out is unmuted, the headphone amplifier is muted, and the
     pot sets the DAC volume over the headphone range (0 to −51.5 dB).
   - On headphones, line out is muted, the DAC is at 0 dB, and the pot sets the
     headphone amplifier as now.
   - Both outputs are muted during every path change.
   - `AMP_SD` is open drain. It is released only while the speaker path is selected
     and unmuted, and it is never driven high.
4. **Band-switch pop.** The amplifier keeps following the transition mute, because
   the user judged the pop at band switches acceptable. The `Demo` bench run (item 5)
   listens again. If the pop is objectionable on the `Demo` image, the transition
   mute leaves the amplifier running and mutes only the codec, while pot mute still
   shuts it down. The general pop work stays in `rmg`.
5. **Qualifying test on the `Demo` image** (demo-track rule 4), recorded in
   `docs/evidence/`. It qualifies the demo image only:
   - speaker by default, headphone takeover and return, the pot on both paths, and
     silence at mute;
   - band switches from the band menu, judged for the pop (item 4);
   - Button 1 PTT and the Instrument voice heard on the speaker;
   - one session recording that includes a headphone insertion, a removal and a pot
     mute crossing, with uninterrupted WAV accounting, no overrun and bounded queues.
6. **Rehearsals.** `p04.9`'s rehearsals and battery-runtime measurement run with the
   speaker in use.
7. **Camera feed** stays with `p04.8`. Its options include recording the speaker
   acoustically, feeding the camera from the headphone jack (which silences the
   speaker by design), or tapping J5 in parallel. This record does not choose.

**Schedule.** The work lands before the 2026-10-21 freeze (0011 item 21). It does not
change any checkpoint gate. The `Demo` setting ends with the demo track on 2026-11-06.

**Principles.**

- **Principle I holds.** The speaker changes only the monitored output. The raw
  tracks are unchanged, and microphone bleed is the room's sound, not processing.
- **Principle II holds.** The log and the outputs agree on which path is live.
- **Principle III holds.** PG8 and the codec stay with the M7.
- **Principle IV holds.** The added codec writes are bounded foreground I²C, and item
  5 tests them during a recording.
- **Principle V.** Item 5 makes the demo evidence demo-image evidence only.
- **Principle VI.** `main`'s Debug and Release are unchanged.
- **Principle VII.** The departure is limited to item 1 and ends with the track.
  Option 1 is the alternative that adds nothing.

## Consequences

- `main` gains an opt-in experiment option, a preset, and an entry in Spooky Bench's
  allowed preset list. Normal images are unaffected.
- In speaker sessions the microphone track contains the monitored radio, except while
  PTT is held. Headphone sessions do not. Anyone reviewing the WAVs should expect the
  difference. The user will try to reduce the bleed through enclosure design; this
  record does not change the firmware for it.
- The speaker draws from `VSYS_RAW`, so loud playback shortens battery runtime; item 6
  measures it.
- `p04.8`'s camera-feed plan must take the speaker into account (item 7).
- When the track ends, the `Demo` setting goes with it. The experiment stays on `main`
  as opt-in until a product decision on a speaker is proposed through product intent.
- Revisit this record if the `Demo` bench run fails, or if the pop or the microphone
  bleed proves unacceptable for the video.

## Evidence

- [Speaker monitor experiment, 2026-10-09](../evidence/2026-10-09-speaker-monitor-experiment.md)
- `Demo`-image evidence (item 5) is pending the demo task.
