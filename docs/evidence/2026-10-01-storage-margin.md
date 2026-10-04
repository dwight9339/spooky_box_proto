# Storage margin warning

Date: 2026-10-01 (local time; the UTC timestamps in the log and run IDs read 2026-10-02)
Beads issue: `full_spooky_proto-jjy.8`

This session checks item 5 of [decision 0014](../decisions/0014-sd-media-policy.md):
the recorder reports a low storage margin, as a warning, once a queue's high-water
mark or a single SD write reaches its threshold.

No available supported card stalls long enough to reach the default thresholds
(4 of 8 blocks or 341 ms). The `LOW` path was therefore exercised by lowering the
write threshold through its build setting, so that the reference card's ordinary
writes cross it.

## Setup and provenance

| Item | Value |
| --- | --- |
| Source revision | `d739b17` plus the change (`CM7/App/storage_margin.[ch]`, `CM7/Core/Src/radio_recorder.c`, `Common/Inc/diag_history.h`, `Common/Src/diag_history.c`, `CM7/CMakeLists.txt`, `cmake/dual_core_firmware.cmake`, `host/spookybench/ipc_load.py`, `host/spookybench/fake.py`, `tests/`); patch `build/jjy8-margin-20261001a.patch` |
| Committed source | Differs from the bench patch only in docs |
| Image A | `build/bench-jjy8-margin-20261001a.json`, Debug, default thresholds. Flash run `2026-10-02T031249.425985_0000-b16454e4` |
| Image B | `build/bench-jjy8-margin-low20-20261001a.json`, Debug, `SPOOKY_STORAGE_MARGIN_WRITE_MS=20`. Flash run `2026-10-02T031400.498590_0000-0937a6b9` |
| Image C | `build/bench-jjy8-margin-low15-20261001a.json`, Debug, `SPOOKY_STORAGE_MARGIN_WRITE_MS=15`. Flash run `2026-10-02T031513.731408_0000-5b86c44b` |
| Flash | Spooky Bench `flash`, pass, both cores verified; `DIAG IDENTITY` matched each build ID. The images were built in sequence in `build/Debug`, so each image is identified by its manifest's SHA-256 values |
| Card | 64 GB reference card (`SD64G`, Phison OEM) |
| Driver | `build/sd_failure_jjy4.py`; all CDC lines in `build/jjy8-session.jsonl` |

## Results

| Image | Recording | Live `RECORD STATUS` | `RECORD DIAG` | Max write | `HAS_FAULT` / `DIAG LAST` |
| --- | --- | --- | --- | --- | --- |
| A (341 ms) | `REC076.WAV`, 30 s, PASS | - | `margin=OK` | 29 ms | 0 / - |
| B (20 ms) | `REC077.WAV`, 30 s, PASS | `margin=OK` at 2.3 s (max write 17 ms) | `margin=LOW` | 23 ms | 0 / `NONE` |
| C (15 ms) | `REC078.WAV`, 20 s, PASS | `margin=LOW` at 1.2 s | `margin=LOW` | 23 ms | 0 / `NONE` |

In image C, `DIAG DUMP` 2.8 s into the recording held exactly one
`EVENT=STORAGE_MARGIN A=1 B=15` (queue high-water 1, write 15 ms) at 125 ms after
`RECORD_START`. Later writes over the threshold, including the 23 ms maximum, did not
add events: the warning latched. In image B the event was recorded after the early
dump and had left the 128-entry history by the end of the recording; SD write events
fill it in about 11 s while recording.

Every recording passed with exact frame counts and queues at 1/8. The warning set no
fault, as the decision requires.

## Verdict

Pass: `margin=OK` with the default thresholds on a healthy card; with a lowered
threshold the margin turns `LOW` live and in `RECORD DIAG`, records one
`STORAGE_MARGIN` event, latches, and does not raise a fault or stop the recording.
The default thresholds themselves are not yet exercised by a slow supported card.
