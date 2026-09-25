# Spooky Box Modes and Interaction

## Document Status

This document is an initial product and interaction specification. It defines the intended mode hierarchy, the purpose of each mode, the relationships among views, and the interaction principles that should guide implementation. It does not yet prescribe every control mapping, gesture, screen layout, or DSP parameter.

Where a behavior is still under discussion, the document labels it as a proposed direction or an open decision rather than treating it as settled.

## Purpose

Spooky Box has two top-level modes with different creative intentions:

- **Field Mode** explores, interprets, and records external signals.
- **Instrument Mode** turns captured material, sensors, and internal sound engines into something the user deliberately plays.

These modes should feel related, but not interchangeable. Field Mode is oriented toward discovery and observation. Instrument Mode is oriented toward performance and transformation. Moving between them should feel like changing the role of the device, not simply changing pages in a menu.

The central interaction goal is to make Spooky Box feel like an instrument with a coherent internal geography. The user should be able to build intuition about where they are, what the controls will do, and how to return to the previous activity without relying on a permanently visible navigation bar.

## Mode Hierarchy

```text
Spooky Box
├── Operating modes
│   ├── Field Mode
│   │   ├── Classic
│   │   ├── Seek
│   │   ├── Orbit
│   │   ├── Manual
│   │   └── Future scan engines
│   └── Instrument Mode
│       ├── Instrument engines
│       │   ├── Granular
│       │   ├── Sampler and Slice concepts
│       │   └── Future engines
│       └── Performance views
│           ├── Effects
│           ├── Modulation
│           ├── Macros
│           └── Sequencer
└── Global utility spaces
    ├── Settings
    ├── Playback and session browser
    └── Diagnostics and maintenance
```

The hierarchy intentionally distinguishes an **engine** from a **view**.

An engine determines how radio space is traversed or how sound is generated. A view exposes a particular aspect of the currently active engine or device state. For example, Effects is not a peer to the Granular engine: the user remains in Granular while opening the Effects view to shape its output.

Settings and Playback are substantial enough to occupy the display and temporarily take over the controls, but they are not peers to Field and Instrument. They are **global utility spaces** entered from either operating mode. Exiting a utility returns the user to the operating mode, engine, view, and parameter page that were active before it opened.

The interface may temporarily present mode or view choices as a row of names, but the product should not assume that a tab bar remains visible during normal operation. The primary screen and LED matrix should be free to express the active engine rather than continually surrendering space to navigation chrome.

## Global Interaction Principles

### Direct Performance Before Menu Navigation

The most common actions should be available from the primary performance surface. Menus are appropriate for infrequent configuration, but tuning, scan behavior, sound shaping, capture, recording, and performance controls should remain immediate.

### Stable Meanings With Mode Specific Expression

Some meanings should remain stable throughout the product even when their visual treatment changes:

- EMF level represents magnetic-field disturbance or intentional magnetic control.
- Radio activity represents detected energy or change in the received audio.
- Scan position represents the current location within a band or scan territory.
- Recording state represents whether the device is idle, armed, recording, or reviewing.
- The active engine determines the rules of motion or sound generation.

Each mode may render these meanings differently, but should not silently redefine them.

### Temporary Navigation Over Persistent Tabs

Normal operation should show the active engine or performance view without a permanent tab strip. A navigation action may reveal a temporary selector, mode ribbon, or other overlay. The overlay should disappear after the user selects a destination or after a short period of inactivity.

The exact engine- and view-selection gesture is not yet fixed. It may use a press-and-turn gesture, a reserved button combination, or one of the Shift-layer chords described below. Whatever gesture is chosen should be consistent within each operating mode and should never conflict with an action that can destroy or overwrite recorded material.

### Parameter Pages And Encoder Color

An active engine or performance view may divide its parameters into several pages. The proposed Field Mode mapping designates the encoder 3 button as the **Menu and Shift** control.

A short encoder 3 press advances to the next parameter page. Pressing it on the final page wraps back to the first. Each page should have a stable color identity, shown through the illuminated encoders and, where useful, echoed by the OLED or button lights. The color is a learned orientation cue rather than decoration: it should help the user recognize the active control layer without reading every label.

Page assignments should group related parameters and preserve encoder meaning where practical. Frequently performed actions should remain on the primary page. A view should use as few pages as its useful parameter set allows.

### Shift Layer

Holding the encoder 3 button enters a temporary **Shift layer** for as long as it remains held. Shifted button presses invoke global commands rather than the buttons' normal Field or Instrument functions. This is preferable to describing the interaction as another mode because it does not replace or reset the active engine.

The proposed initial Shift-layer chords are:

- **Shift plus encoder 1 button:** open Settings.
- **Shift plus button 0:** switch from Field to Instrument Mode.
- **Shift plus button 1:** save the current rolling buffers to disk.
- **Shift plus buttons 0 and 1:** save the rolling buffers, enter Instrument Mode, and load the saved capture as the active sample.

The input handler must distinguish a chord from its component buttons before executing an action. A short chord-resolution window or execution-on-release policy should prevent Shift plus button 0 from switching modes before the user has time to add button 1. Releasing encoder 3 after using a shifted command must not also advance the parameter page.

Equivalent or complementary shifted commands may be defined in Instrument Mode after its performance controls are better understood. The Shift layer should remain small enough to memorize.

### Fast Return To Performance

Configuration views should be shallow. A user who opens Effects, Modulation, Macros, or Sequencer should be able to return to the active performance view with one predictable action.

### Remember Context

Spooky Box should remember useful local context when the user moves between modes or views. Returning to an engine should normally restore its frequency, scan state, parameters, and current performance view rather than resetting it.

Temporary shortcuts may use a small return stack. The most important initial use is Manual Mode: the user can jump into Manual from another Field engine and then return directly to that engine in its prior state.

### Make Consequential Transitions Legible

Top-level mode changes, band changes, recording transitions, calibration, capture, and destructive actions should have clear visual and, where appropriate, sonic feedback. The OLED and LED matrix may express these events differently, but they should agree about what happened.

## Startup Behavior

Spooky Box should start in **Field Mode** with **Classic** as the default scan engine.

The startup sequence should establish system health, initialize or restore the field-sensor baseline, and then place the user on Classic's primary performance view. The user should be able to begin listening and interacting without passing through a home screen.

The system may later offer an option to restore the last top-level mode at startup. The default behavior should remain Field and Classic until there is strong evidence that automatic restoration is more useful and does not create confusing sensor, audio, or recording states.

## Field Mode

### Purpose

Field Mode treats Spooky Box as an exploratory radio, environmental instrument, and capture device. Its engines provide different ways to move through radio territory and respond to detected activity. Only one Field scan engine is active at a time.

Field Mode should preserve the distinction between:

- **Engine**, which determines how movement occurs.
- **Territory**, which determines where movement may occur.
- **Activity model**, which describes what the device has detected.
- **Presentation**, which determines how the OLED and matrix express that state.

A territory may be one band, part of a band, or a curated set of ranges across multiple bands. Engines should operate on structured tune targets rather than assuming that all frequencies form one continuous axis.

### Shared Field Behavior

All Field engines should expose a recognizable set of concepts even when the controls and visuals emphasize them differently:

- current band or territory
- current frequency or tune target
- running, paused, or transitional engine state
- speed or motion intensity where applicable
- jump distance or spatial scale where applicable
- received-signal and audio-activity measurements
- EMF level relative to the active baseline
- recording state

Adaptive EMF baseline tracking should normally be enabled in Field Mode. The device is trying to notice disturbances relative to its environment, so the baseline may drift slowly when the field is quiet. Manual recalibration remains available.

### Rolling Capture Buffers

Field Mode should continuously maintain a synchronized rolling capture set even when no formal session is active. This lets the user preserve an event after realizing that it was interesting rather than requiring recording to have been started in advance.

The rolling capture set should include:

- radio audio
- microphone audio
- timestamped EMF samples or derived EMF activity
- radio-activity metrics needed for later visualization
- tune targets, band transitions, PTT state, and other significant semantic events
- enough timing information to align every stream to a common monotonic clock

The EMF and event buffers are timestamped data rings rather than audio buffers, but they should cover the same time window. The intended rolling duration remains an implementation decision based on available RAM, external memory, storage bandwidth, and audio format.

Saving the buffer should take a coherent snapshot of all streams and continue capture into a new buffer without creating an audible gap. Disk writes should run as a background storage operation and must not block audio capture or an active recording session.

Shift plus button 1 saves the current rolling capture as a standalone Field capture. Shift plus buttons 0 and 1 performs the same save, enters Instrument Mode, and loads the captured audio as the active sample. The UI should confirm that the snapshot has been secured even if final disk writing continues briefly in the background.

### Classic

Classic is the default Field engine and the clearest expression of procedural scanning. It moves through the active territory linearly using adjustable motion parameters.

Its principal parameters are expected to include:

- scan direction
- jump rate or dwell time
- jump distance or tuning step
- band or territory

Classic should feel rhythmic, mechanical, and legible. The user should be able to predict its near-term motion and understand how each control changes the scan.

Classic's OLED view should make frequency, direction, and motion parameters easy to read. Its matrix identity may use a clean sweep, cursor, trail, or pulse. EMF should affect the atmosphere or intensity of the scene, while radio activity should introduce transient texture without obscuring scan position.

### Seek

Seek combines linear exploration with a short-term memory of radio activity. As it scans, it builds or updates an activity map over the current territory. It may periodically leave the linear path and revisit a location whose activity score exceeds a user-controlled threshold.

The activity model should support decay so that old detections gradually lose influence. A useful initial behavior is:

1. Scan linearly and update the activity map.
2. Identify previously observed locations above the current threshold.
3. At controlled intervals, choose a qualifying location using weighted randomness.
4. Jump to that location, listen for a dwell period, and update its score.
5. Resume exploration without losing the broader scan history.

Potential parameters include:

- activity threshold
- memory decay
- revisit probability or frequency
- revisit dwell time
- jump rate and distance during linear exploration

The activity threshold should have an audible and behavioral consequence. A low threshold makes Seek more exploratory and willing to revisit weak events. A high threshold restricts revisits to the strongest or most persistent activity.

Seek's OLED view should reveal enough of the accumulated activity landscape to make its jumps intelligible without turning the display into a laboratory spectrum analyzer. The matrix may express remembered locations, recent hits, and the contrast between exploration and recall.

The exact activity metric remains an implementation decision. It may combine received signal strength, signal-to-noise ratio, RMS level, spectral flux, transient detection, squelch state, and other radio or audio features.

### Orbit

Orbit is a generative scan engine in which frequency movement is governed by a simplified physical simulation rather than a linear schedule or explicit list of jumps.

The current concept treats the active tuning point as a moving body with position, velocity, inertia, and damping. Historically or currently active frequency regions may become attractors. Their strength can be influenced by activity scores, while randomness or escape energy prevents the system from settling permanently into one location.

Potential parameters include:

- number of active attractors
- attraction strength
- damping or inertia
- randomness or perturbation
- escape energy
- attractor lifetime or decay
- boundary behavior within a territory

Orbit should not attempt to simulate physics for its own sake. The simulation is valuable only if it creates an understandable family of musical scanning behaviors: circling, slingshotting, settling, destabilizing, and escaping.

The OLED should make the relationship between the tuning body and its attractors legible at a glance. The LED matrix can carry more of the spatial and kinetic identity through particles, orbital traces, centers of gravity, or converging motion.

Orbit remains the least defined initial Field engine. Its first prototype should focus on finding one compelling physical model and a small set of controls rather than exposing every simulation variable.

### Manual

The proposed direction is to treat Manual as a first-class Field engine rather than as the paused state of every other engine.

This gives Manual its own interaction model and visual identity. It also prevents Classic, Seek, and Orbit from having to support two competing control schemes within their primary views.

Manual should feel tactile and weighted even though the radio uses discrete digital tuning. Possible techniques include:

- velocity-sensitive encoder response
- tuning inertia or smoothing
- selectable tuning step and acceleration
- subtle station capture or frequency friction
- stronger resistance or visual drag near active signals
- controlled drift or instability in presentation

Manual should also have a reserved quick-jump gesture. Invoking it from Classic, Seek, or Orbit enters Manual at the current tune target. Invoking the same gesture again returns to the previous engine and restores its prior state. The exact button or combination remains open until the physical control map is reviewed as a whole.

Making Manual a separate engine is a provisional product decision. It should be revisited after hands-on testing compares the quick-jump model with pausing and directly steering each scan engine.

### Future Field Engines

The Field hierarchy should allow additional engines without changing the meaning of Field Mode itself. Candidate ideas may include random or chaotic scanning, interband traversal, curated territories, or engines driven more directly by environmental activity.

New engines should be added only when they provide a genuinely different model of movement or attention. A parameter preset or minor scan variation does not need to become a new engine.

## Instrument Mode

### Purpose

Instrument Mode treats Spooky Box as a sound-generating and sound-transforming performance instrument. Radio, microphone, stored recordings, session fragments, and sensors may become source material or control signals rather than only things to observe.

The active instrument engine produces or organizes sound. Performance views expose the systems around that engine: Effects, Modulation, Macros, and Sequencer. Opening one of these views does not deactivate the engine.

Adaptive EMF baseline tracking should normally be disabled in Instrument Mode. This prevents the device from learning away a magnetic field that the user is deliberately manipulating. Instrument Mode may capture a fixed baseline when entered, reuse a saved calibration, or allow explicit recalibration.

### Instrument Engines

#### Granular

Granular is the leading candidate for the first Instrument engine. It should be able to turn radio, microphone, live buffers, and captured session material into pads, textures, leads, rhythmic clouds, and unstable transitions.

Likely parameter families include:

- source and capture window
- grain position
- grain size
- density
- pitch and pitch spread
- time or position spread
- envelope shape
- stereo width or spatial spread
- probability and randomization
- freeze, latch, and retrigger behavior

The initial design should present a small set of musically useful controls and strong defaults. Full access to every DSP parameter should not be required for basic performance.

#### Sampler and Slice

Sampler and Slice are currently product concepts rather than a settled hierarchy. They could become separate engines, or they could become source and playback modes within a broader Granular or sample-based instrument.

A useful first implementation direction is to share one capture and buffer system while offering distinct performance behaviors:

- **Sampler behavior** plays captured material continuously or as triggered one-shots and loops.
- **Slice behavior** divides captured material into addressable regions for rhythmic or gestural rearrangement.
- **Granular behavior** reads the same material through overlapping grains and stochastic position control.

This shared foundation would allow complex pads, leads, rhythmic loops, and sliced phrases without duplicating capture, storage, and waveform management. Hands-on prototyping should determine whether these behaviors feel like separate engines or modes within one instrument.

### Performance Views

#### Effects

Effects presents the active signal chain around the current instrument engine. The first implementation should favor a small, characterful, performance-oriented chain over a general-purpose plugin rack.

The architecture should support clear ordering and bypass state. Candidate effects may include filtering, delay, reverb, saturation, degradation, pitch processing, or spectral treatments, but the initial set remains to be chosen.

#### Modulation

Modulation exposes a constrained matrix of sources and destinations. Likely sources include encoders, macros, sequencer lanes, envelopes, LFOs, radio activity, EMF level, and other sensor values. Destinations should be selected deliberately so that routes remain understandable and safe for the real-time audio system.

The first version should not attempt to make every value routable to every parameter. A modest, legible matrix with useful defaults will better support performance and implementation.

#### Macros

Macros provide high-level controls that move several parameters together. They should allow the user to shape meaningful gestures such as density, instability, space, brightness, or rhythmic intensity without editing several routes during performance.

The initial system should use a small fixed number of macros. Each macro may map to multiple engine, effect, or modulation parameters with configurable depth and polarity.

#### Sequencer

The Sequencer is a compact performance layer, not a miniature workstation. It may sequence slices, triggers, macro values, parameter locks, or other engine-specific events.

Its first version should focus on one or two workflows that clearly benefit the initial instrument engine. The interaction model should avoid deep editing pages and preserve immediate access to start, stop, pattern selection, and a small number of musically useful variations.

## Field To Instrument Workflow

The relationship between Field and Instrument Modes is a defining part of Spooky Box. Material discovered in Field Mode should eventually be easy to carry into Instrument Mode.

A target workflow is:

1. Discover an interesting radio, microphone, or EMF event in Field Mode.
2. Hold Shift and press buttons 0 and 1.
3. Commit the synchronized rolling buffers to a Field capture on disk.
4. Enter Instrument Mode with that capture loaded as the active sample.
5. Transform it through granular, sampler, or slice behavior.
6. Optionally return to Field Mode without losing the Instrument state.

This workflow should not require file browsing during performance. A later editor may let the user trim the captured window, but the immediate action should favor preserving the event over stopping to define exact boundaries.

## Recording Across Modes

Recording is a global capability, but its meaning may vary by context.

Field and Instrument sessions should be stored in separate directory trees and use distinct naming schemes. Each session should live in its own folder with a manifest that identifies the session type, start time, duration, firmware and format versions, stream files, and any incomplete or recovered state. Exact names remain to be finalized, but the intended shape is:

```text
/FIELD/<timestamp>_<field-session-id>/
  manifest
  radio audio
  microphone audio
  EMF stream
  activity and event stream

/INSTRUMENT/<timestamp>_<instrument-session-id>/
  manifest
  performance mix
  microphone or injected-sample source
  EMF stream
  MIDI or sequencer events
  activity and event stream
```

### Field Sessions

A Field session should record synchronized radio and microphone audio as separate tracks, along with EMF readings, tuning history, scan-engine state, PTT state, radio-activity metrics, and other significant metadata.

Button 1 acts as the default Field **PTT** performance control. While held, it silences radio audio in live monitoring but continues recording both the unmuted radio track and the microphone track. PTT press and release events are timestamped in the session. This preserves the raw evidence while allowing the user to speak or foreground the environment without radio audio in the monitored mix.

The user-facing name may remain PTT even though its default signal behavior is more precisely push-to-mute-radio. Its meaning should be stated clearly in the interface and documentation.

### Instrument Sessions

An Instrument session should initially record the performed stereo mix, an EMF stream, and the event data required to understand the performance. Where available, it should also preserve microphone injection as a separate source and record sequencer or external MIDI events in a standard or well-documented form.

Button 1 may provide a parallel performance gesture in Instrument Mode: while held, the user can capture or inject microphone material into the active sampler, slicer, or granular buffer. The exact capture length, quantization, and latch behavior depend on the active engine.

Button 0 may select how injected material interacts with the existing performance when it is triggered. Candidate behaviors include layering it over the mix, ducking the existing engine, or making the injected sample temporarily exclusive. This should be non-destructive: the underlying performance mix and microphone source should remain preserved so the policy can be changed during playback or later processing.

The first implementation should prioritize reliable audio capture over exhaustive reconstruction. The user should receive consistent recording-state feedback regardless of the active mode or view.

Entering a navigation or editor view should never silently stop a recording. Actions that would interrupt, replace, or invalidate an active recording must require explicit confirmation or be unavailable until recording stops.

## Playback And Session Browser

Playback should be a global utility space available from both operating modes. It should combine a session browser, transport controls, and mode-appropriate playback options. Entering Playback temporarily takes over the interface; leaving it returns to the previous operating context.

The browser should distinguish Field sessions, standalone rolling-buffer captures, and Instrument sessions. It should initially prioritize browsing by session type and capture time rather than requiring a complex file manager.

### Field Playback

Because radio and microphone audio are stored as separate synchronized tracks, Field playback can provide independent radio and microphone gain, mute, and solo controls. The initial mix presets should include:

- **PTT aware:** reproduce the monitored experience by muting radio audio during recorded PTT intervals.
- **Full mix:** play both tracks continuously and ignore PTT muting.
- **Radio solo:** play only the radio track.
- **Microphone solo:** play only the microphone track.

Independent gain controls may be applied on top of these presets. Adjusting the mix should never modify the stored source tracks.

Playback should reconstruct an LED-matrix interpretation from the recorded EMF, radio-activity, scan, PTT, and other semantic streams. Reconstructing from raw EMF and audio alone is possible but may change when analysis algorithms evolve. For consistent playback, the manifest should record the firmware or renderer version and the session should preserve derived activity metrics and significant semantic events. Exact historical pixel frames do not need to be stored unless later testing shows a strong reason to do so.

### Instrument Playback

The first Instrument playback implementation may use the same synchronized stem mixer and EMF visualization foundation as Field playback. It should play the recorded performance mix and allow any preserved microphone or source stem to be monitored independently.

Later versions may expose sequencer or MIDI events, send MIDI to an external host, remap an old performance through the current instrument engine, or support richer non-destructive remixing. These features should not delay reliable basic playback.

## Settings And Main Menu

Settings should be a global utility space rather than a third operating mode. Field and Instrument describe what the device is doing creatively; Settings changes persistent configuration and system behavior.

Shift plus encoder 1 opens Settings directly. The first Settings screen may serve as the main utility hub, with access to device settings, audio and display preferences, storage status, session playback, calibration, diagnostics, and system information. If this hub becomes too broad, Playback can receive its own direct Shift-layer shortcut later without changing the top-level mode model.

Settings and Playback should suspend or safely constrain controls that would conflict with their active task, while preserving the previous Field or Instrument context for return. Background recording or buffer capture must not be stopped merely because a utility screen is open unless the requested utility operation genuinely requires it and the user confirms the interruption.

## Display and LED Matrix Roles

The OLED and LED matrix should cooperate rather than mirror one another.

- The **OLED** provides precise state, labels, values, editing context, and the information needed to understand or change behavior.
- The **LED matrix** provides peripheral, glanceable, and expressive feedback about activity, motion, intensity, and mode identity.

Each engine should have a distinct visual identity, but the underlying semantic mapping should remain stable. In Field Mode, EMF generally shapes the visual weather while radio activity adds texture and transient motion. In Instrument Mode, the matrix may show modulation, play position, grain activity, sequencing, or other performance state more directly.

Visual work must remain subordinate to audio and recording reliability. The UI may reduce frame rate, simplify animation, or defer nonessential updates under load without changing authoritative device behavior.

## State Ownership and Software Boundary

These requirements hold whichever processor core, chip or driver implements them:

- One authoritative owner holds the operating mode, engine and view state, utility-space state, recording state, rolling capture, storage transactions, audio behavior, gesture resolution, and the mapping from gestures to product actions.
- Physical inputs are reported without behavioral meaning. The authority decides whether an encoder movement tunes, changes grain size or navigates a menu, using the context in which each press began.
- Presentation surfaces render published semantic state and one-shot events. They never keep a competing copy of mode state, so the displays cannot silently disagree about the active operating mode, engine, view, parameter page, Shift state or utility space.
- A lost input event, a restarted presentation component or a queue overflow must not leave a held control such as Shift or PTT latched.

Which core owns each responsibility and peripheral is an engineering decision. The current assignment is in the [architecture](../../docs/design/architecture.md#application-model) and [decision 0001](../../docs/decisions/0001-initial-ui-and-bus-ownership.md).

## Initial Implementation Scope

The first implementation should be intentionally narrower than the full vision. The lists below describe what each area must include before it is coherent from the user's point of view. They do not set the build order: the [development roadmap](roadmap.md) decides which milestone delivers each item and in what sequence. For example, the first Field slice ships without rolling capture, which arrives with recoverable sessions, so the Field scope below is complete only after both milestones.

### First Field Scope

- boot directly into Field and Classic
- implement parameter pages, page colors, and the encoder 3 Shift layer
- establish the temporary engine-selection interaction
- implement Classic's primary performance view and controls
- implement Manual as a quick-jump engine with return behavior
- maintain synchronized rolling radio, microphone, EMF, activity, and event buffers
- save the rolling buffer through Shift plus button 1 without interrupting capture
- preserve engine state across navigation
- present recording, band, frequency, EMF, and radio activity consistently
- leave Seek and Orbit selectable only when their minimum behavior and visuals are coherent

### First Instrument Scope

- establish the top-level Field and Instrument transition
- implement one Granular engine with a limited source and parameter set
- support Shift plus buttons 0 and 1 as a low-friction save, switch, and load path from Field material
- implement a minimal Effects view
- define the data model for Modulation, Macros, and Sequencer before building their full editors
- preserve the active engine while moving among performance views

### First Recording And Playback Scope

- use separate Field and Instrument session folders and manifests
- record Field radio and microphone tracks separately and in sync
- timestamp PTT, tune, scan, activity, and EMF events
- provide PTT-aware, full-mix, radio-solo, and microphone-solo Field playback
- provide independent non-destructive radio and microphone gain
- reconstruct the LED matrix from recorded semantic streams
- provide basic Instrument performance-mix playback before advanced MIDI or remix features

## Open Decisions

The following questions should remain visible as prototypes are tested:

1. Should Shift plus button 0 also return from Instrument to Field, and what state should each mode restore?
2. What gesture reveals the temporary engine or view selector?
3. Does Manual remain a separate Field engine after hands-on comparison with pausing and steering other engines?
4. What state should resume when returning from Manual: elapsed simulation state, frozen state, or a safe re-entry state?
5. Which radio and audio features contribute to Seek's activity score, and how should that score decay?
6. What is the simplest Orbit model that produces compelling, intelligible motion?
7. Are Granular, Sampler, and Slice separate engines or performance behaviors over a shared buffer system?
8. How long should the rolling capture window be, and where should its audio buffers live?
9. Which effects define the initial Instrument identity?
10. How many modulation routes, macros, and sequencer lanes can the interface support without becoming menu-driven?
11. Which settings persist across power cycles, and which should reset to safe defaults?
12. How should the device behave if a top-level mode change is requested during recording, capture, or another time-critical operation?
13. Should Playback remain inside the Settings hub, receive a direct Shift shortcut, or both?
14. What file formats should store EMF, semantic events, and sequencer or MIDI data?
15. How should button 0 select layered, ducked, or exclusive microphone injection in Instrument Mode?
16. What Shift-layer chord timing feels reliable without making the controls feel delayed?

## Design Guardrails

Future interaction decisions should preserve the following constraints:

- Field and Instrument remain the two primary intentions of the device.
- Field engines are different models of radio navigation, not merely presets.
- Instrument engines generate or organize sound; Effects, Modulation, Macros, and Sequencer operate around the active engine.
- Settings and Playback are global utility spaces, not additional creative operating modes.
- Manual access is fast and reversible.
- Navigation does not require a permanently visible tab bar.
- Parameter-page colors remain stable enough to become learned control cues.
- Rolling capture preserves events that occur before the user decides to save them.
- Recorded source tracks remain separate and non-destructive wherever practical.
- Common state retains stable meaning across different visual styles.
- Sensor behavior changes explicitly between environmental observation and intentional control.
- Audio and recording reliability take priority over display richness.
- The interface should reward learned physical intuition rather than repeated menu reading.
- New features should deepen the instrument instead of turning it into a general-purpose workstation.
