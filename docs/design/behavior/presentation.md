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
| Transition noise | Should tunes and band transitions play generated noise in the monitored mix, with user-tunable level, color and duration? It would never reach a stored raw track or the activity metrics. | `full_spooky_proto-54w.16`; product open decision 17 |

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
| PRES-DEV-01 | Proven for the CLI reply | [Radio regression](../../evidence/2026-09-24-radio-regression.md) |
| PRES-DEV-02 | Target | CLI reply implemented; no bench evidence |
| PRES-DEV-03 | Target | Product intent; no mode state exists in firmware |
