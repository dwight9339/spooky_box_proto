# Presentation

How published state and domain events reach the user. Conventions are in the
[model README](README.md); prefix `PRES`. Behavior files say what happened;
this file says how each surface expresses it. The UI can be redesigned here without
changing a machine.

Constitution: Principle I (capture outranks presentation; failures are visible),
Principle II (stable meaning, explicit and consistent acknowledgement), Principle III
(no competing copy of state), Principle VII (semantic facts, not pixel instructions).

## Surfaces

| Surface | Role |
|---|---|
| CLI reply | Text reply to the command's source. The only surface wired today. |
| OLED | Precise state, labels, values and editing context |
| Direct LEDs | Glanceable status, including the record indication |
| LED matrix | Peripheral, expressive feedback about activity, motion and mode identity |
| Audio cue | Short acknowledgement in the monitored mix; never written to raw tracks |

Which core drives each surface is in the [architecture](../architecture.md#application-model)
and [decision 0001](../../decisions/0001-initial-ui-and-bus-ownership.md).

## Rules

| ID | Rule |
|---|---|
| PRES-R1 | Surfaces render published state and domain events only. They keep no competing copy of Context, Session or InputResolution state. |
| PRES-R2 | A surface shows a state only after the machine publishes it. Recording is shown after RecordingStarted, never because StartSession was sent. |
| PRES-R3 | Consequential domain events are acknowledged on display, lights and audio, consistently with one another. They include recording start and stop, band change, calibration, capture and faults. |
| PRES-R4 | RecordingAborted is shown as a visible fault, never as a successful session. |
| PRES-R5 | Under load a surface may lower its frame rate or simplify, but it never delays capture and never changes what a signal means. |
| PRES-R6 | Each engine may express a signal differently; the meaning of the signal stays the same in every engine. |

## Session domain events

| ID | Domain event | Required acknowledgement | CLI reply today |
|---|---|---|---|
| PRES-SES-01 | RecordingStarted | Display, lights and audio | `OK RECORD START file=... duration=...s format=...` |
| PRES-SES-02 | RecordingRejected | Display, lights and audio, with the reason visible | `ERR RECORD <reason>` |
| PRES-SES-03 | RecordingStopping | Display, lights and audio | `OK RECORD STOP requested; finalizing next matched block` |
| PRES-SES-04 | RecordingCompleted | Display, lights and audio | `OK RECORD PASS file=... frames=... bytes=...` |
| PRES-SES-05 | RecordingAborted | Display, lights and audio, as a fault that names the file and whether it was finalized | `ERR RECORD ABORT file=... reason=... finalized=...` |
| PRES-SES-06 | StopIgnored | Reply to the command's source | `OK RECORD already idle` |

## Published session state

| Field | CLI today |
|---|---|
| Session state | `RECORD STATUS` replies `OK RECORD ACTIVE` or `OK RECORD IDLE` |
| File name | `RECORD STATUS` |
| Written audio | `RECORD STATUS` and periodic `RECORD progress` lines |
| Requested duration | The `OK RECORD START` reply only |
| Queue depths and longest SD write | `RECORD STATUS`, periodic `RECORD progress` lines and the final `RECORD DIAG` line |

## Radio domain events

| ID | Domain event | Required acknowledgement | CLI reply today |
|---|---|---|---|
| PRES-RAD-01 | RadioStarted | Display | Boot log `[radio] tuned FM ...` and `[radio] digital output enabled ...` |
| PRES-RAD-02 | TuneStarted | Display | None |
| PRES-RAD-03 | Tuned | Display | `OK RADIO BAND=... FREQ=... RSSI=... SNR=... VALID=...` |
| PRES-RAD-04 | TuneRejected | Reply to the command's source | `ERR <band> range: <min>..<max> kHz` |
| PRES-RAD-05 | TuneFailed | Display, with the target and reason | `ERR RADIO tune failed` |
| PRES-RAD-06 | BandTransitionStarted | Display, lights and audio | None |
| PRES-RAD-07 | BandChanged | Display, lights and audio | `OK RADIO BAND=... FREQ=... RSSI=... SNR=... VALID=...` |
| PRES-RAD-08 | RadioFault | Display, lights and audio, as a fault | `ERR RADIO band switch failed; reset required`, or a `[bridge] FAIL` log line; the red board LED turns on |
| PRES-RAD-09 | RadioCommandRejected | Reply to the command's source | `ERR RADIO audio path is not running` |

## Session prompt and button lights

The standalone button LEDs render published Session state and the physical press state
of their buttons. Brightness is perceived brightness, CIE L\* lightness, converted to
PWM duty through the inverse L\* curve; 10% perceived is roughly 1% duty. The breathing
period stays configurable until bench trials set it.

| ID | Surface | When | Expression |
|---|---|---|---|
| PRES-LED-01 | Button 0 and Button 1 LEDs | Button not pressed, not `in(Session.Active)` | Steady at 10% perceived brightness |
| PRES-LED-02 | Button 0 and Button 1 LEDs | Button pressed | Steady at 100% |
| PRES-LED-03 | Button 0 LED | Button not pressed, `in(Session.Active)` | Slow breathing, which continues through finalizing |
| PRES-LED-04 | Button 1 LED | Button not pressed, `in(Session.Active)` | Steady at 10% perceived brightness, as when idle |
| PRES-LED-05 | Button 0 LED | RecordingRejected or RecordingAborted | A distinct fault pattern, starting as three fast blinks, then the rule for the current state resumes. Blink timing stays configurable. |
| PRES-LED-06 | Encoder 0 LED | From `INP_PUB_PROMPT_OPENED_START` or `_STOP` until the prompt closes | Lit, to show where to confirm. Its color follows the semantic color vocabulary, control map D-016. |
| PRES-PRM-01 | Display | `INP_PUB_PROMPT_OPENED_START` or `INP_PUB_PROMPT_OPENED_STOP` | A prompt naming the action and the Encoder 0 confirm |
| PRES-PRM-02 | Display | `INP_PUB_PROMPT_CONFIRMED_START`, `INP_PUB_PROMPT_CONFIRMED_STOP`, `INP_PUB_PROMPT_CANCELLED` or `INP_PUB_PROMPT_WITHDRAWN` | The prompt closes. The session's own events carry the outcome. |

## Device domain events

| ID | Domain event | Required acknowledgement | CLI reply today |
|---|---|---|---|
| PRES-DEV-01 | SleepEntered | Reply to the command's source before USB stops | `OK SLEEP START; CDC will disconnect; updates continue on AUX UART7` |
| PRES-DEV-02 | SleepRejected | Reply to the command's source | `ERR SLEEP unavailable in IPC smoke build; use Debug` |
| PRES-DEV-03 | UtilityOpened, UtilityClosed | Display | None |

## Open behavior

| Item | Question | Settled by |
|---|---|---|
| Session events on OLED, LEDs, matrix and audio | What does each surface show for each session event, and how are failures made visible? | `full_spooky_proto-54w.1` |
| EMF, activity and warning expression | How do the matrix and LEDs express semantic EMF, radio activity and warnings? | `full_spooky_proto-54w.8` |
| Utility entry and exit | What does the display show when a utility opens or closes? | `full_spooky_proto-54w.2` |
| Band change during a session | How are band changes and radio faults acknowledged on display, lights and audio while recording? | `full_spooky_proto-54w.1`, [decision 0003](../../decisions/0003-radio-control-during-recording.md) |
| Swallowed presses | Does a button that is pressed while its gesture is swallowed or dismissed still light at 100%? | `full_spooky_proto-54w.18` |
| Prompt audio | Is opening, confirming or cancelling the prompt acknowledged with sound? | `full_spooky_proto-54w.1` |
| Transition noise | Should tunes and band transitions play generated noise in the monitored mix, with user-tunable level, color and duration? It would never reach a stored raw track or the activity metrics. | `full_spooky_proto-54w.16`; product open decision 16 |

## Maturity

| ID | Status | Basis |
|---|---|---|
| PRES-R1 to PRES-R6 | Target | Constitution Principles I, II, III and VII; product [display and matrix roles](../../../spec/product/modes-and-interaction.md#display-and-led-matrix-roles) |
| PRES-SES-01 | Proven for the CLI reply | [Radio regression](../../evidence/2026-09-24-radio-regression.md) |
| PRES-SES-02 | Target | CLI replies implemented; no bench evidence |
| PRES-SES-03 | Target | CLI reply implemented; no bench evidence |
| PRES-SES-04 | Proven for the CLI reply | [Radio regression](../../evidence/2026-09-24-radio-regression.md) |
| PRES-SES-05 | Target | CLI reply implemented; no bench evidence |
| PRES-SES-06 | Target | CLI reply implemented; no bench evidence |
| PRES-RAD-01 | Proven for the boot log | [Radio regression](../../evidence/2026-09-24-radio-regression.md) |
| PRES-RAD-02 | Target | Not implemented |
| PRES-RAD-03 | Proven for the CLI reply | [Radio regression](../../evidence/2026-09-24-radio-regression.md) |
| PRES-RAD-04 | Proven for the CLI reply | [Radio regression](../../evidence/2026-09-24-radio-regression.md) |
| PRES-RAD-05 | Target | CLI reply implemented without a reason; no bench evidence |
| PRES-RAD-06 | Target | Not implemented |
| PRES-RAD-07 | Proven for the CLI reply | [Radio regression](../../evidence/2026-09-24-radio-regression.md) |
| PRES-RAD-08 | Target | CLI reply and log implemented; no bench evidence |
| PRES-RAD-09 | Target | CLI reply implemented; no bench evidence |
| PRES-LED-01 to PRES-LED-03 | Target | User direction 2026-09-27, recorded in `full_spooky_proto-54w.18`. Not implemented; the LED pins were chosen for PWM-capable timers. |
| PRES-LED-04 to PRES-LED-06 | Target | [Decision 0005](../../decisions/0005-button-0-session-prompt.md), items 14 to 16. Not implemented. |
| PRES-PRM-01, PRES-PRM-02 | Target | User direction 2026-09-27, `full_spooky_proto-54w.18`. Not implemented. |
| PRES-DEV-01 | Proven for the CLI reply | [Radio regression](../../evidence/2026-09-24-radio-regression.md) |
| PRES-DEV-02 | Target | CLI reply implemented; no bench evidence |
| PRES-DEV-03 | Target | Product intent; no mode state exists in firmware |
