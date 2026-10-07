# 0025. Demo clip in D2 SRAM, up to 5 s

- **Status:** Accepted 2026-10-06
- **Date:** 2026-10-06
- **Supersedes:** [0011](0011-halloween-2026-demo-build.md) item 15 in part (the clip's
  3 s limit and its place in AXI SRAM) and item 17 in part (D2 SRAM1 and SRAM2 move to
  the M7 in the `Demo` image pair). The rest of 0011 stands.
- **Beads:** `full_spooky_proto-p04.17` (decision), `full_spooky_proto-p04.18`
  (demo task); product follow-up `full_spooky_proto-v7l.13`

## Context

0011 item 15 holds the demo clip (mono radio, 24 kHz, 16-bit) in free AXI SRAM and caps it
at 3 s (144,000 bytes). The link-map check for
[0024](0024-clip-sources-and-track-mixing.md) on 2026-10-06 measured the images:

| Image | Region | Size | Used |
|---|---|---|---|
| `Demo`, p04.7 branch `4f5935a`, M7 | AXI `RAM_DMA` | 524,288 B | 471,680 B, including the 144,000 B clip |
| `Demo`, M7 | DTCM | 131,072 B | 89,728 B |
| `Demo`, M7 | ITCM | 65,536 B | 0 B |
| `Demo`, M4 | D2 SRAM1 to SRAM3 (linker `RAM`, 0x10000000) | 294,912 B | 1,584 B: data, BSS and minimum heap and stack |
| Both | D3 SRAM4 | 65,536 B | 256 B IPC block |

AXI can give a clip at most 196,608 bytes, enough for 4.1 s. In the `Demo` image the M4
sleeps (0011 item 12), yet its linker region claims all of D2 SRAM. The M7 places nothing
there.

Principle III gives every hardware resource one owner per image pair and allows a change
only as a build-time transfer backed by a decision and a qualifying test. 0011 item 17
says ownership does not change in the demo. 0010's Context declines to take the M4's
memory because the M4 will own input and rendering. That reasoning concerns the product
images and is not settled here (item 6).

## Options

1. **Keep the 3 s clip in AXI.** No change. Rejected by the user in favor of a longer clip.
2. **Longer clip in AXI only.** At most 4.1 s, and it leaves AXI with almost no free space
   for later demo work. Rejected.
3. **Chosen: move the clip to D2 SRAM in the `Demo` image pair only.** Frees 144,000 bytes
   of AXI and gives up to 5.46 s. The M4 keeps SRAM3, about 20 times its current use.
4. **Two-source demo clip in D2.** Rejected by the user: it needs microphone capture into
   the clip and Granular source controls before the 2026-10-21 freeze.

## Decision

1. In the `Demo` image pair, the M4's linker `RAM` region shrinks to D2 SRAM3 (32 KiB) and
   the M7 owns D2 SRAM1 and SRAM2 (256 KiB at 0x30000000). The `Debug`, `Release`,
   `IpcSmoke` and `IpcMismatch` images keep the current layout.
2. The demo clip moves from AXI to D2 SRAM. It stays mono radio, 24 kHz, 16-bit, ending at
   the save point (0011 item 15, 0020 item 15).
3. The clip is at most 5 s (240,000 bytes).
4. Only the M7 CPU reads or writes the clip. No DMA touches it, so it needs no cache
   maintenance. Nothing in D2 is shared with the M4.
5. The qualifying test for the transfer:
   - linker assertions in both `Demo` scripts that the M4 region and the M7 D2 region do
     not overlap and that the clip fits;
   - a `Demo` map check recorded in the task notes;
   - a bench run on the `Demo` image: the M4 still starts and sleeps, and a 5 s clip loads
     and plays through Granular, with the load time recorded. That run is demo-image
     evidence only (roadmap demo-track rule 4).
6. The product memory layout for clips stays open. It is decided when the M4's memory
   budget is known at M4 bring-up (`full_spooky_proto-54w.5`). This record does not
   settle it (demo-track rule 5).

**Schedule.** The work starts after p04.7 runs on hardware (0020 item 16). It does not
change the checkpoint 2 gate.

**Principles.** Principle III holds: the transfer is build-time, limited to the `Demo`
image pair, and backed by this record and the test in item 5. Principle I holds: the clip
loads outside the audio callbacks, as now. Principles II, IV, V and VI apply as in 0011
items 17 to 19. The rejected alternative that keeps 0011 item 17 whole is option 1.

## Consequences

- p04.6's `clip_decimator.h` raises `CLIP_MAX_SECONDS` to 5 and its static assertion to
  240,000 bytes; its host tests follow. `demo_clip.c` places the clip in the new D2
  section instead of `.dma_buffer`.
- A clip load reads up to 5 s of rolling capture from SD instead of 3 s, so it takes
  about 5/3 as long. The bench run measures it.
- AXI regains 144,000 bytes for other demo work.
- The `Demo` preset needs its own pair of linker scripts. These stay on the demo branch.
- When the demo track ends, this layout ends with it.

## Evidence

- Link-map measurements: `full_spooky_proto-v7l.11` notes, 2026-10-06.
