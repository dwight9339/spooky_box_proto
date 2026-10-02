# SD media identification and SDSC recording refusal

Date: 2026-10-01 (local time; the UTC timestamps in the log and run IDs read 2026-10-02)
Beads issue: `full_spooky_proto-jjy.8`

This session checks the first part of [decision 0014](../decisions/0014-sd-media-policy.md)
(Proposed): `RECORD START` refuses SDSC cards, and the new `SD INFO` command identifies
each card used as evidence.

## Setup and provenance

| Item | Value |
| --- | --- |
| Source revision | `3ab3e7f` plus the change (`CM7/App/sd_media.[ch]`, `CM7/App/storage_service.[ch]`, `CM7/Core/Src/radio_recorder.c`, `CM7/Core/Src/sd_test.c`, `CM7/App/diagnostics.c`, `tests/`) |
| Bench image | `build/bench-jjy8-media-20261001a.json`, build ID `jjy8-media-20261001a`, Debug preset; patch `build/jjy8-media-20261001a.patch` |
| Committed source | Differs from the bench patch only in docs |
| Flash | Spooky Bench `flash`, pass, both cores verified (run `2026-10-02T025341.735659_0000-7dd5cd14`); `DIAG IDENTITY` reported `BUILD=jjy8-media-20261001a` |
| Board | Nucleo prototype; target CDC `335A34763533`; probe `E66540F0A345382D` |
| Driver | `build/sd_failure_jjy4.py`; all CDC lines in `build/jjy8-session.jsonl` |
| Bench power, listening check, WAV inspection | Not performed |

## Card identities

| Card | `SD INFO` |
| --- | --- |
| 2 GB SDSC (the jjy.4 test card) | `TYPE=SDSC SUPPORTED=0 CAPACITY=1886MiB MID=0x03 OID=SD PNM=SU02G PRV=8.0 PSN=0x0BDB627C MDT=2010-12 SPEED_CLASS=2 UHS_GRADE=0 VIDEO_CLASS=0 AU=4096KiB` |
| 16 GB SDHC (used, device-formatted) | `TYPE=SDHC/SDXC SUPPORTED=1 CAPACITY=15193MiB MID=0x03 OID=SD PNM=SC16G PRV=8.0 PSN=0x934591FE MDT=2022-09 SPEED_CLASS=10 UHS_GRADE=0 VIDEO_CLASS=0 AU=4096KiB` |
| 64 GB reference card | `TYPE=SDHC/SDXC SUPPORTED=1 CAPACITY=59344MiB MID=0x27 OID=PH PNM=SD64G PRV=6.0 PSN=0x6CE776AE MDT=2025-04 SPEED_CLASS=10 UHS_GRADE=0 VIDEO_CLASS=0 AU=4096KiB` |

Manufacturer ID 0x03 with OEM `SD` is the SanDisk assignment. Manufacturer ID 0x27
with OEM `PH` is Phison, a controller supplier, so the reference card's retail brand is
not identified by its CID. The `SPEED_CLASS`, `UHS_GRADE` and `VIDEO_CLASS` values are
the card's SD Status fields; the bus here runs in default speed mode.

## Results

### SDSC card

| Step | Result |
| --- | --- |
| `SD STATUS` | `TYPE=SDSC ... FREE=1807MiB` |
| `RECORD START 10` | `ERR RECORD unsupported card TYPE=SDSC; SDHC/SDXC required` |
| `RECORD STATUS`, `RECORD RESULT` | `OK RECORD IDLE last-file=none`; `OK RECORD RESULT NONE`: no file, no recording |
| `SD STATUS` again | Unchanged, `FREE=1807MiB` |
| `DIAG STATUS` | `HAS_FAULT=0` |

### 16 GB SDHC card

`RECORD START 10` recorded `REC002.WAV`: `OK RECORD PASS`, exact frames, and
`RECORD RESULT` matched. One write took 129 ms (histogram `0,109,8,0,0,0,0,1`) and the
radio queue reached 2/8. That exceeded the recorder's 70 ms and the loop's 75 ms
budgets once each, so `DIAG LAST` reports `FOREGROUND_BUDGET A=0 B=135` and
`HAS_FAULT=1`. No overrun occurred: 2/8 is far below the queue headroom.

The same card peaked at 46 and 66 ms in the [format session](2026-10-01-sd-format.md),
so this card's stalls vary from recording to recording. The budget fault fires the same
way for this 129 ms write as for the SDSC card's 607 ms near-overrun; decision 0014
item 5 proposes a margin warning to tell them apart.

### 64 GB reference card

`SD INFO` and `SD STATUS` only (read-only): `FREE=57576MiB`, unchanged.

## Verdict

Pass: an SDSC card cannot start a recording and stays readable, a supported card
records, and `SD INFO` identifies all three cards. The 16 GB result adds a 129 ms
stall to the `jjy.8` survey data.
