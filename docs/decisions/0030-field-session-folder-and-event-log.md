# 0030. Field session folder, manifest and event log

- **Status:** Accepted 2026-10-09
- **Date:** 2026-10-09
- **Supersedes:** none
- **Beads:** `full_spooky_proto-hpq.3`; unblocks `full_spooky_proto-54w.6` (Q5) and
  `full_spooky_proto-54w.12`; asset IDs, recovery and the Reviewing question stay with
  `full_spooky_proto-hpq.8`; product proposal `full_spooky_proto-hpq.9`

## Context

Today a recording is one 3-channel WAV (`REC###.WAV`) in the card's root. Nothing
else from the session is stored. Several accepted decisions and specs need events
stored with the session, on the sample timeline:

- [0015](0015-raw-radio-track-during-in-band-tunes.md) items 2 to 5: tune start, tune
  end, and the per-band settle margin that completes a retune interval. Storing them
  is the last gate (Q5) before the in-band tune guard lifts on `main` (`54w.6`).
- [0004](0004-radio-track-continuity-across-transitions.md): band-switch gap start
  and end (`54w.12`).
- [0028](0028-monitor-only-ptt-without-mic-monitoring.md) item 4: PTT press and
  release.
- Spec 001 FR-029: every change to Classic's run state, direction (including bounce
  reversals) and parameters.
- [0012](0012-common-audio-sample-timeline.md): the timeline. A stamp is a frame
  position plus a declared uncertainty, clipped at a session origin, and gap edges
  are exact (items 5 and 6). A capture never spans an epoch change (item 7).
- [0017](0017-microphone-start-latency-uncompensated.md): the applied microphone
  compensation *C* is 0, and the absolute radio-to-microphone offset is unmeasured.
- [0008](0008-recording-safe-command-policy.md) item 4: with session folders, a
  session continues into the next file at the 4 GiB WAV limit without a gap.
- [0010](0010-sd-backed-rolling-capture.md) items 2, 3 and 6: rolling capture's
  event sidecar covers the same frames as the audio. Its storage bound feeds the
  card-full reserve. `hpq.3` defines names and manifests.

Product intent (modes and interaction, Recording Across Modes) asks for:
- separate directory trees for Field and Instrument sessions;
- one folder per session;
- a manifest naming the session type, start time, duration, firmware and format
  versions, stream files, and any incomplete or recovered state.

Open product decision 14 asks which formats store EMF, semantic events, and
sequencer or MIDI data. Decision 0023 item 21 adds a per-track peak stream for the
clip editor.

The roadmap (M3 since 2026-10-09) scopes this record to the session format and its
event log. Asset IDs, incomplete and recovered state, and recovery rules belong to
`hpq.8` (M4).

Constraints from the tree:

- **Names:** FatFs is built without long file names (`_USE_LFN 0`), so every name is
  8.3.
- **No wall clock:** `_FS_NORTC 1`, and the RTC only holds backup registers. A boot
  counter exists (`DIAG IDENTITY BOOT=`).
- **Storage:** storage is foreground-only and runs below audio draining (0010 item
  7). The radio and microphone queues hold about 683 ms.
- **Tooling:** Spooky Bench's WAV inspection and the recording regression read
  `REC###.WAV` from the root.

The event rate today, per producer:

| Producer | Records per second, worst case |
| --- | --- |
| Classic at its fastest rate, 400 jumps a minute (two tune records per jump) | 13.3 |
| Bounce reversals (FR-029), at most one per jump | 6.7 |
| Activity holds: two records each, and each hold stops jumps for at least 1 s | 2 |
| PTT and parameter changes from the controls | human rate, in bursts |

The engine sustains at most about 22 records a second.

## Options

1. **Text event lines** (like the UART `[retune]` and `[classic]` lines) in the
   session folder. Readable on any PC, but records vary in length and are about
   three times larger. A rolling ring or a time range can't be cut or searched by
   offset, and each record needs `printf` in the foreground. Rejected.
2. **Events inside the WAV** (`LIST` or `cue` chunks). The WAV is written
   progressively and split at 4 GiB. Its chunks would have to wait in RAM until
   finalization, or the file would have to be rewritten. Rejected.
3. **Chosen: fixed-size binary event records** in their own file, plus a small text
   manifest, in one folder per session. The 3-channel WAV stays as it is. A
   human-readable export can come later as a host utility.

## Decision

**Folders and names**

1. **Every recording is a session folder.** Field sessions live under `/FIELD`, as
   `/FIELD/Fnnnn`. Rolling captures (`hpq.5`) will use `/CAPTURE/Cnnnn`, and
   Instrument sessions `/INSTR/Innnn`; those two trees are reserved here and
   defined by their own work.
   - `nnnn` is a decimal sequence number, not reused while the folder exists.
   - The next number comes from stored state, not a directory scan, so preparing a
     recording stays within the foreground budget (roadmap M4 storage headroom,
     `jjy.18`).
   - CLI `RECORD START` and the Button 0 prompt make the same kind of folder.
   - Existing root `REC###.WAV` files stay readable. New recordings no longer
     create them.
2. **Files in a Field session folder:**
   - `MANIFEST.TXT` describes the session (items 4 to 6).
   - `AUDIO000.WAV`, `AUDIO001.WAV` and so on, up to `AUDIO999.WAV`, hold the audio:
     48 kHz, 16-bit, 3 channels (radio left, radio right, microphone), exactly today's
     format.
     - Every part except the last holds exactly `part_frames` frames. That figure is
       a whole number of recorder blocks, under the 4 GiB WAV limit and configurable,
       so a test build can split small parts.
     - The next part starts with the next frame, so the parts are one contiguous frame
       sequence.
     - Each part is a complete WAV by itself.
   - `EVENTS.BIN` is the event log (items 7 to 15).
   - The EMF, activity and peak streams get their own files and manifest keys,
     reserved here (`emf`, `activity`, `peaks`). Their formats are decided by later
     records, and readers ignore keys and files they don't know.

**Manifest**

3. `MANIFEST.TXT` is ASCII `key=value` lines with CRLF line endings, at most 1,024
   bytes (two sectors), written with one write call. Readers ignore unknown keys.
   - Every key in format version 1 has a bounded value; the worst case is about 640
     bytes.
   - Whether a rewrite interrupted between the two sectors needs more protection is a
     recovery question for `hpq.8`.
4. **Keys in format version 1:**

   | Key | Meaning |
   | --- | --- |
   | `format` | `spooky-session` |
   | `format_version` | `1` |
   | `type` | `field` |
   | `state` | `recording` or `ended` |
   | `end_reason` | the recorder's reason, for example `stopped` or `card full`; or `none` |
   | `build` | the firmware build ID |
   | `boot` | the boot counter at start |
   | `start_uptime_ms` | the uptime at start |
   | `start_time` | `unknown` |
   | `sample_rate_hz` | `48000` |
   | `channels` | `radio-L,radio-R,mic` |
   | `frames` | the total frames when ended |
   | `audio_parts` | the number of `AUDIOnnn.WAV` parts |
   | `part_frames` | the frames in each full part |
   | `events` | `EVENTS.BIN` |
   | `event_format_version` | `1` |
   | `events_written` | the records written |
   | `events_lost` | the records dropped |
   | `retune_settle_frames` | per band, for example `FM:0,AM:0,SW:512,LW:512` |
   | `origin` | the timeline origin as `epoch:frame` (diagnostic) |
   | `mic_compensation_frames` | the applied *C* of decision 0012 item 4, which is 0 per decision 0017; the absolute offset is unmeasured |

   `retune_settle_frames` holds the settle margins the firmware applied (decision
   0015 item 5). A reader forms each retune interval as tune start to tune end plus
   the margin for the tune's band.
5. **Written twice.** The manifest is written when the session is prepared, with
   `state=recording`, and rewritten once the audio is finalized, with
   `state=ended` and the totals. A folder whose manifest still says `recording` is
   incomplete. How it is recovered belongs to `hpq.8`.
6. **No wall clock.** `start_time` stays `unknown` because the device has none.
   `boot` and `start_uptime_ms` order sessions within a boot, and the folder number
   orders them across boots. A host may add a wall time when it imports a session,
   without changing the session's files. RTC-derived timestamps can be tried later.

**Event log**

7. **Header.** `EVENTS.BIN` starts with one 32-byte header: a magic value, the
   event format version, the record size and the sample rate. A reader rejects a
   magic or major version it does not know.
8. **Records.** Every event is one 32-byte little-endian record:
   - a 64-bit frame position on the session timeline (frame 0 is the origin, so
     WAV frame *n* is event frame *n*);
   - a 32-bit uncertainty in frames, meaning the event occurred no earlier than the
     frame minus the uncertainty (0012 item 5);
   - a 16-bit kind;
   - 16 bits of flags;
   - a 32-bit sequence number that counts every record produced, including dropped
     ones;
   - a 12-byte payload whose meaning depends on the kind.

   The byte layouts, flag bits, payload encodings and worked examples are in
   [session format](../design/session-format.md), which changes only with a format
   version.
9. **Kinds in version 1:**

   | Kind | Stamp | Payload |
   | --- | --- | --- |
   | Session start | exact, frame 0 | band, frequency |
   | Session end | exact, the last frame | end reason |
   | Tune start (0015) | taken before the tune is issued; uncertainty 0 | band, target |
   | Tune end (0015) | foreground observation; uncertainty 3,600 | band, outcome (tuned, failed, abandoned), frequency, RSSI, SNR |
   | Gap start and gap end (0004) | exact | cause (band switch), band; gap end adds frequency |
   | PTT on and off (0028) | foreground observation; uncertainty 3,600 | none |
   | Classic (FR-029) | foreground observation | the published event and the run state, reason, direction, rate, distance, edge and hold |
   | Events lost | when writing resumes | the number of records dropped |

   - A tune start's stamp precedes the command write by at most the receiver-ready
     wait (5 ms), so the receiver cannot change before it.
   - A Classic record with event `snapshot` holds the engine's whole state.
   - Kinds from `0x8000` up are reserved for opt-in experiments. Readers skip kinds
     they do not know.

**Reader rules**

10. **Range.** The log covers frames 0 to the last frame of the audio, inclusive.
    - A session start record opens it and a session end record closes it.
    - An event the firmware observes before the origin is not in the log. Its effect
      is carried by the state at frame 0 (item 11).
    - An event observed after the last frame is not written.
    - An uncertainty that would reach before frame 0 is clipped there, and the
      record is flagged as clipped.
11. **State at frame 0.** Right after session start, at frame 0, the log holds a
    Classic snapshot, and a PTT record if PTT is held.
    - A tune in flight at the origin gets a tune start at frame 0, flagged as having
      begun before the origin.
    - A gap open at the origin gets a gap start at frame 0, flagged the same way.
12. **Order.** Records appear in sequence order, which is the order they were
    produced. Frames are not guaranteed to be ascending, because exact gap edges and
    observed stamps are produced at different times. A reader that needs time order
    sorts by frame, and by sequence within a frame.
13. **Loss and resynchronization.** A gap in sequence numbers means records were
    dropped. After a loss, the writer emits:
    - an events-lost record with the count;
    - a Classic snapshot and the current PTT state;
    - a tune start, if a tune is then in flight.

    A reader treats state between the last record before the loss and that snapshot
    as unknown.

**Writing**

14. **Queue.** Producers post records to a bounded RAM queue of 64 records (2 KiB).
    - If it is full, the newest record is dropped and counted (Principle IV).
    - The storage service writes whole 512-byte sectors (16 records) below audio
      draining, and writes the partial sector at finalization.
    - A record waits in RAM at most 2 s, a configurable bound. When the oldest has
      waited that long, the storage service writes the partial sector and syncs the
      file, below audio draining. So sparse events reach the card even when a sector
      never fills.
15. **Write failures.** A failed event write or a lost record does not stop an
    otherwise healthy session, as 0010 item 10 already rules for rolling metadata.
    It is visible instead:
    - in `events_lost` and the events-lost record;
    - in `RECORD RESULT` and `RECORD DIAG`;
    - as a session warning on the device.

    The session never presents itself as having a complete event log when it does
    not.
16. **Rolling captures** (`hpq.5`) use the same record format for their event
    sidecar (0010 item 2, 0015 item 9).
17. **Sizing.** The design bound is 32 records a second sustained: the engine's 22
    plus margin for control bursts. That is 1,024 bytes a second, about 3.7 MB an
    hour, and 61,568 bytes (1,923 records and the header) for one 60 s rolling
    window. That bound
    goes into decision 0010 item 3's reserve.
    - The 64-record queue holds 2 s at the bound, longer than the 683 ms audio queues
      that already limit an SD stall.
    - A new producer must state its rate, and one that would exceed the bound needs a
      new sizing.

**Tooling**

18. Spooky Bench gets a session dump command and a session transfer by folder. The
    WAV checks keep reading `AUDIOnnn.WAV` unchanged, and `wav retune` reads the
    tune events from `EVENTS.BIN` instead of the UART log. This is inline tooling for
    the implementing tasks (roadmap tooling rule 6). A human-readable export utility
    can follow later.

**Implementation and qualification**

19. **First task (M3):** the session folder, the manifest and audio parts, with part
    continuation tested with small parts. It also includes the WAV and tooling path
    change. The M1 recording regression passes on it.
20. **Second task (M3):** the event log writer and its queue, the producers for
    session, tune, PTT and Classic, and the host parser. It is qualified by 0015
    item 10's FM and AM run, repeated with the events read from `EVENTS.BIN` instead
    of the UART log. That run must show no records lost at Classic's fastest rate,
    with bounce edges, and each event's sequence and frame must match the UART log.
    Passing it answers Q5 for `54w.6`.
21. **`54w.12`** adds the gap producers when it implements decision 0004.

## Consequences

- Recordings move from the card's root into `/FIELD`. Bench procedures, the recording
  regression and WAV inspection change with the first task.
- Open product decision 14 becomes partly resolved: semantic events are settled here,
  while EMF, activity, and sequencer or MIDI data stay open. The proposal to
  `spec/product/modes-and-interaction.md` is `hpq.9`.
- The demo branch keeps its own rolling-capture descriptor until it merges `main`;
  demo tasks are not changed by this record.
- Revisit this record in any of these cases:
  - a producer's rate breaks item 17's bound;
  - 32-byte records cannot hold a needed payload, which needs a new format version;
  - `hpq.8` defines recovery and asset IDs.

## Evidence

- [Retune qualification](../evidence/2026-10-09-retune-qualification.md) for the tune
  event stamps, rates and settle margins.
- [Monitor-only PTT](../evidence/2026-10-08-monitor-only-ptt.md) for PTT stamps.
