# 0028. Monitor-only PTT without microphone monitoring

- **Status:** Accepted 2026-10-08
- **Date:** 2026-10-08
- **Supersedes:** [0011](0011-halloween-2026-demo-build.md) item 10, as far as it puts
  microphone monitoring in `54w.9`. Its monitor-only PTT and the open PTT stamps stand.
- **Beads:** `full_spooky_proto-54w.9`; storage of PTT stamps `full_spooky_proto-hpq.3`

## Context

Button 1 is PTT in Field (C-016, C-017; [decision 0009](0009-first-slice-field-controls.md)
item 37): from press to release it silences the radio in the monitored mix, while both
raw tracks keep recording (constitution Principle I). The behavior model already turns
PTT on and off, including the release on reconciliation, and the command policy allows
it during a session ([decision 0008](0008-recording-safe-command-policy.md)). The
monitor stage, `RenderMonitor` in `CM7/App/audio_path_service.c`, was a copy of the raw
radio, so PTT had no effect.

Three sources also put the microphone into the monitored mix: C-016's result ("Mic
remains audible"), [decision 0011](0011-halloween-2026-demo-build.md) item 10 ("mic
monitoring and monitor-only PTT") and the `54w.9` task text. The modes and interaction
document does not: while PTT is held, the radio is silenced in live monitoring and both
tracks keep recording.

The user's direction on 2026-10-08 was not to play the microphone back through the live
monitor, even while PTT is held, and to reconsider it after the demo as a possible user
setting.

Facts that bear on microphone monitoring, had it been kept:

- On `main` the microphone streams only during a recording; its DMA start defines the
  recording's alignment ([decision 0012](0012-common-audio-sample-timeline.md) item 4).
  Monitoring it outside sessions would change that start or wait for rolling capture
  (`hpq.5`).
- It arrives in 4,096-frame blocks (about 85 ms). Monitoring it without that delay
  needs a tap on its DMA buffer from the radio interrupt.

Principles involved: I (monitoring never changes the raw capture), II (PTT means the
same thing wherever it applies), IV (the fade and the stamp bound come from
arithmetic and stay configurable), VII (a narrow slice).

## Options

1. **Mix the microphone into the monitor, as 0011 item 10 says.** Rejected by the
   user. It also needs the microphone stream outside sessions and a low-latency tap.
2. **Chosen: PTT fades the radio out of the monitor; the microphone is not monitored.**
3. **Mute the radio at once, with no fade.** Rejected: a step from full scale to zero
   clicks.

## Decision

1. **The microphone is not in the monitored mix,** in any mode or state. It is
   recorded as before. Microphone monitoring may return later as a user setting,
   through a new record.
2. **PTT fades the radio out of the monitor and back.** On press the radio's monitor
   gain falls linearly to silence over 240 frames (5 ms at 48 kHz, the same as an
   engine switch in [decision 0027](0027-instrument-engine-switching.md)), and on
   release it rises back to unity over the same time. A release during a fade turns
   back from the gain reached. The fade length stays a build constant,
   `SPOOKY_PTT_RAMP_FRAMES`, until a bench sets it.
3. **Only the radio is faded.** The fade applies to the monitored radio, after the raw
   capture has been copied. Where something else replaces the radio in the monitor
   (the demo image's Instrument clip), PTT does not change it.
4. **PTT changes are stamped on the radio timeline.** Each press and release is stamped
   when the foreground applies it, with the foreground observation bound of decision
   0012 item 6 (3,600 frames). The input path's own latency before that is not
   included. The latest stamps and the counts are reported by `MONITOR`. Writing the
   stamps into a session belongs to the session format, `hpq.3`.
5. **The CLI uses the same command.** `MONITOR PTT ON|OFF` sets PTT through the same
   command-policy action as Button 1 (`COMMAND_ACTION_PTT`, allowed in every session
   state). A CLI press has no reconciliation; `MONITOR PTT OFF` ends it.

## Consequences

- The control map's C-016 result needs an approved amendment to drop "Mic remains
  audible". That proposal is separate from this record.
- `54w.9`'s acceptance criteria lose the microphone checks (audible microphone, gain
  and clipping of the mix). Its remaining checks: the raw radio is retained in the
  WAV, PTT is silent in the monitor and clean at both edges, a lost release clears it,
  and the stamps are reported.
- With no mix, the monitor cannot clip: the fade only lowers the gain.
- Storing PTT intervals in sessions, and the "PTT aware" playback that uses them, wait
  for `hpq.3`.
- Revisit this record if microphone monitoring is wanted as a setting, or if a bench
  finds the 5 ms fade audible as a click or too slow.

## Evidence

- Host tests: `tests/monitor_ptt_test.c` (fade length, step size, mid-fade release,
  fades across buffers). Host results are not evidence of what is heard.
- Bench evidence follows under `54w.9`.
