# 0023. Clip selection, region editing and the empty Instrument

- **Status:** Proposed
- **Date:** 2026-10-05
- **Supersedes:** none
- **Beads:** `full_spooky_proto-v7l.10` (decision)

Items marked *(user 2026-10-05)* were agreed in conversation. Items marked *(agent)* are
proposals awaiting the user's call. The Open questions section is removed before
acceptance.

## Context

The only defined way to get audio into Instrument is the C-010 chord, which saves the
rolling window and loads it as the active sample. Nothing defines:

- Instrument with no clip. C-008 (Shift + Button 0) enters Instrument without loading
  anything.
- Loading a different clip when one is active.
- How a clip is cut from a recording that may be hours long.

[Modes and interaction](../../spec/product/modes-and-interaction.md) defines Playback as
a global utility with a session browser that separates Field sessions, standalone
captures and Instrument sessions. In Instrument, Shift + Encoder 1 press has no
assignment (in Field it is the Manual quick-jump, C-094).

Clip length is bounded by memory, not by choice ([0024](0024-clip-sources-and-track-mixing.md)).

## Options

1. **Load the first N seconds of the selected recording.** Rejected as the only path: for
   a long session it rarely contains the moment the user wants.
2. **Confirm first, then edit the region.** Rejected: a confirmation dialog followed
   immediately by another editor; the active clip changes before the user has heard the
   new one.
3. **Chosen: select, choose and audition a region, then one Load or Replace action that
   activates the clip only once it is ready.**

## Decision

**Objects**

1. A recording and a clip are different objects. A recording may be hours long; a clip
   is a bounded interval within it. Trimming never changes the recording.
   *(user 2026-10-05)*

**Entry**

2. Shift + Encoder 1 press in Instrument opens clip selection ("Load clip").
   *(user 2026-10-05)*
3. Entering Instrument with no active clip opens clip selection automatically.
   *(user 2026-10-05)*

**Selection and activation**

4. Clip selection reuses the session browser with a caller-specific action: Playback
   opens a recording for review; Instrument opens it to extract a region.
   *(user 2026-10-05)*
5. The browser filters by readable, compatible audio rather than by recording type.
   Instrument sessions are included, so a recorded performance can become source
   material. Recordings still being written (an active session, the rolling buffer) are
   hidden. *(user 2026-10-05; hiding user 2026-10-06)*
6. Activation order: select a recording, choose and audition a region, confirm Load or
   Replace, prepare and validate the audio, make it active only when ready. Selecting a
   recording establishes a candidate; it does not replace the active clip.
   *(user 2026-10-05)*
7. The Load or Replace action is the guard. A further warning appears only when
   replacing would discard source-dependent edits such as custom slices.
   *(user 2026-10-05)*

**Region editor**

8. A recording opens directly into the region editor. *(user 2026-10-05)*
9. Region editor controls. *(user 2026-10-05, revised 2026-10-06)*

    | Control | Turn | Click | Hold |
    |---|---|---|---|
    | Encoder 0 | Slide the window, keeping its length | Start or stop the looped audition | Load or Replace (item 7) |
    | Encoder 1 | Move the start | Previous event | — |
    | Encoder 2 | Move the end | Next event | — |
    | Encoder 3 | Zoom | Finer step | Back to the browser |

    Sliding the window is for finding the moment; moving a boundary changes the length
    and is for trimming. Both are kept. The Encoder 1 click departs from the back
    convention of C-003 and C-088, and the Encoder 3 click from page advance (C-024):
    the editor is a utility screen with no pages, and back moves to the Encoder 3 hold,
    as a hold returns elsewhere (C-084). The Shift layer is not available in the editor.
10. Default window: the first N seconds of a session, but the last N seconds of a
    rolling capture, because a rolling capture ends at the moment the user saved. N is
    the maximum clip length ([0024](0024-clip-sources-and-track-mixing.md)).
    *(user 2026-10-06)*
11. The editor shows a timeline on the OLED (128×64, one colour). *(user 2026-10-06)*
    - Radio and microphone are drawn as two stacked waveforms, each column a vertical
      line from the minimum to the maximum sample in its span.
    - Events (radio onsets, EMF peaks, PTT) are dotted vertical lines with a small icon
      per event type along the top. Icons that would overlap merge into one cluster
      mark.
    - The selected window is drawn inverted; during audition a moving line shows the
      playback position. A status row shows the position, the window length and the
      step.
    - The waveform comes from the peak track (item 21) and the events from the sidecars
      every recording already stores ([0010](0010-sd-backed-rolling-capture.md)
      item 2), never from reading hours of audio.
12. An Encoder 1 or Encoder 2 click jumps to the previous or next event: the window
    then starts a short pre-roll before the event, keeps its length, and the view
    scrolls with it. Jumps visit every event, including those inside a cluster mark.
    Filtering jumps by event type is a later addition. *(user 2026-10-06)*
13. The active clip's window can be re-edited later without going back through the
    browser: with a clip active, the item 2 gesture opens the region editor directly on
    that clip's recording and window, and the Encoder 3 hold goes back to the browser
    to choose another recording. With no clip active, the gesture opens the browser.
    *(user 2026-10-06)*

**No clip and failures**

14. Cancelling clip selection with no clip active leaves Instrument in an empty state:
    the OLED shows "No clip" and how to load one, and engines are silent. It does not
    return to Field. Cancelling with a clip active returns to the engine with that clip.
    *(user 2026-10-06)*
15. A load that fails (missing or corrupt file, card removed) leaves the previous clip
    active. With no previous clip, the empty state shows the fault. *(user 2026-10-06)*
16. C-008 back into Instrument restores the clip, engine and view that were active
    earlier in this power cycle. *(user 2026-10-06)*

**Zoom and limits**

17. Encoder 3 zooms around the window, from the whole recording down to a few
    milliseconds across the screen. A detent of any turn moves one column, so the step
    always matches the zoom. An Encoder 3 click divides the step by 4 and then 16 before
    returning to 1, shown on the OLED; changing the zoom resets it. Steps never go below
    one sample. *(user 2026-10-06)*
18. A boundary stops at the maximum clip length and the OLED shows "MAX"; trimming
    never moves the other edge. A clip is at least 250 ms long: the Slicer's floor is
    16 slices of 10 ms ([0022](0022-slicer-engine.md) item 7), and Granular already
    clamps grain size to the clip. *(user 2026-10-06)*

**Recording, storage and the chord**

19. Clip selection, including re-editing the active window, is rejected during a session
    with a visible reason. [0008](0008-recording-safe-command-policy.md) already
    rejects Playback and Instrument engine switches during a session; loading also reads
    the card while the recorder writes (Principle I). Revisit after the M4 storage-stall
    measurements. *(user 2026-10-06)*
20. A clip is a reference, never a copy: a stable recording asset ID
    (`full_spooky_proto-hpq.3`) and a window in samples. Presets store the reference,
    beside the Slicer's slice map (0022 item 14). The active clip's reference survives
    a power cycle as part of the persistent-settings work; if its recording is missing,
    the empty state shows the fault. The repair flow stays with control map D-009.
    *(user 2026-10-06)*
21. Every recording stores a peak track: the minimum and maximum of each 256-sample
    block (about 5 ms) per track, about 1.4 MB per track per hour, with coarser levels
    derived from it. It is computed from recorder blocks outside interrupt context.
    Below about 5 ms per column the editor reads the audio itself, a few kilobytes.
    Recordings made before the format are scanned once while no session records, and
    the result is cached. *(user 2026-10-06)*
22. In the product, a C-010 load failure keeps the user in Field with a visible fault
    naming which half failed, the save or the load, as the demo does
    ([0011](0011-halloween-2026-demo-build.md) item 15). The chord starts in Field,
    where rolling capture continues; a saved capture can be loaded later through clip
    selection. Item 15 applies to loads started in Instrument. *(user 2026-10-06)*

## Open questions

None remain. The user's calls of 2026-10-06 are recorded in items 5 and 9 to 22.

## Product document changes on acceptance

These are proposals to protected documents and need separate approval.

- [Control map](../../spec/product/control-map.md): new row C-112, Instrument, main
  engine page, Shift, Encoder 1 button, Press, "Open clip selection or the active
  clip's region editor", "Session browser filtered for clip sources", Defined, this
  record. Rows for the region editor controls in item 9.
- Control map D-015: clip selection is rejected during a session (item 19).
- Control map D-009: a clip is a recording asset ID plus a window (item 20).
- [Modes and interaction](../../spec/product/modes-and-interaction.md), Field To
  Instrument Workflow and Playback And Session Browser: the browser's caller-specific
  action, the recording-versus-clip distinction, the region editor's timeline, and the
  empty Instrument state.

## Consequences

- The timeline in item 11 depends on the session format storing the peak track
  (item 21), derived metrics and events (`full_spooky_proto-hpq.3`).
- Once the M4 renders the UI, the M7 sends it a timeline summary of about 256 bytes per
  redraw plus the visible events.
- The demo builds none of this; it shows the empty state only
  ([0020](0020-halloween-demo-slicer-and-sequencers.md) item 14).

## Evidence

None yet.
