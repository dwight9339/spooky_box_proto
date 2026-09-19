# Spooky Box Design Philosophy, Functional Requirements, and Software Architecture

## 1. Product Vision

Spooky Box is an experimental field instrument that combines radio scanning, microphone capture, EMF sensing, visual feedback, session recording, and performable interaction into a cohesive handheld device.

The device should feel like a real instrument rather than a novelty effect. Its behavior should be explainable in engineering terms, but its presentation should leave room for ambiguity, ritual, and emotional interpretation. The experience should live somewhere between field recorder, radio scanner, synthesizer controller, and haunted measuring device.

The system should prioritize immediacy, responsiveness, and legibility. Controls should feel physical and intentional. Visual feedback should reinforce what the device is doing without requiring the user to read a detailed screen constantly.

## 2. Core Design Philosophy

### 2.1 Instrument First

Spooky Box should behave like an instrument with modes, ranges, sensitivity, modulation, and feedback. It should not feel like a menu-driven toy where effects are simply triggered. The user should be able to build intuition over time: how the knobs respond, how the scan engines behave, how EMF affects the system, and how radio activity changes the visual texture.

### 2.2 Stable Meaning, Variable Texture

The system should keep core meanings consistent across modes:

- EMF level represents field disturbance relative to a calibrated ambient baseline.
- Radio activity represents detected activity or change in the incoming radio audio.
- Scan position represents where the radio engine is moving through its current territory.
- Session state represents whether the device is idle, armed, recording, or reviewing.

Different scan engines and modes may present these meanings with different visual textures, but the underlying interpretation should remain stable.

For example, the LED matrix may use different animations for classic scan, orbit scan, or interband scan, but EMF should still generally influence color, brightness, speed, or intensity, while radio activity should still inject liveliness, flicker, shimmer, or transient motion.

### 2.3 Semantic UI Over Raw Pixels

The main application should describe what is happening, not micromanage every visual output.

Instead of pushing pixels to the LED matrix from the main application loop, the system should eventually send semantic state such as:

- current mode
- scan engine
- EMF level
- EMF bucket
- radio activity
- scan position
- band
- recording state
- one-shot events such as signal spikes or band transitions

The UI layer should then decide how to render that state.

### 2.4 Prototype Quickly, Architect for Separation

The project is pivoting away from a Teensy-centered compute stack and toward the STM32H745/H755 dual-core family as the primary platform. This should simplify the final architecture by keeping core real-time work, UI coordination, storage, and peripheral control inside one coherent MCU family instead of depending on the Teensy ecosystem plus an external UI coprocessor from the start.

The codebase should still preserve strong software boundaries. The important architectural idea is not "use a separate chip for UI"; it is "keep recording-critical, radio-control, sensor-processing, and presentation workloads isolated enough that one domain cannot starve another."

The recommended path is now:

1. Use the Nucleo-H745/H755-class board as the main development platform.
2. Prove SDMMC, card detect, audio I/O, radio control, sensors, display, and LED/UI workloads incrementally.
3. Assign recording-critical and high-rate DSP work to the Cortex-M7 side by default.
4. Assign UI service, lower-priority control, input scanning, display/matrix coordination, and housekeeping to the Cortex-M4 side where useful.
5. Keep semantic UI boundaries so a future external UI coprocessor remains possible, but no longer treat it as the default first integration path.
6. Spin focused custom hardware only where breakouts are insufficient, especially the RF/antenna section.

## 3. Hardware Architecture Direction

### 3.1 Main Compute Platform

The STM32H745/H755 dual-core family is now the preferred primary controller platform for Spooky Box.

The STM32 owns:

- radio control
- audio capture and processing
- microphone and radio recording
- session state
- SDMMC storage
- scan engines
- EMF sensor processing
- high-level mode state
- mapping input events to behavior
- display state and UI semantics
- LED matrix state generation or rendering, depending on the prototype phase
- power, battery, and system health monitoring

The architectural advantage is that the product can be developed as a single embedded system with two cooperating cores rather than as a main controller plus a mandatory external UI computer. This should reduce inter-board protocol risk, simplify firmware deployment, and keep the final design less tied to the Teensy ecosystem.

### 3.2 Dual-Core Responsibility Split

The default split should be:

**Cortex-M7: real-time core / source of truth**

- audio capture and buffering
- audio DSP and activity metrics
- session recorder
- SDMMC filesystem/write coordination
- radio controller and scan engines
- EMF processing
- mode/session state
- event generation
- safety-critical timing and resource arbitration

**Cortex-M4: UI/service core**

- input scanning and debounce
- encoder quadrature decoding
- button LED PWM, blinking, and breathing effects
- LED matrix animation rendering or frame preparation
- display rendering where practical
- menu/state presentation
- non-critical housekeeping
- diagnostics and debug/status surfaces

This split is a starting point, not a hard rule. The guiding principle is that audio recording and storage reliability must be protected from visual animation, display refreshes, and control-surface chatter.

Inter-core communication should pass semantic state and events rather than raw pixels or tight shared objects. The M7 should publish what is true about the device; the M4 should decide how to present it.

### 3.3 External UI Coprocessor Status

An external UI coprocessor, such as an RP2040, is no longer the default next step. The STM32H745/H755 already provides a second core that can absorb much of the workload that previously motivated a separate coprocessor.

A future external UI coprocessor remains a valid escape hatch if later prototypes show that the STM32's second core is not enough, if the front-panel board benefits from local intelligence, or if LED/display hardware physically wants to live on a detachable UI module.

For now, the architecture should plan for:

- STM32 dual-core first
- semantic UI boundaries preserved
- optional future external UI processor, not mandatory
- no dependence on Teensy-specific libraries or assumptions

### 3.4 Display Ownership

Near-term, either STM32 core may draw the TFT or OLED display depending on driver maturity and bus ownership. A likely default is for the M4 to own visual rendering while the M7 publishes state.

Long-term, display ownership should be decided by timing, bus topology, and physical PCB layout. If the display becomes animation-heavy, it likely belongs with the UI/service side. If it remains mostly informational, it may be acceptable for the main application side to update it directly through a controlled interface.

The architecture should not assume forever that any particular core owns the display. It should assume that display rendering is downstream from semantic application state.

### 3.5 LED Matrix Driver

The prototype uses an Adafruit IS31FL3741 13x9 PWM RGB LED matrix breakout. The final design is expected to use a square matrix, likely 9x9.

During prototyping, the 13x9 breakout can be treated as a physical surface with a centered 9x9 logical viewport. This allows the animation design to target the intended final form factor while using available hardware.

The IS31FL3741-style architecture is appropriate for Spooky Box because the LED matrix is an expressive instrument display, not a high-frame-rate video display. The matrix should show pulses, gradients, scan movement, field intensity, radio texture, signal events, and mode identity.

The driver code should remain modular so the final PCB can use:

- IS31FL3741 or similar matrix driver
- another ISSI/Lumissil LED driver
- smart RGB LEDs
- an STM32 M4/service-core-controlled display surface
- a future external UI coprocessor-controlled display surface

Application logic should not depend directly on IS31FL3741 register details or on which STM32 core ultimately drives the matrix.

## 4. LED Matrix Role and Behavior

### 4.1 Primary Role in Field Mode

In field mode, the LED matrix should primarily indicate EMF level through some combination of:

- color
- brightness
- animation speed
- visual intensity
- density
- instability

The matrix should make the device feel reactive to the surrounding field.

### 4.2 Radio Activity as Texture

Radio audio activity should influence the animation as texture rather than replacing the core EMF meaning.

EMF determines the weather.
Radio activity makes the ghosts move.

Radio activity can modulate:

- flicker
- shimmer
- spark amount
- pulse depth
- jitter
- scan trail length
- asymmetry
- transient flashes
- button LED accents

The radio activity metric should be derived from audio processing and may include:

- RMS level
- spectral flux
- transient hits
- squelch/open-state cues
- signal confidence or activity heuristics

### 4.3 Per-Scan-Engine Animation Identity

Each scan engine should have its own matrix animation style, while still preserving the same core meanings.

Examples:

- Classic scan: clean sweep line, tuning cursor, scan trail, or rhythmic pulse.
- Orbit scan: particles orbiting attractors or converging around a center.
- Random/chaotic scan: scattered sparks, unstable flashes, noisy texture.
- Interband scan: color-domain shifts or wipes when moving between bands.
- Calibration: settling particles, stabilizing center, or convergence animation.
- Instrument mode: modulation-focused patterns that respond directly to the magnetometer as a control source.

The display can explain the mode explicitly, while the LED matrix gives the mode a felt signature.

## 5. Radio and Scan Architecture

### 5.1 Scan Mode vs Territory

The scan engine should separate how movement happens from where movement is allowed to happen.

Scan mode describes motion:

- off/manual
- linear up
- linear down
- ping-pong
- random
- seek-like behavior
- orbit or other future engines

Band or territory describes the available frequency space:

- FM
- MW/AM
- SW
- LW
- weather, if supported
- curated interband territory
- custom territories

The scan engine should operate over TuneTargets or ScanRanges, not raw global frequencies.

### 5.2 Tune Target Model

The radio layer should receive a structured target such as:

- band/mode
- frequency
- step size
- optional range metadata
- RF input path

The scan engine should not directly call low-level radio library functions. It should emit a desired target. The radio controller decides whether that target is a cheap same-band tune or a more expensive mode/band transition.

### 5.3 Band Switching and Interband Scanning

Band switching is not the same as tuning.

Changing frequency within FM is relatively cheap. Moving between FM and AM/SW/LW may require mode changes, property updates, antenna routing changes, muting, and settling time.

Interband scanning should be modeled as movement through a curated list of ranges rather than one continuous frequency axis.

A scan territory may contain entries such as:

- FM 87.5 to 108.0 MHz
- AM 520 to 1710 kHz
- SW 5.8 to 6.2 MHz
- SW 9.4 to 10.0 MHz

The scanner may walk through these ranges linearly or choose among them randomly with weights.

### 5.4 Si4732 / Si4735 Library Considerations

The current library treats AM, MW, LW, SW, and SSB-like behavior under AM-mode power-up, while FM is a separate power-up mode. Therefore, software should model:

- FM as FM mode
- MW/AM as AM mode
- SW as AM mode
- LW as AM mode

The user's experimentation suggests that SW should be treated as an AMI-path mode, not as a direct FMI-path mode. Although simplified diagrams sometimes label an FM/SW antenna path, practical testing showed SW tuning had meaningful noise changes when the antenna was connected to AMI rather than FMI.

Final hardware should therefore plan for:

- FM using FMI
- AM/MW using AMI with an AM-style antenna network
- SW using AMI with a whip, long-wire, or wideband antenna network
- possible switching or configurable routing into AMI

AM and SW both wanting AMI is a key hardware design constraint because MW and SW may prefer different antenna networks.

### 5.5 Audio Behavior During Band Changes

Band changes may produce pops, mutes, or discontinuities. The system should make this intentional.

Recommended behavior:

- emit a BandTransitionStart event
- fade or mute radio audio briefly
- switch RF path and radio mode
- tune to the target frequency
- wait for tune/status completion or a short settle period
- emit BandTransitionComplete
- restore audio with a short fade

The LED matrix and button LEDs can use these events to make transitions feel deliberate.

## 6. Classic Mode Control Philosophy

### 6.1 Current Control Direction

Classic mode currently uses:

- Encoder 0 turn: manual tuning in off mode; jump rate in linear scan modes
- Encoder 0 click: change scan mode
- Encoder 1 turn: jump size in linear scan modes; tuning step size in off/manual mode
- Encoder 1 click: band or territory switching

This creates two conceptual axes:

- Encoder 0: motion
- Encoder 1: scale and territory

### 6.2 Manual Mode Analog Feel

Although the Si4732 is a DSP tuner, manual classic tuning should feel analog-ish. The tuning experience should feel like moving through a medium rather than setting a number.

Useful techniques:

- tuning inertia
- smoothing between target frequency and current displayed/commanded frequency
- micro-drift in display or modulation values
- station capture or sticky frequency behavior using RSSI/SNR/audio activity
- nonlinear encoder response based on turn velocity
- friction zones near signals
- subtle imperfect stepping
- speed-dependent noise or visual instability

Manual mode should feel different from scan mode:

- Manual: continuous, tactile, weighted, expressive.
- Scan: rhythmic, mechanical, clocked, procedural.

## 7. EMF Sensor Calibration and Processing

### 7.1 Purpose

The magnetometer is used primarily as a relative disturbance sensor, not as an absolute compass. It should measure deviation from ambient magnetic conditions.

### 7.2 Startup Calibration

On boot, the device should collect magnetometer samples for a fixed window, such as 2 to 5 seconds.

For each sample:

- compute magnitude: sqrt(x*x + y*y + z*z)

The startup calibration should reject outliers or spikes using a simple robust method such as median filtering or ignoring values beyond a standard deviation threshold.

It should compute:

- baselineMagnitude
- noiseFloor

The noise floor should have a reasonable minimum to avoid divide-by-zero and hypersensitivity.

### 7.3 Continuous EMF Calculation

For each new sample:

- compute magnitude
- compute delta = abs(magnitude - baselineMagnitude)
- normalize delta from [noiseFloor, threshold] to [0.0, 1.0]
- clamp to [0.0, 1.0]

Then apply exponential shaping:

- shaped = pow(normalized, gamma)
- gamma should be tunable, with a default around 2.2 and a useful range around 2.0 to 3.0

Then apply smoothing to produce a stable output.

The system should expose:

- raw magnitude
- normalized EMF level
- shaped EMF level
- smoothed EMF level
- discrete EMF bucket

### 7.4 Discrete Buckets

The shaped EMF value can be grouped into N discrete buckets:

- bucket = floor(shaped * N)
- clamp to [0, N - 1]

Because shaping is exponential, higher buckets require stronger signals while lower ranges retain useful resolution.

### 7.5 Adaptive Baseline

Adaptive baseline drift should be configurable at runtime.

Enable adaptive baseline by default in field mode.
Disable adaptive baseline by default in instrument mode.

When enabled:

- slowly adapt baselineMagnitude toward the current magnitude
- only adapt when the signal is quiet
- do not adapt during spikes or high activity
- use a very small alpha, such as 0.0005 to 0.005

When disabled:

- continue computing EMF relative to the existing baseline
- do not update baselineMagnitude or noiseFloor

This matters because in instrument mode the user may intentionally manipulate the magnetic field as a modulation source. Adaptive baseline drift would make the control feel like it is moving under the user's hand.

Manual recalibration should remain available regardless of adaptive mode.

## 8. Session Recording Requirements

### 8.1 Current Prototype State

Early STM32/Nucleo testing has validated SDMMC read/write and card detect using a breakout. Further storage stress testing should resume once the audio shield/codec hardware is available, because the important validation target is not SDMMC in isolation; it is SDMMC operating reliably while audio capture, radio control, UI updates, sensors, and display activity are active.

The earlier Teensy prototype remains useful as a behavioral reference for field mode, classic scan behavior, session recording expectations, display output, and LED matrix animation. It should no longer define the final compute architecture.

### 8.2 Recording Priority

Audio recording reliability should take priority over UI animation smoothness.

The system should avoid blocking or jitter-inducing operations in the recording-critical path. Display updates, LED matrix updates, button LED effects, input polling, radio housekeeping, and inter-core communication should not be allowed to starve audio buffers or storage writes.

### 8.3 Dual-Core Motivation

The STM32H745/H755 dual-core architecture exists partly to protect recording-critical work from UI timing and bus traffic.

The M7 should be treated as the conservative home for audio, recording, SDMMC coordination, radio scan decisions, and authoritative device state. The M4 should absorb UI rendering, input scanning, button LED behavior, matrix animation, and other work that can tolerate brief delays or degraded frame rate.

This approach keeps the project in one MCU ecosystem while preserving the same separation of concerns that originally motivated a UI coprocessor.

## 9. Software Architecture

### 9.1 High-Level Split

The software should be organized around domain ownership.

Main application domain:

- modes
- scan engines
- radio control
- audio processing
- recording
- storage
- EMF processing
- settings
- session lifecycle
- mapping inputs to behavior

UI domain:

- display rendering
- LED matrix scenes
- button LED feedback
- input scanning/debouncing/decoding
- visual transitions
- UI-side animation timing

Communication domain:

- shared protocol
- state packets
- event packets
- input event packets
- health/status packets
- firmware update management placeholder

### 9.2 Suggested Codebase Layout

A useful project layout:

```text
/spooky-box
  /common
    Protocol.h
    UiState.h
    UiEvents.h
    RadioTypes.h
    SensorTypes.h
    AudioTypes.h
    FixedPoint.h
    SharedMemoryMap.h

  /firmware-stm32h745
    /core-m7
      App.cpp
      FieldMode.cpp
      InstrumentMode.cpp
      RadioController.cpp
      AudioEngine.cpp
      SessionRecorder.cpp
      StorageManager.cpp
      SensorManager.cpp
      EmfProcessor.cpp
      ScanEngines/
        ClassicScanEngine.cpp
        OrbitScanEngine.cpp
      IntercoreLink.cpp
      SystemStatePublisher.cpp

    /core-m4
      UiService.cpp
      UiProtocol.cpp
      MatrixRenderer.cpp
      DisplayRenderer.cpp
      ButtonLedRenderer.cpp
      InputScanner.cpp
      DiagnosticsView.cpp
      scenes/
        ClassicScanScene.cpp
        OrbitScene.cpp
        CalibrationScene.cpp
        MenuScene.cpp
        InstrumentScene.cpp
      drivers/
        IS31FL3741Driver.cpp
        DisplayDriver.cpp

    /shared
      linker/
      startup/
      board/
      stm32h745xx_hal_conf.h

  /firmware-experiments
    /nucleo-sdmmc-test
    /audio-shield-bringup
    /rf-shield-bringup
    /ui-matrix-test

  /tools
    protocol-test/
    asset-converter/
    log-decoder/

  /docs
    protocol.md
    architecture.md
    hardware-prototyping.md
```

The exact build system can change, but the repository should make the M7/M4 boundary obvious.

### 9.3 UI Service Abstraction

The application should depend on a UI/service interface, not on a specific core, transport, display driver, or LED matrix driver.

Example concept:

```cpp
class UiService {
  pollInputEvents();
  sendState(UiState state);
  sendEvent(UiEvent event);
  sendCommand(UiCommand command);
  getStatus();
};
```

There should be at least two useful implementations over time:

- LocalUiService: single-core or same-core implementation for early bring-up and tests
- DualCoreUiService: STM32 M7-to-M4 implementation using shared memory, hardware semaphores, mailboxes, or another STM32-appropriate IPC mechanism

A future RemoteUiCoprocessor implementation can still exist if an external RP2040-style UI board becomes useful later.

This lets the project prototype quickly while preserving the eventual hardware and firmware boundaries.

### 9.4 State Packets vs Event Packets

The system should separate persistent state from one-shot events.

Persistent UI state answers: what is true right now?

Examples:

- mode
- scene
- band
- scan engine
- recording state
- EMF level
- EMF bucket
- radio activity
- scan position
- scan velocity
- PTT state
- battery state

One-shot UI events answer: what just happened?

Examples:

- band transition started
- band transition completed
- signal spike
- session armed
- recording started
- recording stopped
- PTT down
- PTT up
- calibration started
- calibration completed
- error occurred

Commands answer: what should the UI side explicitly do?

Examples:

- set brightness
- set scene
- set palette
- set button LED override
- enable/disable animation
- enter diagnostic mode

### 9.5 Input Boundary

The UI/service side should report decoded but behavior-neutral input events.

For example:

- encoder 0 moved +3
- encoder 1 moved -1
- encoder 0 clicked
- PTT button down
- PTT button up

The main application decides what those inputs mean based on current mode.

The UI/service side should not decide that encoder 0 means tuning, scan rate, menu navigation, or parameter editing. It only reports the input.

### 9.6 Inter-Core Communication

For the STM32H745/H755 target, the first communication boundary to design is M7-to-M4, not main-MCU-to-external-coprocessor.

The exact IPC mechanism can be chosen during firmware bring-up, but the architecture should support:

- regular state updates from M7 to M4
- asynchronous input events from M4 to M7
- one-shot events from M7 to M4
- commands from M7 to M4
- health/status reports in both directions
- versioning of shared packet structures
- validation to detect malformed packets or stale shared state

Likely STM32 mechanisms include shared SRAM regions, hardware semaphores, interrupt notifications, lock-free ring buffers, and carefully owned DMA buffers. The design should avoid casual shared mutable state between cores.

### 9.7 Firmware Update Management

Firmware update management should focus first on reliable programming/debug access for the STM32 target and repeatable flashing of both cores.

If a future external UI coprocessor is added, the architecture can reintroduce a remote firmware lifecycle manager with states such as:

- not present
- running
- bootloader
- updating
- verifying
- fault

For the dual-core STM32 prototype, the near-term requirement is simpler: keep M7 and M4 firmware versioned together, expose build/version status in diagnostics, and avoid protocol changes that make one core's firmware silently incompatible with the other.

## 10. PCB Prototyping Strategy

### 10.1 Rationale for Split Prototypes

The project should avoid putting every risky subsystem onto one first integrated PCB. Some parts are straightforward, while others are unpredictable and deserve isolated testing.

The tricky areas include:

- STM32H745/H755 dual-core bring-up and peripheral ownership
- SDMMC reliability under audio load
- codec/audio noise and clocking
- radio RF performance
- AM/SW antenna routing
- power supply noise
- LED matrix current and bus traffic
- display bus activity
- mechanical alignment
- debug/programming access for both cores

Splitting the PCB prototypes allows progress on stable subsystems without being blocked by RF, power, audio, or UI integration issues.

### 10.2 Recommended First Custom PCB: RF Shield

Given the hard pivot to STM32 and the successful early SDMMC/card-detect breakout tests, the next custom PCB should likely be a focused RF shield rather than a UI coprocessor board.

The RF shield should include:

- Si4732/Si4735 radio circuit
- AMI and FMI access
- configurable antenna networks
- jumpers or 0-ohm links for routing experiments
- optional analog switch footprints where useful
- RF test points
- audio output test points
- options for ferrite, whip, long-wire, or external antenna input
- footprints for optional filters, coupling components, and matching networks
- clean local power filtering
- STM32/Nucleo-compatible control and audio routing headers

This board should be designed for experimentation and bodge-friendly iteration. It exists to answer antenna/routing questions that breakouts cannot answer cleanly.

### 10.3 Breakout-Based Subsystem Prototypes

Most non-RF subsystems can remain breakout-based during the STM32 bring-up phase:

- audio codec or audio shield
- SDMMC breakout, until final storage layout is needed
- TFT/OLED display breakout
- LED matrix breakout
- magnetometer/EMF sensor breakout or movable daughterboard
- encoders/buttons on perfboard or a crude front-panel mockup
- power regulator/charger breakouts during load characterization

This keeps the custom hardware workload focused on the parts where PCB layout and physical routing matter most.

### 10.4 Optional UI Board

A custom UI board may still make sense later, but it is no longer the first architecture-proving PCB.

A later UI/front-panel board may include:

- LED matrix driver or connector
- 9x9 RGB LED matrix or breakout-compatible footprint
- button LED drivers/PWM capability
- button and encoder input hardware
- TFT/OLED connector
- optional local UI processor footprint only if STM32 dual-core testing shows it is needed
- debug/test pads
- current measurement options
- mechanical alignment features for enclosure/front-panel work

The first goal of that board would be ergonomic and physical integration, not proving that a separate processor is required.

### 10.5 Integration PCB

After STM32, RF, audio, UI, and power lessons are learned, a larger integration PCB can combine:

- STM32H745/H755 main compute section
- audio capture and routing
- radio subsystem
- display and LED matrix
- power and battery management
- storage
- controls
- EMF sensor or sensor connector
- enclosure-driven mechanical layout
- debug/programming access

The first integrated PCB should still be considered a validation board, not a final production board.

## 11. Practical Engineering Principles

### 11.1 Protect Audio and Recording

Avoid letting visual feedback interfere with audio buffer timing or recording reliability.

UI updates should be scheduled, assigned to the service core, offloaded, or deprioritized when necessary.

### 11.2 Make Transitions Intentional

Band changes, scan direction changes, recording start/stop, EMF calibration, and signal spikes should generate explicit events. These events can be used by the display, matrix, and button LEDs to create a coherent device personality.

### 11.3 Keep Hardware Abstractions Honest

The software should model real hardware constraints explicitly:

- band switching cost
- RF input path
- AMI vs FMI differences
- per-band step sizes
- antenna configuration
- tune settling
- audio muting windows
- inter-core communication health
- M7/M4 firmware compatibility
- optional external coprocessor availability

Hiding these realities too deeply will make later bugs harder to diagnose.

### 11.4 Prefer Semantic State Over Raw Control

The main application should not need to know how to draw a classic scan shimmer or an orbit pulse. It should report the state of the device. The UI layer should render that state.

### 11.5 Design for Debugging

Prototype PCBs should include:

- test pads
- labeled headers
- reset/boot access
- serial debug
- measurement jumpers
- optional footprints
- clear silkscreen
- isolation points for noisy subsystems

The first board should be easy to probe, bodge, and understand.

## 12. Near-Term Next Steps

1. Treat STM32H745/H755 as the primary compute target.
2. Keep the Teensy prototype only as a behavioral reference, not as the forward architecture.
3. Continue Nucleo bring-up around SDMMC, card detect, audio shield integration, and basic peripheral ownership.
4. Define the initial M7/M4 responsibility split.
5. Define shared UiState, UiEvent, UiCommand, InputEvent, RadioTypes, SensorTypes, and AudioTypes structures.
6. Implement a LocalUiService for early single-core/simple bring-up and a DualCoreUiService for M7/M4 communication.
7. Stop allowing core application code to directly depend on LED matrix implementation details.
8. Spin a focused RF shield for Si4732/Si4735, AMI/FMI routing, antenna experiments, test points, and STM32-compatible headers.
9. Use off-the-shelf breakouts for audio, display, LED matrix, EMF sensor, controls, and power until their requirements stabilize.
10. Leave external UI coprocessor support as an optional future branch rather than the default next hardware step.

## 13. Summary

Spooky Box should be built as a responsive, expressive field instrument. The STM32H745/H755 dual-core family is now the preferred primary compute platform. The M7 side should protect audio, recording, SDMMC coordination, radio/scan behavior, sensors, and authoritative state. The M4 side should absorb UI rendering, input scanning, LED/button behavior, display work, diagnostics, and other service tasks where useful.

The Teensy prototype remains valuable as a behavioral sketch, but the project should no longer depend on Teensy-specific hardware, libraries, or architecture assumptions.

The project still benefits from split PCB prototyping, but the priority changes. Instead of proving a separate UI coprocessor first, the near-term hardware plan should focus on STM32 bring-up with off-the-shelf subsystem breakouts and a custom RF shield for the radio/antenna section. RF remains the subsystem most deserving of a dedicated custom board because antenna routing, AMI/FMI behavior, filtering, and test access are hard to evaluate cleanly with generic breakouts.

The best next hardware step is likely a focused STM32-compatible RF shield, followed by broader system integration once audio, storage, UI, power, and enclosure constraints are better understood.
