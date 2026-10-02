# 0014. SD media policy for recording

- **Status:** Proposed
- **Date:** 2026-10-01
- **Supersedes:** none
- **Beads:** `full_spooky_proto-jjy.8` (this decision); `full_spooky_proto-jjy.14`
  (in-device format, done); `full_spooky_proto-jjy.9` (allocation latency);
  `full_spooky_proto-jjy.13` (DMA buffer layout); `full_spooky_proto-jjy.15`
  (unresponsive-card mount wait)

## Context

The recorder writes 288,000 bytes/s in 4096-frame blocks, one every 85.33 ms. Each
`f_write` blocks the M7 foreground loop while the audio DMA interrupts keep filling two
eight-block queues. Eight blocks are about 683 ms: one write stall longer than that
overruns a queue, which aborts the recording visibly and finalizes the partial file
(Principle I).

Bench results so far (see Evidence):

| Card | Condition | Max write | Queue high-water |
| --- | --- | --- | --- |
| 64 GB SDHC/SDXC reference (`SD64G`, Phison OEM, 2025) | 60 s recordings, many sessions | 15-54 ms | 1/8 |
| 16 GB SDHC, used (SanDisk `SC16G`, 2022), formatted in the device | 60 s recording | 46 ms | 1/8 |
| 16 GB SDHC, same card | 60 s recording straight after a 64 MiB stress pass | 66 ms | 1/8 |
| 16 GB SDHC, same card | 10 s recording, later session | 129 ms | 2/8 |
| 2 GB SDSC (SanDisk `SU02G`, 2010) | 60 s recording | 607 ms | 7/8 |
| 2 GB SDSC | First write after an interrupted stress pass | 780-810 ms | overrun: `REC001`, `REC002` aborted |
| 8 GB SDHC | Never reaches the transfer state | - | unusable |

The bad stalls on the SDSC card happened inside contiguously preallocated space, so
they come from the card's internal housekeeping, not from file layout. Card ratings do
not bound them: speed classes (C10, U1, U3, V30) guarantee sustained throughput, of
which the recorder needs a small fraction, and application classes (A1, A2) rate
random operations per second. None caps a single write stall.

The media available for qualification is limited: several identical 64 GB reference
cards, one used 16 GB SDHC card, one unusable 8 GB SDHC card and several 1-2 GB SDSC
cards.

## Options

- **Engineer around slow cards now** (deeper queues sized to survive the 810 ms SDSC
  event): rejected for M1. It sizes the buffers to one bad card rather than to a
  deliberate figure, and it spends DMA RAM that the rolling capture (decision 0010) and
  `jjy.13` may need.
- **Gate on card ratings** (require C10, U1 or A1): rejected as a gate, since ratings do
  not bound write stalls. Ratings are reported for evidence instead.
- **Test write latency at mount or before recording:** rejected. A short probe does not
  trigger the housekeeping stalls, wears the card and delays the start.
- **Nonblocking storage now** (asynchronous or DMA-driven writes so a stall only drains
  the queues): deferred; it is a large storage-service change outside M1.
- **Supported card class plus evidence** (this decision).

## Decision

For M1:

1. **Supported media are SDHC and SDXC cards.** `RECORD START` refuses an SDSC or
   unrecognized card with an explicit error before creating a file. Reading and
   maintenance (`SD STATUS`, `SD INFO`, `WAV FETCH`, `SD STRESS`, `SD CLEAN`) remain
   available on such cards, so files can be recovered and the card can serve as a
   negative qualification case. `SD FORMAT` refuses SDSC (`jjy.14`).
2. **In-device formatting is provided and recommended, not required.** Cards formatted
   elsewhere stay usable. `SD FORMAT` writes one FAT32 volume with 32 KiB clusters and
   the data area aligned to the card's allocation unit.
3. **Overrun behavior is unchanged.** A queue overrun aborts the recording visibly and
   finalizes the valid partial file.
4. **Media evidence.** Every recording qualification records the card's identity and
   ratings (`SD INFO`), maximum write, write histogram and queue high-water. These are
   the inputs for a later media contract: the maximum write stall a supported card may
   have, plus a stated margin.
5. **Storage margin warning** (proposed here, not yet implemented): the recorder reports
   a low storage margin when queue high-water or a single write passes a configurable
   threshold, initially half the queue headroom (4/8 blocks or 341 ms). The warning
   appears in `RECORD DIAG` and as a diagnostic event, and does not stop the recording.
   The threshold stays configurable until the media survey fixes it (Principle IV).

After M1, in order:

6. Reduce allocation latency with contiguous or stepped preallocation extents, together
   with the nearly-full-card search fix (`jjy.9`). This addresses fragmented and nearly
   full cards, not the card-internal stalls above.
7. Deepen the recorder queues to a deliberate headroom figure derived from the media
   contract, once the rolling-capture design (`hpq.2`) and the DMA buffer layout
   (`jjy.13`) are known.
8. Move storage behind a nonblocking execution boundary so a pathological card cannot
   freeze application services. Aggregate buffering must still cover the contract's
   maximum stall.

## Consequences

- SDSC cards cannot record. Existing recordings on them can still be read and
  transferred.
- Until a media contract exists, the documented recording guarantee is the queue
  headroom (about 683 ms) on the cards actually qualified; other SDHC/SDXC cards are
  untested, not supported by evidence.
- The qualified set is narrow because of the cards at hand. Buying two or three cards
  of different brands and grades would widen it cheaply; until then, the identical
  reference cards give unit-to-unit variation and a PC-format versus device-format
  comparison.
- Revisit if a supported card overruns in normal use, if the survey shows SDHC/SDXC
  cards with stalls near the headroom, or when item 7 or 8 starts.

## Evidence

- [SD failure and recovery](../evidence/2026-10-01-sd-failure-recovery.md): SDSC stalls
  and overruns, removal and card-full behavior.
- [In-device SD format](../evidence/2026-10-01-sd-format.md): 16 GB card, SDSC
  refusal, unresponsive 8 GB card.
- [SD media identification](../evidence/2026-10-01-sd-media-identity.md): `SD INFO`
  for all three working cards, SDSC recording refusal, the 16 GB card's 129 ms write.
- [Recorder write latency](../evidence/2026-09-30-recorder-write-latency.md) and the
  other 2026-09-30 and 2026-10-01 recording evidence for the 64 GB reference card.
