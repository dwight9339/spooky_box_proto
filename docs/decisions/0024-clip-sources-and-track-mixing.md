# 0024. Clip sources, track mixing and maximum clip length

- **Status:** Proposed
- **Date:** 2026-10-05
- **Supersedes:** none
- **Beads:** `full_spooky_proto-v7l.11` (decision)

Items marked *(user 2026-10-05)* were agreed in conversation. Items marked *(agent)* are
proposals awaiting the user's call. The Open questions section is removed before
acceptance.

## Context

Recordings hold radio left, radio right and microphone as separate synchronized tracks.
Field playback already plans independent radio and microphone gain, mute and solo
([modes and interaction](../../spec/product/modes-and-interaction.md), Field Playback).
No document says which tracks an Instrument clip carries or how an engine plays them.

Memory bounds the clip. The demo clip is mono radio, 24 kHz, 16-bit, at most 3 s
(144,000 bytes), held in about 196 KB of free AXI SRAM
([0011](0011-halloween-2026-demo-build.md) item 15). Granular reads grains from random
positions, so the clip must sit in RAM. Radio plus microphone at the same rate and
length is 288,000 bytes, which does not fit there.

A link-map check on 2026-10-06 measured what is free:

| Image | Region | Size | Used |
|---|---|---|---|
| `Release`, main tree `1ff22e9` | AXI `RAM_DMA` | 524,288 B | 327,680 B |
| `Demo`, p04.7 branch `4f5935a` | AXI `RAM_DMA` | 524,288 B | 471,680 B, including the 144,000 B clip |
| `Demo`, M7 | DTCM | 131,072 B | 89,728 B |
| Both, M7 | ITCM | 65,536 B | 0 B |
| Both, M4 | D2 SRAM1 to SRAM3 (linker `RAM`) | 294,912 B | about 1.6 KB: data, BSS and minimum heap and stack |
| Both | D3 SRAM4 | 65,536 B | 256 B IPC block |

Clips can have at most 196,608 bytes of AXI. Mono 16-bit lengths in that space:

| Source layout | 24 kHz | 16 kHz |
|---|---|---|
| One source | 4.10 s | 6.14 s |
| Two sources | 2.05 s | 3.07 s |

D2 SRAM is almost unused, but the M4's linker region claims all of it. Taking part of it
for the M7 is a build-time ownership transfer (Principle III), and 0010's Context declines
it because the M4 will own input and rendering.

How Granular works now (p04.7): each grain is a short window read from the clip at the
position plus random spray, played at the pitch rate under a trapezoid envelope, with up
to 16 grains overlapping. The render runs in the radio interrupt with a 1.5 ms budget.

## Options

For Granular's source control:

1. **Blend:** every grain reads radio and microphone at the same position and mixes them.
   Doubles the read work in the interrupt. A fixed blend sounds the same as a pre-mixed
   clip; it becomes interesting only when moved by a macro, modulation or the sequencer.
2. **Probability (Scatter):** each new grain picks one source. Same cost as now, and it
   makes a sound a pre-mix cannot: two materials scattered through one cloud.
3. **Proposed: both, as one control with a style setting** (item 4).

## Decision

1. A clip carries two sources: radio (left and right summed to mono) and microphone.
   *(agent)*
2. Source mixing is an engine-level control set. It never changes the stored recording.
   *(user 2026-10-05)*
3. Slicer and One-shot provide an independent level per source. Slicer adds optional
   per-slice overrides. *(user 2026-10-05)*
4. Granular provides a **Source** control (all radio to all microphone) and a style
   setting: **Scatter** (each grain picks a source with that probability) or **Blend**
   (each grain mixes both). Scatter is the default. *(agent)*
5. In Scatter, each source may be panned to its own side of the stereo output.
   *(agent, candidate)*
6. Maximum clip length is set from measured free memory per source layout, not chosen
   for UX. *(agent)*
7. Whether product clips may use D2 SRAM is decided at M4 bring-up
   (`full_spooky_proto-54w.5`), once the M4's memory budget is known. Until then the
   product budget is AXI only. *(user 2026-10-06)*

## Open questions

- Within AXI, which lever for two-source clips: shorter clips, a lower sample rate (for
  example 16 kHz), a compressed sample format, or microphone only when chosen at load
  time?
- Is the source layout chosen at load time (radio, microphone or both), so a one-source
  clip gets the full length?
- Does Blend fit the interrupt budget? It needs a measurement before it is offered.
- Is PTT-aware muting (Field Playback) applied when a clip is cut from a session?
- Does the radio need to stay stereo for any engine, or is the mono sum enough?

## Consequences

- The demo stays mono radio ([0020](0020-halloween-demo-slicer-and-sequencers.md)
  item 15). [0025](0025-demo-clip-in-d2-sram.md) moves the demo clip to D2 SRAM in the
  `Demo` image pair only; it does not settle item 7.
- Clip preparation (decimation, summing) belongs to the shared clip infrastructure
  ([0021](0021-instrument-engines-and-performance-views.md) item 5).

## Evidence

- Free AXI SRAM: [0010](0010-sd-backed-rolling-capture.md) and 0011 item 15.
- Link-map measurements: `full_spooky_proto-v7l.11` notes, 2026-10-06.
