# 0010. Sixty-second SD-backed rolling capture

- **Status:** Accepted 2026-09-29
- **Date:** 2026-09-29
- **Supersedes:** none
- **Beads:** `full_spooky_proto-hpq.2` (this decision),
  `full_spooky_proto-hpq.5` (implementation and qualification),
  `full_spooky_proto-hpq.3` (persistent asset and reference format)

## Context

Field Mode must retain synchronized radio, microphone, EMF and semantic history so
an event can be saved after it happens. The product intent deliberately leaves the
duration and storage location to an implementation decision. The proven audio format
is 48,000 frames/s, three signed 16-bit channels, or 288,000 bytes/s before event and
EMF sidecars.

The current IpcSmoke CM7 link map at clean revision
`c6d333dfbdc33717275ff441607839d1820c054a` declares 524,288 bytes of AXI
`RAM_DMA`. `.dma_buffer` occupies 327,680 bytes: 8,192 bytes in
`audio_path_service.c` and 319,488 bytes in `radio_recorder.c`. The remaining
196,608 bytes represent only 0.683 seconds of audio if nothing else uses them.
M7 DTCM is 128 KiB and already contains data, BSS, heap and stack. M4 has a separate
288 KiB linker region with future input/render ownership. No external RAM is
configured in the maintained linkers or hardware services. Taking either core's
remaining internal memory cannot produce a useful multi-second window and would
weaken the current recorder or the planned M4 ownership boundary.

The recorder already writes one 4,096-frame, 24,576-byte block every 85.333 ms. The
ten-minute IpcSmoke baseline wrote 172,818,432 audio bytes in 600.064 seconds with a
maximum observed block write of 53 ms, queue high-water 1/8 for both sources, and no
overrun or SD/audio error. For that observed worst block, the implied service rate is
463,698 bytes/s, 1.61 times the 288,000-byte/s source rate, with 32.333 ms left in the
block period. This is evidence for the current card and workload, not a universal SD
guarantee. A design that writes a second audio copy on save would discard this margin
and is not qualified.

Constraints:

- **Principle I:** a rolling save during a formal session may never delay or replace
  the session write. A card or rolling-store failure is visible and does not turn a
  failed capture into a successful one.
- **Principle III:** M7 remains the only owner of audio, storage, session state and
  rolling-capture meaning. M4 may publish input and render semantic state; it does not
  own rolling audio or FatFs objects.
- **Principle IV:** queues and request slots are bounded, writes remain in foreground
  service code, and their high-water, overrun and latency counters are observable.
- **Principle V:** the link map and existing SD run support a design choice. Segment
  rotation, metadata commit, removal, full-card and concurrent-session behavior still
  require host tests and hardware evidence before the capability is delivered.
- **Principle VI:** the existing recorder queues, DMA placement, callback cadence and
  single storage lease remain the baseline during implementation.
- **Principle VII:** the window deepens the Field instrument and does not introduce a
  general file-management surface.

## Options

1. **Put the full window in AXI SRAM.** Rejected. The 196,608 free bytes cover only
   0.683 seconds, and using all of them leaves no growth margin. Five seconds would
   need 1,440,000 bytes.
2. **Assemble internal RAM from the M7 and M4 banks.** Rejected. The capacity is still
   too small for a useful multi-second window, some banks are not DMA-accessible, and
   borrowing the M4 region conflicts with explicit future ownership.
3. **Add external RAM now.** Deferred. No external RAM device, pin ownership, driver,
   cache policy or bench evidence exists in the prototype baseline. Adding hardware
   solely for the window is not the narrowest M4 slice.
4. **Continuously write a ring file, then copy the previous minute when Save is
   pressed.** Rejected. The copy adds read/write contention at the exact moment a
   session may be recording and requires a second window or overwrite race.
5. **Write immutable storage blocks once, keep a logical rolling view over the newest
   blocks, and save by pinning references rather than copying audio.** Chosen. The
   storage format may group logical blocks into segment files, but ownership and
   snapshot boundaries remain block-addressable.

## Decision

**Window and arithmetic**

1. The configured rolling window is nominally 60 seconds. At the current format its
   audio portion is 704 recorder blocks: 2,883,584 frames, 17,301,504 bytes and
   60.074667 seconds. The implementation retains at least those 704 most recent
   matched radio/microphone blocks. It does not round down to 60 seconds.
2. EMF samples, activity values and semantic events use the same monotonic sample
   timeline and cover the same first and last sample indices. Their storage upper
   bounds are configuration inputs derived from their producer rates, not a reason to
   shorten the audio window silently.
3. The card-full contribution is computed as 17,301,504 audio bytes plus the bounded
   sidecar and transaction allocation selected by `hpq.3`. The implementation sets
   `SPOOKY_ROLLING_CAPTURE_RESERVE_BYTES` from that checked sum. It must not use the
   current zero placeholder or count unbounded filesystem free space as a buffer.

**One write stream and ownership**

4. M7 owns one rolling/session storage stream under the recorder's exclusive storage
   lease. When no formal session is active, matched blocks append to the rolling
   journal. During a formal session, the session's block stream is also the rolling
   source; audio is not written once to the session and again to a separate ring.
5. A logical rolling block is immutable after its successful write. The rolling view
   owns one reference to each of the newest 704 blocks. A session or saved capture may
   own additional references. Reclamation may remove a block only after every owner
   has released it.
6. Save resolves at the next matched-block boundary. It closes the window at that
   block, pins the audio blocks and aligned sidecar range, starts the next rolling
   block without a gap, and commits a small capture descriptor transaction. Only the
   successful descriptor commit changes the semantic result from `writing` to
   `saved`. `hpq.3` defines filenames, manifests, recovery and reference persistence.
7. The storage service remains foreground-only. ISR and DMA callbacks continue to
   enqueue into the existing bounded source queues and never call FatFs. Segment
   rotation, sync and descriptor work run below audio draining and expose maximum
   latency and failure counters.

**Save admission and exhaustion**

8. There is one save-commit request slot. A second Save while that slot is occupied is
   rejected immediately as busy with visible feedback; it is not queued without a
   bound and does not replace the first request. After commit, another Save is
   allowed, including an overlapping window, because references may share immutable
   blocks.
9. Outside a session, unreferenced blocks older than the newest 704 are reclaimed.
   Pinned capture and session blocks are never reclaimed as rolling scratch. If the
   store cannot allocate the next block, rolling capture enters a visible unavailable
   or fault state; the UI must not claim that the previous minute is current.
10. During a formal session, the session write has priority. Decision 0008's reserve
    keeps space for one more rolling save. When the remaining budget reaches the
    one-minute session margin plus the computed rolling and finalization reserves, the
    session takes the defined `card full` path. A rolling metadata failure reports the
    capture failure but does not abort an otherwise healthy session.
11. Card absence, removal, write failure, reference-catalog corruption and recovery
    uncertainty are explicit rolling faults. The last descriptor known to be committed
    remains a saved capture; an uncommitted request is incomplete and is never shown as
    saved.

**Qualification gates**

12. Host tests cover 704-block wrap, aligned sidecars, overlapping pins, release and
    reclamation, second-save busy behavior, transaction interruption, reference-count
    exhaustion, and priority of a session owner over rolling maintenance.
13. Hardware qualification measures segment rotation and descriptor-commit latency,
    source-queue high-water, SD maximum write time and every overrun/error counter for:
    rolling alone; a Save; a formal session that saves; repeated saves after each
    prior commit; card-full approach; and removal/failure recovery. The current
    53 ms observation is the starting benchmark, not the acceptance ceiling.
14. The 60-second duration is revisited only if those tests cannot retain the window
    without recorder regression, or later measured user trials justify a different
    instrument window. A change updates the configured block count and reserve from
    explicit arithmetic; it does not silently truncate under load.

## Consequences

- `hpq.5` implements a logical block catalog, rolling references, the single
  save-commit slot, counters and the non-copying storage path behind narrow recorder
  and storage interfaces.
- `hpq.3` chooses the persistent segment, descriptor and reference format. It must
  support byte/block ranges shared by a session and one or more captures, committed
  versus incomplete state, and recovery without deleting referenced raw audio.
- The rolling window requires a healthy SD card. This prototype does not claim a
  minute of pre-event audio when storage is absent or faulted.
- The steady audio write rate remains 288,000 bytes/s regardless of window duration.
  Longer retention consumes card capacity, not firmware RAM or additional steady
  bandwidth.
- A Save can be secured quickly because it commits references, while later packaging
  or playback indexing may continue in bounded background work. UI wording follows
  the actual `writing`, `saved`, `busy`, `unavailable` and `failed` outcomes.
- The proposed duration answers product open decision 8 only after this record is
  accepted and an exact protected-document amendment is separately approved.

## Evidence

- [Ten-minute recording baseline](../evidence/2026-09-26-ten-minute-recording-baseline.md)
- [Repository review rolling-buffer budget](../history/2026-09-23-repository-review.md#rolling-buffer-budget)
- Current CM7 IpcSmoke link map:
  `build/IpcSmoke/firmware/CM7/full_spooky_proto_CM7.map` (generated, not tracked)
- Host tests and new bench evidence are pending `full_spooky_proto-hpq.5`.
