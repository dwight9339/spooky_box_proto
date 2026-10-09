# Session format

Byte layouts for a Field session folder. The decision is
[0030](../decisions/0030-field-session-folder-and-event-log.md); this document holds
the encodings it refers to. It changes only together with a format version.

**Status:** target. No firmware writes this format yet (`full_spooky_proto-hpq.3`).
Recordings today are root `REC###.WAV` files.

## Folder

```text
/FIELD/F0007/
  MANIFEST.TXT
  AUDIO000.WAV      every part except the last holds part_frames frames
  AUDIO001.WAV
  EVENTS.BIN
```

All names are 8.3. Rolling captures (`/CAPTURE/Cnnnn`) and Instrument sessions
(`/INSTR/Innnn`) are reserved and defined elsewhere.

Each `AUDIOnnn.WAV` is today's recorder format: RIFF/WAVE, PCM, 3 channels (radio
left, radio right, microphone), 48,000 Hz, 16-bit, 288,000 bytes a second.

## Manifest

ASCII `key=value` lines with CRLF endings, at most 1,024 bytes (two sectors),
unknown keys ignored. Every value is bounded, as the table below shows.

| Key | Value | Largest |
| --- | --- | --- |
| `format` | `spooky-session` | fixed |
| `format_version` | decimal | 3 digits |
| `type` | `field` | fixed |
| `state` | `recording`, `ended` | 9 |
| `end_reason` | recorder reason text, or `none` | 32 |
| `build` | build ID token | 128 (the `SPOOKY_BUILD_ID` limit) |
| `boot` | decimal | 10 digits |
| `start_uptime_ms` | decimal | 10 digits |
| `start_time` | `unknown` | 7 |
| `sample_rate_hz` | `48000` | 5 |
| `channels` | `radio-L,radio-R,mic` | fixed |
| `frames` | decimal, 64-bit | 20 digits |
| `audio_parts` | decimal, 1 to 1000 | 4 digits |
| `part_frames` | decimal | 10 digits |
| `events` | `EVENTS.BIN` | fixed |
| `event_format_version` | decimal | 3 digits |
| `events_written` | decimal | 10 digits |
| `events_lost` | decimal | 10 digits |
| `retune_settle_frames` | `FM:n,AM:n,SW:n,LW:n` | 40 |
| `origin` | `epoch:frame` | 31 |
| `mic_compensation_frames` | decimal | 10 digits |

With every value at its largest, including the 128-byte build ID, the manifest is
about 640 bytes. A key added later must keep the 1,024-byte bound or move to a new
format version.

Example, ended:

```text
format=spooky-session
format_version=1
type=field
state=ended
end_reason=stopped
build=54w61-retune-20261009a
boot=12
start_uptime_ms=184022
start_time=unknown
sample_rate_hz=48000
channels=radio-L,radio-R,mic
frames=2883584
audio_parts=1
part_frames=715827712
events=EVENTS.BIN
event_format_version=1
events_written=963
events_lost=0
retune_settle_frames=FM:0,AM:0,SW:512,LW:512
origin=1:2245835
mic_compensation_frames=0
```

`part_frames` is a whole number of 4,096-frame recorder blocks below the 4 GiB WAV
limit. The value above is an example; the firmware's configured value applies.

## Event log

Little-endian throughout. The header and every record are 32 bytes, so a record's
offset is `32 + 32 * index`.

### Header

| Offset | Size | Field | Value |
| --- | --- | --- | --- |
| 0 | 8 | magic | ASCII `SPKYEVTS` |
| 8 | 2 | major version | 1; a reader rejects a major version it does not know |
| 10 | 2 | minor version | 0; additions that older readers can skip |
| 12 | 2 | record size | 32 |
| 14 | 2 | header size | 32 |
| 16 | 4 | sample rate | 48000 |
| 20 | 12 | reserved | 0 |

### Record

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 8 | frame: session frame, 0 is the origin |
| 8 | 4 | uncertainty: frames before `frame` in which the event may have occurred |
| 12 | 2 | kind |
| 14 | 2 | flags |
| 16 | 4 | sequence: counts every record produced, dropped ones included |
| 20 | 12 | payload, by kind; unused bytes are 0 |

Flags:

| Bit | Meaning |
| --- | --- |
| 0 | the uncertainty was clipped at frame 0 |
| 1 | the condition began before the origin (a tune or gap open at frame 0) |
| 2-15 | reserved, 0 |

### Kinds and payloads

Payload offsets are from the start of the record. Bands are 0 FM, 1 AM, 2 SW, 3 LW.

| Kind | Name | Payload |
| --- | --- | --- |
| `0x0001` | session start | 20: band (u8); 24: frequency kHz (u32) |
| `0x0002` | session end | 20: end reason (u8, below) |
| `0x0010` | tune start | 20: band (u8); 24: target kHz (u32) |
| `0x0011` | tune end | 20: band (u8); 21: outcome (u8: 1 tuned, 2 failed, 3 abandoned); 22: RSSI dBuV (u8); 23: SNR dB (u8); 24: target kHz (u32); 28: frequency kHz (u32, 0 unless tuned) |
| `0x0020` | gap start | 20: cause (u8: 1 band switch); 21: band before (u8) |
| `0x0021` | gap end | 20: cause (u8); 21: band after (u8); 24: frequency kHz (u32) |
| `0x0030` | PTT on | none |
| `0x0031` | PTT off | none |
| `0x0040` | Classic | 20: event (u8: 0 run state, 1 direction, 2 rate, 3 distance, 4 edge, 5 hold time, 255 snapshot); 21: run state (u8: 0 running, 1 paused, 2 sweep complete, 3 unable, 4 holding); 22: unable reason (u8: 0 none, 1 radio stopped, 2 radio faulted, 3 session); 23: edge (u8: 0 wrap, 1 bounce, 2 stop); 24: flags (u8: bit 0 direction up, bit 1 rate limited); 25: hold seconds (u8, 0 off); 26: rate setting per minute (u16); 28: rate in effect per minute (u16); 30: distance in channels (u16) |
| `0x00F0` | events lost | 20: records dropped (u32) |
| `0x8000`+ | experiments | defined by the opt-in experiment; readers skip them |

The Classic codes are the firmware's `ClassicPublished`, `ClassicPublishedRun`,
`ClassicUnableReason` and `ClassicEdge` values, fixed here for version 1.

End reasons: 1 stopped, 2 duration complete, 3 card full, 4 SD card removed,
5 write failed, 6 radio queue overrun, 7 PDM queue overrun, 8 radio DMA error,
9 PDM DMA error, 10 radio timeline unavailable, 11 PDM DMA did not start,
255 other. For 255 the manifest's `end_reason` text names it.

### Example

Header, then a tune end at frame 1,033 with uncertainty 3,600: FM, tuned, RSSI 23,
SNR 2, 99.2 MHz, sequence 5.

```text
53 50 4B 59 45 56 54 53  01 00 00 00 20 00 20 00  80 BB 00 00 00 00 00 00  00 00 00 00 00 00 00 00
09 04 00 00 00 00 00 00  10 0E 00 00 11 00 00 00  05 00 00 00 00 01 17 02  80 83 01 00 80 83 01 00
```

## Reader rules

From decision 0030 items 10 to 13:

- **Range.** The log covers frames 0 to the session's last frame, opened by session
  start and closed by session end.
- **Frame 0.** It also carries the starting state: a Classic snapshot, PTT if held,
  and any tune or gap open at the origin (flag bit 1).
- **Order.** Records are in sequence order. Frames may not ascend; sort by frame, then
  sequence, for time order.
- **Loss.** A sequence gap means lost records. The writer follows a loss with an
  events-lost record, a Classic snapshot, the PTT state and any tune in flight. State
  between the loss and that snapshot is unknown.
- **Retune intervals.** A retune interval is tune start to tune end plus the
  manifest's `retune_settle_frames` for the tune's band (decision 0015 item 4).
- **Gaps.** A gap (decision 0004) runs from gap start to gap end, both exact. The
  radio track is digital silence inside it.
