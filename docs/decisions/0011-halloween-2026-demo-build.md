# 0011. Halloween 2026 demo build

- **Status:** Proposed
- **Date:** 2026-09-30
- **Supersedes:** none
- **Beads:** demo epic and tasks to be created on acceptance (label `track-demo`);
  product tasks pulled forward: `full_spooky_proto-8lw.9`, `full_spooky_proto-54w.6`,
  `full_spooky_proto-54w.7`, `full_spooky_proto-54w.8`, `full_spooky_proto-54w.9`,
  `full_spooky_proto-v7l.1`; product tasks narrowed by demo-only work:
  `full_spooky_proto-54w.5`, `full_spooky_proto-hpq.5`, `full_spooky_proto-v7l.2`

## Context

The user wants to publish a short video in the week of 2026-10-26 that shows the core
product thesis on the prototype: Field Mode with Classic scanning, honest EMF and
radio-activity feedback on the matrix and button LEDs, band switching, a session with
spirit-box scanning and PTT questions, and then a moment caught outside any session
carried straight into Instrument Mode and turned into a pad or chopped loop.

This departs from the milestone sequence in `spec/product/roadmap.md`, which Principle
VII binds work to. M1 and M2 have open gates (`jjy.2`–`jjy.4`, `jjy.6`, `8lw.9`), and
the demo draws on M3, M4 and M5 scope. The departure is allowed only if the roadmap
demo-track amendment proposed alongside this record is approved. This record is the
authorization and scope that amendment requires.

Facts that shape the scope, from the current tree:

- `RadioControl_Tune`, `TuneStep` and `SwitchBand` block the foreground loop, polling
  with `HAL_Delay(2)` for up to `RADIO_DEVICE_TIMEOUT_MS` (2 s), while recorder queues
  hold about 683 ms (`54w.6` notes). Classic scanning needs non-blocking radio control
  (RadioSm, decision 0006) even outside a session.
- The firmware rejects `TUNE`, `UP`, `DOWN` and `BAND` while recording. Principle VI
  keeps that guard until a bench test proves the workload safe (decision 0003).
- The matrix animation is suspended during recording in the bring-up adapter
  (`8lw.9` notes). Running it during a session is new, measured work.
- The monitor stage is a radio passthrough (`RenderMonitor` in
  `CM7/App/audio_path_service.c`), a clean seam for the mic mix, PTT and Instrument
  output.
- About 196,608 bytes of AXI `RAM_DMA` are free (decision 0010). Rolling capture must
  therefore be SD-backed, and an Instrument clip held in RAM is a few seconds long.
- Every Field gesture in the script is Defined in the control map: C-090/C-093 session
  prompt, C-099–C-102 band menu, C-103–C-106 Classic, C-016/C-017 PTT, C-009 save,
  C-010 save-switch-load chord, C-028 return to Field. Instrument parameter
  assignments (C-025) are Proposed.
- The rig runs from a 3.7 V, 3,700 mAh LiPo (prototype hardware). A random-wire
  antenna uses the SW whip path selected by the PC6 antenna switch.

## Options

1. **Follow the roadmap as written.** Rejected for this demo. M5 cannot exit by the
   end of October.
2. **Stage the video with pre-recorded audio or scripted visuals.** Rejected. It
   violates Principle II, and a video that the device cannot reproduce undermines the
   product thesis it is meant to show.
3. **A long-lived demo fork with its own relaxed rules.** Rejected. It would diverge from
   `8lw.9` and the M3 work it depends on, and would leave two readings of the
   constitution in the repository.
4. **Chosen: a time-boxed demo track.** Product-grade work lands on `main` through the
   normal gates. A thin layer of demo-only behavior lives on a demo branch behind an
   opt-in `Demo` preset. Its departures are listed below, and it ends on a fixed date.

## Decision

**Scope and script**

1. The demo image boots into Field/Classic and supports this script, in order:
   Classic scanning with run/pause, rate, distance and direction; the band menu, used
   outside a session; a session started and stopped with the Button 0 prompt; Classic
   scanning and Button 1 PTT during that session; a C-010 chord outside a session that
   saves the rolling window, switches to Instrument and loads a clip; Instrument
   performance on that clip; C-028 back to Field.
2. Seek and Orbit are out of scope. A Seek-like engine may be proposed as a stretch
   task only if checkpoint 2 (item 21) passes with every required task on track.

**Branches, build and tracking**

3. Product-grade work lands on `main` first and is merged into the demo branch
   `demo/halloween-2026`. Demo-only code exists only on that branch and builds only in
   an opt-in `Demo` preset (`SPOOKY_DEMO=1`). Debug and Release on the demo branch
   behave as on `main`.
4. The demo branch is never merged into `main`. The shoot image is tagged
   `demo-halloween-2026-shoot`.
5. Beads is not branched. The demo has one epic labelled `track-demo`. Each demo task
   links to the product task it depends on or narrows. A demo task never closes a
   product task; it appends a note to that task naming what remains.

**Product work pulled forward (full acceptance, on `main`)**

6. `8lw.9`: run the pending board regressions and merge `feature/ui-service-split`.
7. RadioSm non-blocking radio control, split out of `54w.6` as its own product task.
8. `54w.8`: the user decides the EMF, radio-activity and warning mapping (control map
   D-016) in its own decision record, then the mapping is implemented with host tests.
9. `54w.7`: Classic first. Manual may remain open.
10. `54w.9`: mic monitoring and monitor-only PTT in `RenderMonitor`. Sample-timestamped
    PTT events remain open under `54w.9` until `hpq.1` lands.
11. The granular voice's portable core goes in `Common/` with host tests, intended as
    the start of `v7l.1`. `v7l.1` stays open until it runs over the accepted capture
    format.

**Demo-only behavior (`Demo` preset only)**

12. **Field on M7 without product IPC.** The Context and InputResolution machines drive
    Classic, the band menu, the session prompt and the render service directly on M7.
    The M4 stays asleep. This narrows `54w.5`; it does not transfer ownership
    (Principle III is unchanged). The matrix runs during sessions at a reduced, measured
    frame rate and yields to capture (Principle I).
13. **Scanning while recording.** The tune guard is lifted in the `Demo` preset only,
    and only after a bench run on that image shows Classic scanning during recording
    with uninterrupted WAV accounting, no overrun and bounded queues. The run is
    recorded in `docs/evidence/`. That evidence qualifies the demo image only, not
    `54w.6`. The band guard stays in place during sessions.
14. **Rolling capture.** The demo follows decision 0010's shape: one write stream,
    blocks written once, saves that pin by reference and never copy audio. It writes
    fixed-length segment files outside sessions and keeps the newest 60 s. A save
    renames the window's segments into a capture folder and writes a small descriptor.
    Outcomes are `writing`, `saved`, `busy`, `unavailable` and `failed`, following
    decision 0010 item 8 for a second save. The hpq.3 format, crash recovery and
    session-shared blocks are not implemented. During a session, rolling capture is
    off and C-009 is rejected with a visible reason. This is a demo narrowing of C-009,
    not a product change.
15. **Field to Instrument.** The C-010 chord outside a session saves, switches to
    Instrument and loads the clip. The clip is the mono sum of the radio channels,
    decimated to 24 kHz, 16-bit, at most 3 s long (144,000 bytes), ending at the save
    point. It is held in free AXI SRAM, and the final length is set from the link map at
    implementation. A load failure returns to Field with a visible fault and never
    shows a clip that did not load (Principle II). C-028 returns to Field. The session
    prompt in Instrument is rejected with a visible reason.
16. **Instrument performance.** One granular voice feeds the monitor path. Parameter
    assignments are provisional and demo-only, and they do not settle C-025. Page 1 is
    position, grain size, density and pitch on Encoders 0–3. Page 2 (C-024) is spray,
    slice quantization (off, 4, 8 or 16, which gives the chopped loop), envelope and
    level. Button 1 has no Instrument function in the demo (C-029 stays Proposed). The
    matrix shows grain activity, not EMF, so the Field-to-Instrument sensor policy is
    not exercised (Principle II).

**Principles**

17. **Kept without exception: Principles I–IV.** Capture keeps priority over display.
    Every consequential outcome is explicit and truthful. Ownership does not change.
    New queues and loops have declared bounds and counters. Filesystem calls never run
    in audio callbacks.
18. **Principle V.** Portable demo logic (rolling segment catalog, clip decimation,
    granular core) has host tests. Demo evidence is labelled as demo-image evidence.
    README and design documents do not list demo-only behavior as proven.
19. **Principle VI.** Items 3, 4 and 13 keep the `main` baseline and its guards
    unchanged. Debug and Release build and the M1 recording regression passes on
    `main` after each pulled-forward change.
20. **Principle VII.** The departure from the milestone sequence is limited to items
    6–16 and ends on the date in item 22. The alternative that keeps the sequence
    (option 1) was rejected above.

**Schedule and checkpoints**

21. The user decides at each checkpoint whether to continue, cut scope or stop.
    - **Checkpoint 1, 2026-10-07:** RadioSm and Classic run on hardware; the matrix
      reflects EMF and radio activity; the `54w.8` mapping is accepted.
    - **Checkpoint 2, 2026-10-14:** the scanning-while-recording bench run (item 13)
      passes. If it fails, the video shows spirit-box scanning outside a recorded
      session instead. Rolling save and the chord reach Instrument with a plain loop
      of the clip.
    - **Checkpoint 3, 2026-10-21:** firmware freeze. The granular voice runs on
      hardware; otherwise the video uses the plain loop with slice quantization.
    - **2026-10-21 to 2026-10-23:** three consecutive clean rehearsals of the full script
      on the tagged image, with battery runtime and WAV inspection, recorded in
      `docs/evidence/`.
    - **2026-10-24 to 2026-10-25:** shoot. Publish by 2026-10-30.
22. The demo track ends on 2026-11-06. By then the demo epic is closed, notes are
    appended to every linked product task, the demo branch is frozen, and anything
    worth keeping from it is re-proposed as product work.

## Consequences

- M1 gates `jjy.3`, `jjy.4` and `jjy.6` and tooling task `5yv.1` wait unless they are
  blocking the demo. `jjy.2` continues if bench time allows. None of their exit
  criteria change.
- `54w.8`, RadioSm and the mic monitor stage land in product form earlier than
  planned. That is roadmap order changing, not scope growing.
- The rolling-capture and Instrument code on the demo branch are throwaway by default.
  `hpq.5` and `v7l.2` are implemented afresh to decision 0010, `hpq.3` and their own
  acceptance criteria. They may reuse demo code only after it has been reviewed as
  product work.
- The public video shows a prototype and must not state that the demo-only behaviors
  are finished product capabilities.
- Revisit this record if a checkpoint fails and the user chooses a different cut, or if
  the date moves.

## Evidence

- [Ten-minute recording baseline](../evidence/2026-09-26-ten-minute-recording-baseline.md)
- [Foreground latency](../evidence/2026-09-28-foreground-latency.md)
- [Decision 0010](0010-sd-backed-rolling-capture.md) for the rolling-capture memory and
  SD arithmetic
- Demo-image bench evidence (item 13 and the rehearsals) is pending the demo tasks.
