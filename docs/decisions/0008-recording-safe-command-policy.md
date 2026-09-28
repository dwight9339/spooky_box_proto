# 0008. Recording-safe command policy

- **Status:** Accepted 2026-09-28
- **Date:** 2026-09-28
- **Supersedes:** none
- **Beads:** `full_spooky_proto-54w.1` (this policy), `full_spooky_proto-8lw.4` (command
  policy and Session authority), `full_spooky_proto-8lw.11` (sleep during a session),
  `full_spooky_proto-54w.6` and `full_spooky_proto-54w.12` (radio qualification),
  `full_spooky_proto-hpq.3` (session folders)

## Context

Principle I requires every action that would interrupt, replace or invalidate an active
session to be classified as allowed, deferred or rejected, and to need explicit
confirmation or be unavailable. Principle II requires consequential transitions to have
explicit outcomes that display, lights and audio acknowledge consistently. Principle
III requires the CLI and the physical controls to share one command policy. Product
open decision 12 asks how the device behaves when a top-level mode change is requested
during recording.

[Decision 0005](0005-button-0-session-prompt.md) settled the physical start and stop
gesture and left the duration of a physical start open. [Decision
0003](0003-radio-control-during-recording.md) makes radio control a target in every
session state, behind today's guard until `54w.6` and `54w.12` qualify it, and
[decision 0004](0004-radio-track-continuity-across-transitions.md) keeps the radio
track's timeline through receiver transitions.

Today, firmware rejects SD maintenance, WAV transfer and tune or band commands while
recording. It accepts `SLEEP START`, which silently ends the session
(`full_spooky_proto-8lw.11`). A session lasts 1 to 3600 seconds, 60 by default, and
lives in one WAV file.

The user gave direction on 2026-09-28: sessions run until the user stops them or the
card is full; sleep and mode changes are not allowed during a session; Field scan-engine
switches and some device-level settings stay available, possibly through an allow list;
Instrument engine switches are probably disallowed; Playback is off limits. The user
asked for a proposal on radio events.

## Options

1. **Allow everything and rely on confirmation prompts.** Rejected. Prompts during
   performance are slow, and a confirmed mode change would still end or split a
   session the user expected to continue.
2. **Reject everything that is not needed to keep recording.** Rejected. Spooky Box is
   a scanning radio that records; freezing engines and settings would make recording
   stop the instrument from working (product intent, Field Sessions).
3. **Defer blocked actions until the session ends.** Rejected. An action that runs
   minutes after it was requested surprises the user and can change the device while
   they are not looking.
4. **A fixed table: actions that keep the session's meaning are allowed, actions that
   would end, split or re-purpose it are rejected with a visible reason, and settings
   are rejected unless allow-listed.** Chosen.

## Decision

Items marked *(agent)* were proposed by the agent to close gaps in the user's direction.
Everything else is the user's direction. The user reviewed the draft on 2026-09-28,
directed the capture-save and Shift-lighting behavior in items 7 and 8, and confirmed
the calibration block, the one-minute reserve and the rejection acknowledgement.

**Session length and endings**

1. A physical start opens a session with no duration limit. It runs until the user
   stops it or the card becomes full.
2. *(agent)* `RECORD START` without a duration behaves like the physical start.
   `RECORD START <seconds>` keeps timed sessions for bench tests; its 1 to 3600 second
   range is unchanged.
3. *(agent)* The card counts as full when the space left would not hold the reserve:
   one more minute of session audio, one rolling-capture save of the configured window
   length (`full_spooky_proto-hpq.2`), and what finalization needs. The session then
   stops at the next matched block, finalizes the file, and ends with reason
   `card full`, shown as a visible fault as Principle I requires for a full card, never
   as a clean completion. The file must be finalized before any write can fail for lack
   of space, and a capture save stays possible until the session stops. The one-minute
   margin is a configurable constant (Principle IV).
4. *(agent)* A single WAV file cannot exceed 4 GiB, about 4 hours 8 minutes at 288,000
   bytes per second. Until session folders exist (`full_spooky_proto-hpq.3`), a session
   that reaches that limit ends cleanly with reason `file size limit`. With session
   folders, the session continues into the next file of the same session without a
   gap in either track, so only a stop or a full card ends it.
5. Card removal, a failed write, a DMA error or a queue overrun still end the session as
   capture faults, as today.

**Actions during a session**

6. Every action has exactly one class while a session is active. No action is deferred
   until the session ends.

| Action | Class | Source |
| --- | --- | --- |
| Stop the session through the Button 0 prompt or `RECORD STOP` | Allowed | Decision 0005 |
| Start another session | Rejected | Existing behavior |
| Sleep (`SLEEP START`) | Rejected | User |
| Operating mode change, Field to Instrument or back (Shift plus Button 0) | Rejected | User |
| Field scan-engine switch, including the Manual quick-jump and its return | Allowed | User |
| Instrument engine switch | Rejected | User, leaning; revisit after hands-on trials |
| Instrument performance views, parameter pages and encoder edits | Allowed | *(agent)* A view shapes the active engine without re-purposing the session |
| PTT (Button 1 in Field) | Allowed, timestamped in the session | Product intent |
| Save the rolling capture (Shift plus Button 1) | Allowed | User; preserving an event never harms the session |
| Save, switch mode and load capture chord (Shift plus Buttons 0 and 1) | The save runs; the mode switch is rejected | *(agent)* A user who presses the full chord still keeps the event |
| Open Playback | Rejected | User |
| Open Settings | Allowed; only allow-listed settings can change | User |
| SD maintenance, SD tests and WAV transfer | Rejected | Existing behavior |
| Status and diagnostic reads (`RECORD`, `DIAG`, `LOG`, `IPC`, `SD STATUS`, `STATUS`, `VOLUME`) | Allowed | Existing behavior |
| UI bring-up test patterns (`UI LEDS`, `UI MATRIX ANIMATE`, `UI DISPLAY TEST`) | Rejected | *(agent)* Test patterns replace the semantic display and load the buses during capture |
| EMF calibration (`EMF ZERO`) | Rejected | *(agent)* A mid-session baseline change alters what the EMF stream means (Principle II) |
| Radio commands | See radio events below | Decision 0003 |

7. **Saving a capture during a session.** The user saves the rolling capture with
   Shift plus Button 1 alone; the mode-switch chord is not needed and cannot run.
8. **Shift lighting.** While Shift is held, only the controls with an available action
   are lit. During a session Button 1 is lit for the capture save and Button 0 is dark,
   because its Shift action, the mode switch, is unavailable. Available and unavailable
   Shift actions are settled by this table; the lighting itself follows
   [presentation.md](../design/behavior/presentation.md).

9. *(agent)* **Settings allow list.** A setting may change during a session only if it
   is on the allow list. Every new setting is rejected until it is added. The first
   list holds settings that change presentation or monitoring and never capture,
   storage or the meaning of a recorded stream:
   - display and LED matrix brightness;
   - monitored-mix settings, such as monitor level and transition-noise level if that
     feature is adopted (`full_spooky_proto-54w.16`);
   - read-only pages: storage status, battery and system information.

   Settings that change the audio format, storage, time, calibration, sensor policy or
   firmware are not on the list. Settings that are not allowed are shown locked, with
   the reason.

**Acknowledging a rejection** *(agent)*

10. A rejected action changes nothing and says why, the same way everywhere. The display
   shows the action and "unavailable while recording" briefly, then returns to the
   current view. The CLI replies `ERR <area> unavailable while recording`, matching
   today's replies. Lights and audio are unchanged, so a rejection never sounds or
   looks like a fault or a session event in the monitored experience. The rejection is
   logged as a numeric diagnostic event.

**Radio events during a session** *(agent)*

11. Once `54w.6` and `54w.12` qualify them, radio commands are allowed in every session
   state (decision 0003). Until then today's rejection stays (Principle VI).
12. In-band tuning, tune steps and scan motion are routine. They change the published
    frequency on the display and are timestamped on the session timeline, with no extra
    light or audio acknowledgement, so scanning does not become noisy.
13. A band change is a consequential transition. Display, lights and audio acknowledge
    its start and its outcome together: the display shows the target band and then the
    result, the LED matrix shows a short band cue, and the monitored mix plays a short
    cue that is never written to a raw track. The cue's sound is settled with
    `full_spooky_proto-54w.16`. The radio track keeps its timeline, the gap is written
    as silence, and its start and end are published with sample positions (decision
    0004).
14. A radio control failure, such as a failed tune or band switch, does not end the
    session. The microphone and other streams keep recording. The radio track carries
    silence while the receiver is not delivering audio, the gap is marked on the
    session timeline, and a radio fault stays visible on display and lights until the
    radio recovers. A radio audio stream failure, a SAI or DMA error or a queue
    overrun, still ends the session as a capture fault until a recovery path is
    qualified.
15. While a tune or band transition is in progress, a newer radio command replaces any
    pending one (latest wins), so scanning never builds a backlog. A command is never
    silently dropped: a replaced command is simply superseded, and a rejected one is
    answered.

**Implementation**

16. The policy is enforced in one place, the Session machine's command handling with
    authority (`full_spooky_proto-8lw.4`), for both the CLI and the physical controls.
    Host tests cover every row of the table and each acknowledgement.

## Consequences

- Sessions can last as long as the card allows, which makes the card-full ending and,
  after `hpq.3`, file continuation part of the recording path and its bench tests.
- `8lw.11` is settled: sleep is rejected while a session is active.
- Product open decision 12 is answered for sessions. Protected product documents need
  their own approved proposal to record it: the modes-and-interaction open decisions
  and Field Sessions text, and control-map rows for Shift plus Button 0 and the
  save-switch-load chord during a session.
- The CLI contract gains open-ended `RECORD START`, the `card full` and `file size
  limit` endings, and `ERR SLEEP unavailable while recording` when `8lw.4` lands.
- Keeping radio control faults from ending a session depends on the silence-filled
  radio track of decision 0004, which `54w.12` implements.
- Presentation gains a Shift-lighting rule: lit controls are exactly those with an
  available Shift action in the current context.
- The card-full reserve depends on the rolling-capture window, so `hpq.2` must fix
  that window's size before long sessions are qualified.
- Revisit if hands-on trials show that Instrument engine switching during a session is
  wanted, that a rejected action needs a sound, or that the allow list is too narrow to
  perform with.

## Evidence

None yet. Host tests for the action table and the Shift lighting belong to
`full_spooky_proto-8lw.4`. Bench
tests for the card-full ending and long sessions belong to the recording-baseline work
under `full_spooky_proto-jjy`.
