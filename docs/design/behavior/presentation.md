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
| PRES-R2 | A surface shows a state only after the machine publishes it. Recording is shown after `SES_PUB_RECORDING_STARTED`, never because StartSession was sent. |
| PRES-R3 | Consequential domain events are acknowledged on display, lights and audio, consistently with one another. They include recording start and stop, band change, calibration, capture and faults. |
| PRES-R4 | `SES_PUB_RECORDING_ABORTED` and `SES_PUB_RECORDING_CARD_FULL` are shown as visible faults, never as successful sessions. |
| PRES-R5 | Under load a surface may lower its frame rate or simplify, but it never delays capture and never changes what a signal means. |
| PRES-R6 | Each engine may express a signal differently; the meaning of the signal stays the same in every engine. |

## Session domain events

| ID | Domain event | Required acknowledgement | CLI reply today |
|---|---|---|---|
| PRES-SES-01 | `SES_PUB_RECORDING_STARTED` | Display, lights and audio | `OK RECORD START file=... duration=...s format=...` |
| PRES-SES-02 | `SES_PUB_RECORDING_REJECTED` | Display, lights and audio, with the reason visible | `ERR RECORD <reason>` |
| PRES-SES-03 | `SES_PUB_RECORDING_STOPPING` | Display, lights and audio | `OK RECORD STOP requested; finalizing next matched block` |
| PRES-SES-04 | `SES_PUB_RECORDING_COMPLETED` | Display, lights and audio | `OK RECORD PASS file=... frames=... bytes=...` |
| PRES-SES-05 | `SES_PUB_RECORDING_ABORTED` | Display, lights and audio, as a fault that names the file and whether it was finalized | `ERR RECORD ABORT file=... reason=... finalized=...` |
| PRES-SES-06 | `SES_PUB_STOP_IGNORED` | Reply to the command's source | `OK RECORD already idle` |
| PRES-SES-07 | `SES_PUB_RECORDING_CARD_FULL` | Display, lights and audio, as a card-full fault; retain the valid finalized file | `ERR RECORD ABORT file=... reason=card full finalized=1` |
| PRES-SES-08 | `SES_PUB_RECORDING_FILE_LIMIT` | Display, lights and audio, as a clean single-file ending | `OK RECORD PASS file=... reason=WAV size limit` |
| PRES-SES-09 | `SES_PUB_RECORDING_PREPARING` | Reply to the command's source; a surface may show that a start is pending, never that recording has begun (PRES-R2) | `OK RECORD PREPARING prealloc-kib=...`, then `OK RECORD PREPARED file=...` with step timings |
| PRES-SES-10 | `SES_PUB_RECORDING_CANCELLED` | Display, lights and audio: the start was cancelled and nothing was recorded | `OK RECORD STOP cancelled before capture; no file kept` |

## Published session state

| Field | CLI today |
|---|---|
| Session state | `RECORD STATUS` replies `OK RECORD PREPARING`, `OK RECORD ACTIVE` or `OK RECORD IDLE` |
| File name | `RECORD STATUS` |
| Written audio | `RECORD STATUS` and periodic `RECORD progress` lines |
| Requested duration | The `OK RECORD START` reply only |
| Queue depths and longest SD write | `RECORD STATUS`, periodic `RECORD progress` lines and the final `RECORD DIAG` line |

## Radio domain events

| ID | Domain event | Required acknowledgement | CLI reply today |
|---|---|---|---|
| PRES-RAD-01 | `RAD_PUB_STARTED` | Display | Boot log `[radio] tuned FM ...` and `[radio] digital output enabled ...` |
| PRES-RAD-02 | `RAD_PUB_TUNE_STARTED` | Display | None |
| PRES-RAD-03 | `RAD_PUB_TUNED` | Display | `OK RADIO BAND=... FREQ=... RSSI=... SNR=... VALID=...` |
| PRES-RAD-04 | `RAD_PUB_REJECTED_RANGE` | Reply to the command's source | `ERR <band> range: <min>..<max> kHz` |
| PRES-RAD-05 | `RAD_PUB_TUNE_FAILED` | Display, with the target and reason | `ERR RADIO tune failed` |
| PRES-RAD-06 | Band switch started; not published while the switch is one synchronous action (`full_spooky_proto-54w.12`) | Display, lights and audio | None |
| PRES-RAD-07 | `RAD_PUB_BAND_CHANGED` | Display, lights and audio | `OK RADIO BAND=... FREQ=... RSSI=... SNR=... VALID=...` |
| PRES-RAD-08 | `RAD_PUB_FAULT_START`, `RAD_PUB_FAULT_BAND`, `RAD_PUB_FAULT_AUDIO` | Display, lights and audio, as a fault | `ERR RADIO band switch failed; reset required`, or a `[bridge] FAIL` or `[radio] audio fault` log line; the red board LED turns on |
| PRES-RAD-09 | `RAD_PUB_REJECTED_UNAVAILABLE` | Reply to the command's source | `ERR RADIO audio path is not running` |
| PRES-RAD-10 | `RAD_PUB_SUPERSEDED` | Reply to the command's source | `OK RADIO SUPERSEDED` |
| PRES-RAD-11 | `RAD_PUB_ABANDONED` | Reply to the command's source | `ERR RADIO abandoned; radio fault` |

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
| PRES-LED-05 | Button 0 LED | `SES_PUB_RECORDING_REJECTED` or `SES_PUB_RECORDING_ABORTED` | A distinct fault pattern, starting as three fast blinks, then the rule for the current state resumes. Blink timing stays configurable. |
| PRES-LED-06 | Encoder 0 LED | From `INP_PUB_PROMPT_OPENED_START` or `_STOP` until the prompt closes | Lit, to show where to confirm. Its color follows the semantic color vocabulary, control map D-016. |
| PRES-PRM-01 | Display | `INP_PUB_PROMPT_OPENED_START` or `INP_PUB_PROMPT_OPENED_STOP` | A prompt naming the action and the Encoder 0 confirm |
| PRES-PRM-02 | Display | `INP_PUB_PROMPT_CONFIRMED_START`, `INP_PUB_PROMPT_CONFIRMED_STOP`, `INP_PUB_PROMPT_CANCELLED` or `INP_PUB_PROMPT_WITHDRAWN` | The prompt closes. The session's own events carry the outcome. |

## Device domain events

| ID | Domain event | Required acknowledgement | CLI reply today |
|---|---|---|---|
| PRES-DEV-01 | SleepEntered | Reply to the command's source before USB stops | `OK SLEEP START; CDC will disconnect; updates continue on AUX UART7` |
| PRES-DEV-02 | SleepRejected | Reply to the command's source | `ERR SLEEP unavailable while recording`, or `ERR SLEEP unavailable in IPC smoke build; use Debug` |
| PRES-DEV-03 | `CTX_PUB_UTILITY_OPENED`, `CTX_PUB_UTILITY_CLOSED` | Display | None |

## Navigation and Shift domain events

Published by [ContextSm.puml](ContextSm.puml) and
[InputResolutionSm.puml](InputResolutionSm.puml) under
[decision 0009](../../decisions/0009-first-slice-field-controls.md).

| ID | Domain event | Required acknowledgement | CLI reply today |
|---|---|---|---|
| PRES-CTX-01 | `CTX_PUB_MODE_CHANGED` | Display, lights and audio: a top-level mode change is a consequential transition | None |
| PRES-CTX-02 | `CTX_PUB_ENGINE_CHANGED` | Display | None |
| PRES-CTX-03 | `CTX_PUB_PAGE_CHANGED` | Display and the page color of the encoder lights | None |
| PRES-CTX-04 | `CTX_PUB_MENU_OPENED`, `CTX_PUB_MENU_HIGHLIGHT`, `CTX_PUB_MENU_CLOSED`, `CTX_PUB_MENU_WITHDRAWN` | Display: the engine or band menu, its highlight, and its closing | None |
| PRES-CTX-05 | `CTX_PUB_ACTION_REJECTED` | Display only: the action and "unavailable while recording" briefly, then the current view. Lights and audio are unchanged ([decision 0008](../../decisions/0008-recording-safe-command-policy.md) item 10). | None |
| PRES-CTX-06 | PTT on and off (`CTX_CMD_MONITOR_PTT`, C-016, C-017) | Audio: the radio fades out of the monitored mix and back in ([decision 0028](../../decisions/0028-monitor-only-ptt-without-mic-monitoring.md)). Display and lights are open. | `OK MONITOR PTT=...` for `MONITOR PTT ON` and `OFF` |
| PRES-INP-01 | `INP_PUB_SHIFT_ENTERED`, `INP_PUB_SHIFT_LEFT` | Lights: while Shift is held, only the controls with an available Shift action are lit ([decision 0008](../../decisions/0008-recording-safe-command-policy.md) item 8) | None |

## Classic scan engine events

Published by the Classic service (`CM7/App/classic_service.c`) under
[spec 001](../../../spec/specs/001-classic-scan-engine/spec.md) and
[decision 0016](../../decisions/0016-classic-scan-motion.md). Each event carries the
state after the change. Routine jumps change only the frequency and are not events
([decision 0008](../../decisions/0008-recording-safe-command-policy.md) item 12).

| ID | Domain event | Required acknowledgement | CLI and log today |
|---|---|---|---|
| PRES-CLS-01 | `CLASSIC_PUB_RUN_STATE` | Display: running, holding, paused, sweep complete, or unable to scan with the reason. Unable to scan is never shown as running or holding (spec FR-027); holding is distinct from paused (FR-020). | `[classic] ... RUN_STATE` log line; `OK CLASSIC STATE=... REASON=...` to a CLI command |
| PRES-CLS-02 | `CLASSIC_PUB_DIRECTION` | Display, including a bounce reversal | `[classic] ... DIRECTION` log line |
| PRES-CLS-03 | `CLASSIC_PUB_RATE` | Display: the rate in effect, and that it is limited when the band's maximum applies | `[classic] ... RATE` log line |
| PRES-CLS-04 | `CLASSIC_PUB_DISTANCE` | Display: channels and kHz | `[classic] ... DISTANCE` log line |
| PRES-CLS-05 | `CLASSIC_PUB_EDGE` | Display | `[classic] ... EDGE` log line |
| PRES-CLS-06 | `CLASSIC_PUB_HOLD_TIME` | Display: the hold time, or that the hold is off | `[classic] ... HOLD_TIME` log line |

## Published Classic state

| Field | CLI today |
|---|---|
| Run state and unable reason | `CLASSIC` replies `OK CLASSIC STATE=... REASON=...` |
| Band, frequency, channel index and count | `CLASSIC` (`BAND`, `FREQ`, `CH`) |
| Direction, edge behavior | `CLASSIC` (`DIR`, `EDGE`) |
| Rate in effect, setting, limited | `CLASSIC` (`RATE`, `SET`, `LIMITED`) |
| Jump distance in channels and kHz | `CLASSIC` (`DIST`, `DIST_KHZ`) |
| Hold time | `CLASSIC` (`HOLD`) |

## Open behavior

| Item | Question | Settled by |
|---|---|---|
| Session events on OLED, LEDs, matrix and audio | What does each surface show for each session event, and how are failures made visible? | `full_spooky_proto-54w.1` |
| EMF, activity and warning expression | How do the matrix and LEDs express semantic EMF, radio activity and warnings? | `full_spooky_proto-54w.8` |
| Utility entry and exit | What does the display show when a utility opens or closes? | `full_spooky_proto-54w.5` |
| Classic view | How does the display show Classic's run state, rate, distance, edge behavior and hold time, and is scan position expressed on the matrix? | `full_spooky_proto-54w.5` |
| Band change during a session | How are band changes and radio faults acknowledged on display, lights and audio while recording? | `full_spooky_proto-54w.1`, [decision 0003](../../decisions/0003-radio-control-during-recording.md) |
| Swallowed presses | Does a button that is pressed while its gesture is swallowed or dismissed still light at 100%? | `full_spooky_proto-54w.18` |
| PTT on display and lights | Do the display and Button 1's light show that PTT is held? | `full_spooky_proto-54w.5`, `full_spooky_proto-54w.18` |
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
| PRES-SES-07 | Target | Session outcome and CLI reply implemented; no nearly-full-card evidence |
| PRES-SES-08 | Target | Session outcome and CLI reply implemented; no file-limit evidence |
| PRES-SES-09 | Proven for the CLI reply | [Stepped preallocation](../../evidence/2026-10-02-stepped-preallocation.md) |
| PRES-SES-10 | Proven for the CLI reply | [Stepped preallocation](../../evidence/2026-10-02-stepped-preallocation.md) |
| PRES-RAD-01 | Proven for the boot log | [Radio regression](../../evidence/2026-09-24-radio-regression.md) |
| PRES-RAD-02 | Target | Published by the Radio machine; no surface renders it |
| PRES-RAD-03 | Target | CLI reply now sent through the Radio machine; the [radio regression](../../evidence/2026-09-24-radio-regression.md) proved the blocking path it replaced and has not been rerun (`full_spooky_proto-54w.28`) |
| PRES-RAD-04 | Target | Same basis as PRES-RAD-03 |
| PRES-RAD-05 | Target | CLI reply implemented without a reason; no bench evidence |
| PRES-RAD-06 | Target | Not implemented |
| PRES-RAD-07 | Target | Same basis as PRES-RAD-03 |
| PRES-RAD-08 | Target | CLI reply and log implemented; no bench evidence |
| PRES-RAD-09 | Target | CLI reply implemented; no bench evidence |
| PRES-RAD-10 | Target | CLI reply implemented; no bench evidence |
| PRES-RAD-11 | Target | CLI reply implemented; no bench evidence |
| PRES-LED-01 to PRES-LED-03 | Target | User direction 2026-09-27, recorded in `full_spooky_proto-54w.18`. Not implemented; the LED pins were chosen for PWM-capable timers. |
| PRES-LED-04 to PRES-LED-06 | Target | [Decision 0005](../../decisions/0005-button-0-session-prompt.md), items 14 to 16. Not implemented. |
| PRES-PRM-01, PRES-PRM-02 | Target | User direction 2026-09-27, `full_spooky_proto-54w.18`. Not implemented. |
| PRES-DEV-01 | Proven for the CLI reply | [Radio regression](../../evidence/2026-09-24-radio-regression.md) |
| PRES-DEV-02 | Target | CLI reply implemented; no bench evidence |
| PRES-DEV-03 | Target | Product intent. Published by the Context machine and host tested; not wired to firmware or any surface. |
| PRES-CTX-01 to PRES-CTX-05 | Target | [Decision 0009](../../decisions/0009-first-slice-field-controls.md) and [decision 0008](../../decisions/0008-recording-safe-command-policy.md). Published by the Context machine and host tested; not wired to firmware or any surface. |
| PRES-CTX-06 | Proven for the audio fade and the CLI reply | [Monitor-only PTT](../../evidence/2026-10-08-monitor-only-ptt.md), through `MONITOR PTT`; [decision 0028](../../decisions/0028-monitor-only-ptt-without-mic-monitoring.md). Button 1 is not wired on `main`. |
| PRES-INP-01 | Target | [Decision 0008](../../decisions/0008-recording-safe-command-policy.md) item 8. Published by the InputResolution machine and host tested; not wired to firmware or any surface. |
| PRES-CLS-01 to PRES-CLS-05 | Proven for the CLI reply and log line | [Classic on the M7](../../evidence/2026-10-02-classic-on-m7.md), without the holding run state. No display surface (`full_spooky_proto-54w.5`). |
| PRES-CLS-01 holding, PRES-CLS-06 | Proven for the CLI reply and log line | [Classic activity hold](../../evidence/2026-10-03-classic-activity-hold.md). When the hold triggers is not settled (spec SC-008 failed; `full_spooky_proto-54w.32`). No display surface (`full_spooky_proto-54w.5`). |
