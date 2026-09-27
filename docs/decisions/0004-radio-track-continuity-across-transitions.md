# 0004. Radio-track continuity across receiver transitions

- **Status:** Accepted 2026-09-27
- **Date:** 2026-09-27
- **Supersedes:** none
- **Beads:** `full_spooky_proto-54w.14` (proposal), `full_spooky_proto-54w.12` (band
  transitions), `full_spooky_proto-hpq.1` (sample timeline)

## Context

[Decision 0003](0003-radio-control-during-recording.md) makes band transitions legal
during a session. That is safe only if a transition cannot shift the radio track
against the microphone track.

Facts from the current code:

- SAI2 block A is the clock master of the Si4735 digital audio link and runs circular
  DMA (`AudioPath_StartCapture` in `CM7/App/audio_path_service.c`). Radio half-buffers
  of 512 frames keep arriving while the Si4735 is powered down for a band switch.
- The band-switch wrapper in `CM7/Core/Src/main.c` mutes the codec and closes the
  stream gate for the whole switch. While the gate is closed, the half-buffer handler
  skips both the recorder hand-off and the monitor render.
- In-band tunes do not close the gate. The receiver keeps producing audio while it
  retunes.
- Radio SAI, codec SAI and the DFSDM microphone all derive from PLL3P (`hpq.1` design
  notes), so counting blocks is a valid shared timeline.

If a band switch happened during a session today, the microphone would keep producing
blocks while the radio produced none. The recorder pairs radio and microphone blocks in
order, so every later radio block would be written against a later microphone block: a
permanent shift equal to the gap. A gap longer than the queue budget of about 683 ms
would also overflow the microphone queue and abort the session.

Principles involved: I (raw tracks preserved non-destructively), II (no fabricated
meaning), IV (bounded ISR work).

## Options

1. **Drop radio samples during the gap, as the gate does today.** Rejected. It breaks
   radio and microphone alignment for the rest of the session.
2. **Stop and restart radio DMA around the switch.** Rejected. Each restart moves the
   radio phase against the microphone by up to one DMA half, about 10.7 ms, and adds its
   own failure paths.
3. **Store whatever the link carries while the receiver is powered down.** Rejected.
   Undriven data is not radio audio, and storing it as such fabricates signal.
4. **Store generated noise in the gap.** Rejected. Generated sound is a monitoring
   choice and must not enter a raw track.
5. **Substitute digital silence for each gated radio half-buffer, and mark the gap
   with events.** Chosen.

## Decision

- While the stream gate is closed, each radio half-buffer the SAI delivers is replaced
  by digital silence of the same length before it reaches the recorder. The radio and
  microphone tracks keep the same block count through every transition.
- The session events record the start and end of each gap on the session sample
  timeline, with its cause. Playback and analysis treat the interval as "no radio
  signal", not as quiet radio. Activity metrics report no measurement for it.
- The monitored mix during the gap is silence. Generated transition noise may replace
  it in the monitored mix only, if that feature is accepted (`54w.16`). Nothing
  generated reaches a stored raw track.
- Receiver output during an in-band tune is stored as received, including any switching
  transient, because it is real receiver output. Tune events on the timeline mark where
  those transients occur.

## Consequences

- Band transitions during a session no longer shift or abort the recording by
  themselves; timing still has to fit the queue budget (`54w.6`, `54w.12`).
- The session event format (`hpq.3`) needs gap start and end events with sample
  positions.
- The hardware codec mute during a switch may become unnecessary once the monitor
  receives silence. Whether it can be removed without audible pops is a bench question.
- A consumer that reads only the WAV sees silence in the gap; only the event stream says
  why. Spooky Bench alignment checks must tolerate marked gaps.
- Revisit if the Si4735 cannot be switched without disturbing the SAI clocking, which
  would make option 2 unavoidable.

## Evidence

None yet. Qualification under `54w.12` needs repeated band switches during a recording
with exact block accounting, and loopback alignment unchanged across transitions
([WAV alignment procedure](../procedures/spooky-bench-wav-alignment.md)).
