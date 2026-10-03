# Spooky Box Control Map

Every contextual binding of the physical controls, the navigation structure they serve,
the first Instrument preset model, and the open control decisions. This document is
product intent. Mode purposes and interaction principles are in
[modes and interaction](modes-and-interaction.md); the exact mechanism that resolves
each binding is in the [behavior model](../../docs/design/behavior/README.md).

## Status values

| Status | Meaning |
|---|---|
| Defined | Settled. Implementations follow it. |
| Proposed | A candidate that has not been settled. It must not be treated as a requirement. |
| Open | Undecided. The row records the question, not an answer. |

## Changing this map

This map is protected (constitution, Governance). A change needs an approved proposal,
and every proposal that adds, changes or retires a binding includes an audit that
reports each of these checks:

1. **Uniqueness.** No two Defined or Proposed bindings share mode, workspace, state or
   focus, layer, control and gesture.
2. **Gesture conflicts.** For each control the change touches, list every binding on
   that control in the same context, including press, hold, release, chord and Shift
   layer, and state how they are told apart. A press resolves in the context where it
   began.
3. **Recording safety.** Each new or changed action is classified as allowed, deferred
   or rejected while a session is active, under the recording-safe command policy.
4. **Behavior model.** The matching rows in the behavior model are identified, and
   their updates are part of the same change.
5. **Acknowledgement.** How the action's outcome is shown on display, lights and audio
   is defined, or listed as open in the behavior model's presentation file.
6. **Status and IDs.** A binding becomes Defined only with an accepted decision record
   or a user direction recorded in Beads. Retired IDs move to the Retired IDs table and
   are never reused.
7. **Consistency.** No other product-intent document contradicts the change.


## Bindings

One row per contextual binding. Filter by mode, workspace, layer, control, or status.

| Binding ID | Mode | Workspace | State or focus | Layer | Control | Gesture | Action | Result or destination | Status | Origin | Notes |
|---|---|---|---|---|---|---|---|---|---|---|---|
| C-001 | Global utility | Menus and submenus | Browse | Normal | Encoder 0 | Turn | Scroll menu options or selectable values | Selection moves | Defined | User control notes |  |
| C-002 | Global utility | Menus and submenus | Browse | Normal | Encoder 0 button | Short press | Select highlighted item | Open item or enter value edit | Defined | User control notes |  |
| C-003 | Global utility | Menus and submenus | Any | Normal | Encoder 1 button | Press | Go back one level | Parent menu or previous state | Defined | User control notes |  |
| C-004 | Global utility | Menus and submenus | Any | Normal | Encoder 3 button | Hold | Leave utility workspace | Return to previous operating mode screen | Defined | User control notes |  |
| C-005 | Field | Field engine page | Parameter page | Normal | Encoder 3 button | Short press | Advance parameter page | Next page; wrap after final page | Defined | Modes and interaction document | Page color changes with the active page. |
| C-006 | Field | Field engine page | Any | Normal | Encoder 3 button | Hold | Enter Shift layer while held | Shift commands become active | Defined | Modes and interaction document |  |
| C-008 | Field | Field engine page | Any | Shift | Button 0 | Press | Switch operating mode | Instrument main engine page | Defined | Modes and interaction document | Rejected while a session is active (decision 0008 item 6): the display shows the reason and the session continues. |
| C-009 | Field | Field engine page | Any | Shift | Button 1 | Press | Save synchronized rolling buffers | Standalone Field capture saved | Defined | Modes and interaction document | Allowed during a session (decision 0008 item 7); the save resolves on release. |
| C-010 | Field | Field engine page | Any | Shift | Buttons 0 and 1 | Chord | Save buffers, switch modes, and load capture | Instrument mode with capture as active sample | Defined | Modes and interaction document | Chord resolver must delay component actions until the combination is known (D-006). During a session the chord performs the capture save only; the mode switch is suppressed (decision 0008 item 7). |
| C-014 | Field | Engine menu | Menu open | Normal | Encoder 0 | Turn | Scroll scan-engine choices | Highlighted engine changes | Defined | User control notes |  |
| C-016 | Field | Field engine page | Monitoring | Normal | Button 1 | Momentary press | Mute radio in monitored mix while held | Mic remains audible; both raw tracks continue recording | Defined | Modes and interaction document | PTT mutes from press to release; a momentary press, not a threshold hold (decision 0009 item 37). PTT events are timestamped for playback. |
| C-017 | Field | Field engine page | Monitoring | Normal | Button 1 | Release | Restore radio monitoring | Normal monitored mix resumes | Defined | Modes and interaction document |  |
| C-018 | Instrument | Main engine page | Any | Normal | Encoder 1 button | Hold | Open instrument-engine selector | Instrument-engine popover remains open while held | Defined | User control notes |  |
| C-019 | Instrument | Instrument-engine selector | Popover open | Popover | Encoder 0 | Turn | Scroll instrument-engine choices | Highlighted engine changes | Defined | User control notes |  |
| C-020 | Instrument | Instrument-engine selector | Popover open | Popover | Encoder 1 button | Release | Commit highlighted engine and close popover | Selected engine opens; current engine remains if unchanged | Defined | User control notes |  |
| C-021 | Instrument | Main engine page | Any | Normal | Encoder 2 button | Hold | Open performance-view selector | Performance-view popover remains open while held | Defined | User control notes |  |
| C-022 | Instrument | Performance-view selector | Popover open | Popover | Encoder 0 | Turn | Scroll performance-view choices | Highlighted view changes | Defined | User control notes |  |
| C-023 | Instrument | Performance-view selector | Popover open | Popover | Encoder 2 button | Release | Commit highlighted view and close popover | Selected performance view opens | Defined | User control notes |  |
| C-024 | Instrument | Main engine page | Parameter page | Normal | Encoder 3 button | Short press | Advance parameter page | Next page; wrap after final page | Defined | User control notes | Early pages are engine-specific; later pages may expose macros, modulation, and sequencer controls. |
| C-025 | Instrument | Main engine page | Parameter page | Normal | Encoders 0 to 3 | Turn | Adjust parameter assigned on active page | Parameter value changes | Proposed | Workbook proposal | Exact per-engine assignments remain to be designed. |
| C-026 | Instrument | Main engine page | Any | Normal | Encoder 3 button | Hold | Enter Shift layer while held | Instrument Shift commands become active | Defined | User control notes |  |
| C-027 | Instrument | Main engine page | Any | Shift | Encoder 0 button | Press | Open global utility root | Global utility menu | Defined | User control notes |  |
| C-028 | Instrument | Main engine page | Any | Shift | Button 0 | Press | Switch operating mode | Return to Field mode | Defined | User control notes |  |
| C-029 | Instrument | Main engine page | Performance | Normal | Button 1 | Hold | Capture or inject microphone material | Active engine receives mic material | Proposed | Modes and interaction document | Capture length, quantization, and latch behavior depend on the engine. |
| C-030 | Instrument | Main engine page | Performance | Normal | Button 1 | Release | End momentary microphone capture or injection | Engine-specific capture completes | Proposed | Workbook proposal |  |
| C-032 | Instrument | FX chain | Effect list | Normal | Encoder 0 | Turn | Scroll effect selection | Highlighted effect changes | Defined | User control notes |  |
| C-033 | Instrument | FX chain | Effect list | Normal | Encoder 1 | Turn | Adjust selected effect top-level parameter 1 | Displayed value changes | Defined | User control notes |  |
| C-034 | Instrument | FX chain | Effect list | Normal | Encoder 2 | Turn | Adjust selected effect top-level parameter 2 | Displayed value changes | Defined | User control notes |  |
| C-035 | Instrument | FX chain | Effect list | Normal | Encoder 3 | Turn | Adjust selected effect top-level parameter 3 | Displayed value changes | Defined | User control notes |  |
| C-036 | Instrument | FX chain | Effect list | Normal | Encoder 0 button | Short press | Open selected effect settings | Effect settings page | Defined | User control notes |  |
| C-037 | Instrument | FX chain | Effect list | Normal | Encoder 0 button | Hold | Enter chain-ordering state | Selected effect becomes movable | Defined | User control notes |  |
| C-038 | Instrument | FX chain | Ordering | Edit | Encoder 0 | Turn | Move selected effect in chain | Effect position changes | Defined | User control notes |  |
| C-039 | Instrument | FX chain | Ordering | Edit | Encoder 0 button | Press | Save order | Return to effect list | Defined | User control notes |  |
| C-040 | Instrument | FX chain | Effect list | Normal | Encoder 1 button | Press | Toggle selected effect | Effect becomes active or bypassed | Defined | User control notes |  |
| C-041 | Instrument | FX chain | Effect list | Normal | Encoder 3 button | Short press | Return to performance-view selection | Performance-view selector | Defined | User control notes | Clarify whether this opens the popover or a persistent menu. |
| C-042 | Instrument | FX chain | Any | Normal | Encoder 3 button | Hold | Return to instrument engine | Main engine page | Defined | User control notes |  |
| C-043 | Instrument | Effect settings | Parameter page | Normal | Encoders 0 to 3 | Turn | Adjust assigned effect parameter | Parameter value changes | Proposed | Workbook proposal |  |
| C-044 | Instrument | Effect settings | Parameter page | Normal | Encoder 3 button | Short press | Advance effect parameter page | Next page; wrap after final page | Defined | User control notes |  |
| C-045 | Instrument | Effect settings | Any | Normal | Encoder 3 button | Hold | Return to FX chain | FX effect list with same effect selected | Defined | User control notes |  |
| C-046 | Instrument | Modulation matrix | Source browse | Normal | Encoder 0 | Turn | Scroll modulation-source rows | Selected source changes | Defined | User control notes |  |
| C-047 | Instrument | Modulation matrix | Source browse | Normal | Encoder 0 button | Short press | Select current modulation source | First target cell in row is highlighted | Defined | User control notes |  |
| C-048 | Instrument | Modulation matrix | Target browse | Edit | Encoder 0 | Turn | Scroll target columns | Highlighted routing cell changes | Defined | User control notes |  |
| C-049 | Instrument | Modulation matrix | Target browse | Edit | Encoder 0 button | Press | Select routing cell | Enter modulation-amount edit | Defined | User control notes |  |
| C-050 | Instrument | Modulation matrix | Amount edit | Edit | Encoder 0 | Turn | Adjust modulation amount | Routing amount changes | Defined | User control notes |  |
| C-051 | Instrument | Modulation matrix | Amount edit | Edit | Encoder 0 button | Press | Save modulation amount | Return to target browse | Defined | User control notes |  |
| C-052 | Instrument | Modulation matrix | Target or amount edit | Edit | Encoder 1 button | Press | Go back or cancel current edit | Previous matrix state | Defined | User control notes |  |
| C-053 | Instrument | Modulation matrix | Source browse | Normal | Encoder 0 button | Hold | Open selected source settings | Modulation-source settings page | Defined | User control notes |  |
| C-054 | Instrument | Modulation matrix | Source browse | Normal | Encoder 1 button | Press | Toggle selected source | Source becomes active or inactive | Defined | User control notes |  |
| C-055 | Instrument | Modulation matrix | Any | Normal | Encoder 3 button | Short press | Return to performance-view selection | Performance-view selector | Defined | User control notes |  |
| C-056 | Instrument | Modulation matrix | Any | Normal | Encoder 3 button | Hold | Return to instrument engine | Main engine page | Defined | User control notes |  |
| C-057 | Instrument | Mod-source settings | Parameter page | Normal | Encoders 0 to 3 | Turn | Adjust assigned source parameter | Parameter value changes | Proposed | Workbook proposal |  |
| C-058 | Instrument | Mod-source settings | Parameter page | Normal | Encoder 3 button | Short press | Advance source parameter page | Next page; wrap after final page | Proposed | Workbook proposal | Inferred from the familiar parameter-page scheme. |
| C-059 | Instrument | Mod-source settings | Any | Normal | Encoder 3 button | Hold | Return to modulation matrix | Same source remains selected | Proposed | Workbook proposal | Return mapping was not explicitly specified. |
| C-060 | Instrument | Macro editor | Macro browse | Normal | Encoder 0 | Turn | Scroll macro selection | Highlighted macro changes | Defined | User control notes |  |
| C-061 | Instrument | Macro editor | Macro browse | Normal | Encoder 0 button | Press | Open selected macro | Macro settings page | Defined | User control notes |  |
| C-062 | Instrument | Macro settings | Field browse | Normal | Encoder 0 | Turn | Scroll macro name, mappings, and add row | Highlighted field changes | Defined | User control notes |  |
| C-063 | Instrument | Macro settings | Field browse | Normal | Encoder 0 button | Press | Edit highlighted field | Context-specific editor opens | Defined | User control notes |  |
| C-064 | Instrument | Macro settings | Name edit | Edit | Encoder 0 button | Press | Open standardized text editor | Macro name editor | Defined | User control notes |  |
| C-065 | Instrument | Macro settings | Mapping amount edit | Edit | Encoder 0 | Turn | Adjust mapped amount | Mapping depth changes | Defined | User control notes |  |
| C-066 | Instrument | Macro settings | Mapping amount edit | Edit | Encoder 0 button | Press | Save mapped amount | Return to field browse | Defined | User control notes |  |
| C-067 | Instrument | Macro settings | Add mapping | Edit | Encoder 0 | Turn | Scroll parameter choices | Highlighted parameter changes | Defined | User control notes |  |
| C-068 | Instrument | Macro settings | Add mapping | Edit | Encoder 1 button | Press | Cancel parameter selection | Return to macro settings | Defined | User control notes |  |
| C-069 | Instrument | Macro settings | Add mapping | Edit | Encoder 0 button | Press | Add highlighted parameter | New mapping added at amount 0 | Defined | User control notes |  |
| C-070 | Instrument | Macro settings | Any | Normal | Encoder 3 button | Short press | Return to macro editor | Macro browse with same macro selected | Defined | User control notes |  |
| C-071 | Instrument | Macro settings | Any | Normal | Encoder 3 button | Hold | Return to instrument engine | Main engine page | Defined | User control notes |  |
| C-072 | Instrument | Sequencer | Note-bar browse | Normal | Encoder 0 | Turn | Scroll note bars | Highlighted bar changes | Defined | User control notes |  |
| C-073 | Instrument | Sequencer | Note-bar browse | Normal | Encoder 0 button | Short press | Select highlighted bar | Enter note edit | Defined | User control notes |  |
| C-074 | Instrument | Sequencer | Note edit | Edit | Encoder 0 | Turn | Adjust note value | Note changes within current scale | Defined | User control notes |  |
| C-075 | Instrument | Sequencer | Note edit | Edit | Encoder 1 button | Press | Toggle note activation | Step becomes active or inactive | Defined | User control notes |  |
| C-076 | Instrument | Sequencer | Note edit | Edit | Encoder 0 button | Press | Save note value | Return to note-bar browse | Defined | User control notes |  |
| C-077 | Instrument | Sequencer | Note-bar browse | Normal | Encoder 0 button | Hold | Switch focus to sequencer settings | Left-most setting becomes selected | Defined | User control notes |  |
| C-078 | Instrument | Sequencer | Settings browse | Normal | Encoder 0 | Turn | Scroll settings | Highlighted setting changes | Proposed | Workbook proposal | Navigation was implied but not explicitly stated. |
| C-079 | Instrument | Sequencer | Settings browse | Normal | Encoder 0 button | Short press | Select highlighted setting | Enter setting-value edit | Defined | User control notes |  |
| C-080 | Instrument | Sequencer | Setting-value edit | Edit | Encoder 0 | Turn | Adjust setting value | Tempo, division, scale, or other value changes | Defined | User control notes |  |
| C-081 | Instrument | Sequencer | Setting-value edit | Edit | Encoder 1 button | Press | Cancel value edit | Return to settings browse without saving | Defined | User control notes |  |
| C-082 | Instrument | Sequencer | Setting-value edit | Edit | Encoder 0 button | Press | Save setting value | Return to settings browse | Defined | User control notes |  |
| C-083 | Instrument | Sequencer | Any | Normal | Encoder 3 button | Short press | Return to performance-view selection | Performance-view selector | Defined | User control notes |  |
| C-084 | Instrument | Sequencer | Any | Normal | Encoder 3 button | Hold | Return to instrument engine | Main engine page | Defined | User control notes |  |
| C-085 | Instrument | Preset browser | Preset browse | Normal | Encoder 0 | Turn | Scroll presets for active engine | Highlighted preset changes | Proposed | Workbook proposal |  |
| C-086 | Instrument | Preset browser | Preset browse | Normal | Encoder 0 button | Short press | Load highlighted preset | Engine state changes after compatibility check | Proposed | Workbook proposal |  |
| C-087 | Instrument | Preset browser | Preset browse | Normal | Encoder 0 button | Hold | Open save flow | Save New or Overwrite choice | Proposed | Workbook proposal |  |
| C-088 | Instrument | Preset browser | Any | Normal | Encoder 1 button | Press | Cancel or go back | Previous preset or engine state | Proposed | Workbook proposal |  |
| C-089 | Instrument | Preset browser | Any | Normal | Encoder 3 button | Hold | Return to instrument engine | Main engine page | Proposed | Workbook proposal |  |
| C-090 | Field | Field engine page | Any engine, including Manual | Normal | Button 0 | Hold | Open session prompt | Start prompt when no session is active; stop prompt when one is | Defined | User direction 2026-09-27 | Other controls are swallowed while Button 0 is held before the prompt appears. |
| C-091 | Instrument | Main engine page or performance view | Any | Normal | Button 0 | Hold | Open session prompt | Same as C-090 | Defined | User direction 2026-09-27 | Same as C-090. |
| C-092 | Field and Instrument | Session prompt | Prompt open | Prompt | Encoder 0 button | Press | Confirm the prompt | Session starts or stops | Defined | User direction 2026-09-27 | Feedback follows the session outcome, not the confirmation. |
| C-093 | Field and Instrument | Session prompt | Prompt open | Prompt | Button 0 | Release | Cancel the prompt | No change to the session | Defined | User direction 2026-09-27 | Every other gesture is dismissed while the prompt is open. |
| C-094 | Field | Field engine page | Non-manual engine | Shift | Encoder 1 button | Press | Manual quick-jump | Manual engine at current tune target | Defined | Decision 0009 item 28 | Second press returns; one-entry return slot. |
| C-095 | Field | Manual engine page | Jumped | Shift | Encoder 1 button | Press | Return from quick-jump | Restore previous Field engine and state; clear the return slot | Defined | Decision 0009 item 28 |  |
| C-096 | Field | Field engine page | Menu closed | Normal | Encoder 2 button | Long press | Open engine menu | Engine menu stays open | Defined | Decision 0009 item 16 | Timeout closes the menu. |
| C-097 | Field | Engine menu | Menu open | Normal | Encoder 0 button | Press | Select highlighted engine | Engine switches; menu closes | Defined | Decision 0009 items 18-19 |  |
| C-098 | Field | Engine menu | Menu open | Normal | Encoder 1 button | Press | Close menu without switching | Previous engine page | Defined | Decision 0009 item 18 |  |
| C-099 | Field | Field engine page | Menu closed | Normal | Encoder 1 button | Long press | Open band menu | Band menu stays open | Defined | Decision 0009 item 16 | Classic and Manual pages. Timeout closes the menu. |
| C-100 | Field | Band menu | Menu open | Normal | Encoder 0 | Turn | Choose band | Highlighted band changes | Defined | Decision 0009 item 18 |  |
| C-101 | Field | Band menu | Menu open | Normal | Encoder 0 button | Press | Confirm band | Band changes; consequential transition (decision 0008 item 13) | Defined | Decision 0009 items 18-19 |  |
| C-102 | Field | Band menu | Menu open | Normal | Encoder 1 button | Press | Dismiss band menu | Previous page | Defined | Decision 0009 item 18 |  |
| C-103 | Field | Classic engine page | Any | Normal | Encoder 0 button | Click | Run or pause the scan | Scan state toggles | Defined | Decision 0009 item 9 |  |
| C-104 | Field | Classic engine page | Any | Normal | Encoder 0 | Turn | Set jump rate | Jump rate changes | Defined | Decision 0009 item 8 |  |
| C-105 | Field | Classic engine page | Any | Normal | Encoder 1 button | Click | Toggle scan direction | Direction reverses | Defined | Decision 0009 item 10 |  |
| C-106 | Field | Classic engine page | Any | Normal | Encoder 1 | Turn | Set jump distance | Jump distance changes | Defined | Decision 0009 item 8 |  |
| C-107 | Field | Manual engine page | Any | Normal | Encoder 0 | Turn | Tune | Frequency changes | Defined | Decision 0009 item 13 | Only tuning control for now. |
| C-108 | Field | Manual engine page | Any | Normal | Encoder 0 button | Click | Toggle wrap at band edges | Wrap toggles | Defined | Decision 0009 item 14 |  |
| C-109 | Field | Field engine page | Any | Shift | Encoder 0 button | Press | Open global utility root | Global utility menu | Defined | Decision 0009 item 31 | Fires on press and ends the Shift layer (decision 0009 items 27 and 34). Allowed during a session: opening the root changes nothing; each entry inside it keeps its own class (decision 0008 item 6). |
| C-110 | Field | Classic engine page | Any | Normal | Encoder 2 | Turn | Set edge behavior | Edge behavior steps through wrap, bounce and stop | Defined | Decision 0016 item 11 | Stops at the first and last choice. Wrap is the default. |
| C-111 | Field | Classic engine page | Any | Normal | Encoder 3 | Turn | Set activity hold time | Hold time changes; zero turns the hold off | Defined | Decision 0016 item 16 | The activity threshold is internal. |

## Workspace index

Navigation structure for operating modes, popovers, editors, and global utilities.

| Mode | Workspace | Parent | Entry | Exit | Purpose | Status |
|---|---|---|---|---|---|---|
| Global utility | Global utility root | Operating mode | Shift + Encoder 0 from an operating-mode engine page (decision 0009) | Encoder 3 hold | Access Settings, Playback, Diagnostics, Maintenance, and system information | Defined |
| Global utility | Settings | Global utility root | Select Settings from the global utility root | Encoder 1 back; Encoder 3 hold exits utilities | Persistent configuration | Defined |
| Global utility | Playback | Global utility root | Select Playback | Encoder 1 back; Encoder 3 hold exits utilities | Browse and replay Field captures and Instrument sessions | Defined |
| Global utility | Diagnostics | Global utility root | Select Diagnostics | Encoder 1 back; Encoder 3 hold exits utilities | System health, buses, sensors, storage, and core status | Defined |
| Global utility | Maintenance | Global utility root | Select Maintenance | Encoder 1 back; Encoder 3 hold exits utilities | Calibration, storage maintenance, updates, and service actions | Defined |
| Global utility | Text editor | Invoking editor | Macro name or another editable text field | Save or cancel to caller | Product-standardized text entry | Proposed |
| Field | Field engine page | Field Mode | Startup, mode switch, or engine selection | Mode switch or utility entry | Classic, Seek, Orbit, Manual, and future scan-engine performance | Defined |
| Field | Engine menu | Field engine page | Long press Encoder 2 button | Encoder 1 close, engine selection, or timeout | Stay-open engine selection menu | Defined |
| Field | Band menu | Field engine page | Long press Encoder 1 button | Encoder 1 dismiss, band confirm, or timeout | Stay-open band selection menu | Defined |
| Instrument | Main engine page | Instrument Mode | Mode switch, preset load, or engine selection | Mode switch, utility entry, or performance view | Active sound engine and parameter pages | Defined |
| Instrument | Instrument-engine selector | Main engine page | Hold Encoder 1 button | Release Encoder 1 button | Momentary instrument-engine selection | Defined |
| Instrument | Performance-view selector | Main engine page or performance view | Hold Encoder 2 from main page; short Encoder 3 from several views | Select view or return to engine | Choose FX, Modulation, Macros, Sequencer, Presets, and future views | Open |
| Instrument | FX chain | Performance views | Select FX | Encoder 3 hold | Order effects, toggle bypass, and adjust top-level values | Defined |
| Instrument | Effect settings | FX chain | Short press Encoder 0 button on an effect | Encoder 3 hold | Effect-specific visualizer and parameter pages | Defined |
| Instrument | Modulation matrix | Performance views | Select Modulation | Encoder 3 hold | Route modulation sources to targets and edit amounts | Defined |
| Instrument | Mod-source settings | Modulation matrix | Hold Encoder 0 button on a source | Proposed Encoder 3 hold | Granular source-specific settings and visuals | Proposed |
| Instrument | Macro editor | Performance views | Select Macros | Return mapping not fully defined | Choose among four named macros | Defined |
| Instrument | Macro settings | Macro editor | Press Encoder 0 button on a macro | Encoder 3 short or hold | Rename macro and manage parameter mappings | Defined |
| Instrument | Sequencer | Performance views | Select Sequencer | Encoder 3 short or hold | Edit note bars and sequence settings | Defined |
| Instrument | Preset browser | Performance views or parameter page | Access mapping not yet selected | Encoder 3 hold | Save and load presets for the active instrument engine | Proposed |
| Field and Instrument | Session prompt | Field engine page, Instrument main engine page or performance view | Hold Button 0 | Release Button 0, or confirm with Encoder 0 button | Confirm starting or stopping a session | Defined |

## Instrument preset model

Recommended first scope for save and load behavior. Open items remain deliberately unresolved.

### Preset scope

| State category | Treatment | Rationale |
|---|---|---|
| Engine identity and schema version | Include | Required to reject or migrate incompatible presets. |
| Engine-specific parameter values | Include | Core purpose of an instrument-engine preset. |
| Macro names, mappings, depths, and polarities | Include | Keeps the engine's performance surface intact. |
| Modulation routes and source settings | Include | Preserves the engine's expressive behavior. |
| Sequencer pattern and engine-specific settings | Include | Useful when the sequence is part of the sound design. |
| Slice markers and sample trim metadata | Include | Small metadata that defines how the source is played. |
| Active sample audio | Reference | Store a stable file reference or copy policy; do not silently embed large audio in every preset. |
| Effects chain | Open | Could belong to the engine preset, a separate FX preset, or a later full-scene format. |
| External MIDI configuration | Optional | Include only settings that are meaningfully tied to this engine. |
| Current UI page, highlight, or cursor | Exclude | Restore a predictable default performance page instead. |
| Transient envelopes, held notes, and live grain state | Exclude | Runtime state should not leak into ordinary preset recall. |
| Recording or rolling-buffer contents | Exclude | Presets may reference saved captures but should not contain active recording state. |

### Preset operations

| Operation | Behavior | Status | Candidate control |
|---|---|---|---|
| Load | Recall selected preset after compatibility validation | Proposed | Encoder 0 short press in Preset browser |
| Save New | Capture current engine state under a new name | Proposed | Encoder 0 hold, then select Save New |
| Overwrite | Replace an existing preset after explicit confirmation | Proposed | Encoder 0 hold, then select Overwrite |
| Rename | Open the standardized text editor | Open | No control binding chosen |
| Delete | Remove a user preset after confirmation | Open | No control binding chosen |
| Factory reset | Restore built-in presets without deleting user captures | Open | Maintenance utility rather than ordinary preset browser |

## Open control decisions

Questions that affect consistency, safety, or the ability to learn the physical interface.

| Decision ID | Area | Question | Why it matters | Suggested next test | Priority |
|---|---|---|---|---|---|
| D-001 | Shift layer | Should Field Shift open Settings directly or the global utility root? | Current Field and Instrument Shift mappings differ. | Resolved by decision 0009 (`full_spooky_proto-54w.23`): Shift plus Encoder 0 opens the global utility root; C-007 retired. | High |
| D-002 | Encoder 3 hold | What is the context-priority rule for Shift, exit, and back actions? | The same hold enters Shift on engine pages but exits utilities and returns from editors. | Resolved by decision 0009 items 3 and 23 to 25 (`full_spooky_proto-54w.23`). | High |
| D-003 | Selectors | Is the performance-view destination a momentary popover or a persistent top-level menu? | Several pages say Encoder 3 short returns to a top-level performance menu. | Prototype both with FX and Sequencer navigation. | High |
| D-004 | Instrument pages | What belongs on main-engine parameter pages versus full Macro, Modulation, and Sequencer editors? | Duplicated access can feel powerful or confusing depending on scope. | Limit engine pages to live performance controls; reserve structural editing for full views. | High |
| D-005 | Popovers | Does release always commit the highlighted engine or view, and how does the user cancel? | A slip during a hold could switch engines unintentionally. | Resolved for Field by decision 0009 (`full_spooky_proto-54w.23`): menus open on a long press and stay open; selection and dismissal are explicit. Instrument remains open. | High |
| D-006 | Chord timing | What hold and chord-resolution timing feels reliable? | Shift plus buttons 0 and 1 must not fire Button 0 early. | Resolved in principle by decision 0009 (`full_spooky_proto-54w.23`): release-resolved actions with push-turn cancellation; thresholds stay configurable until bench trials. | High |
| D-007 | Presets | Where is the Preset browser entered from? | Save and load are required, but no access gesture has been selected. | Compare a performance-view entry with a dedicated parameter page. | High |
| D-008 | Presets | Does an engine preset include the FX chain? | Including FX gives complete sounds; excluding it supports reusable global chains. | Start with engine state plus macros/modulation/sequencer; test separate FX presets. | Medium |
| D-009 | Presets | How are sample files referenced, copied, moved, or missing? | A preset can become unusable if its source audio changes location. | Define stable asset IDs and a missing-source repair flow. | High |
| D-010 | Macro editor | How are mappings deleted or reordered? | The current design only adds mappings and changes amount. | Try hold-to-open an item menu rather than overloading ordinary presses. | Medium |
| D-011 | Mod source settings | Should Encoder 3 short cycle pages and hold return to the matrix? | This was inferred from the shared parameter-page pattern. | Validate against Effect settings on the prototype. | Medium |
| D-012 | Sequencer | How does focus return from sequencer settings to note bars? | Only entry into settings is currently defined. | Test Encoder 1 back versus Encoder 0 hold as the inverse action. | Medium |
| D-014 | FX chain | Are three inline effect parameters always useful, and how are they assigned? | Effects have different parameter priorities. | Let each effect define up to three quick parameters with stable defaults. | Medium |
| D-015 | Recording safety | Which navigation and preset actions are restricted while recording? | Loading presets or changing engines may interrupt or invalidate a capture. | Create an explicit safe, deferred, or blocked policy for each action. | High |
| D-016 | Page colors | Which colors are reserved for parameter pages, warnings, and mode identity? | Using the same colors for unrelated meanings weakens the learned cue. | Build a small semantic color vocabulary before asset production. | Medium |

## Retired IDs

| ID | Former meaning | Retired by |
|---|---|---|
| C-031 | Instrument main engine page, Button 0 press: change the injected-sample mix policy (Open) | User direction 2026-09-27, `full_spooky_proto-54w.18` |
| D-013 | How Button 0 chooses layered, ducked or exclusive microphone injection | User direction 2026-09-27, `full_spooky_proto-54w.18` |
| C-007 | Field Field engine page, Shift + Encoder 1 press: open Settings (Proposed) | Decision 0009, `full_spooky_proto-54w.23` |
| C-011 | Field Field engine page, Encoder 0 hold: quick-jump to Manual (Defined) | Decision 0009, `full_spooky_proto-54w.23` |
| C-012 | Field Field engine page, Encoder 0 hold: return from Manual (Defined) | Decision 0009, `full_spooky_proto-54w.23` |
| C-013 | Field Field engine page, Encoder 2 hold: open scan-engine selector (Defined) | Decision 0009, `full_spooky_proto-54w.23` |
| C-015 | Field Scan-engine selector, Encoder 2 release: commit highlighted engine (Defined) | Decision 0009, `full_spooky_proto-54w.23` |
