# Recording outcome recovery with RECORD RESULT

Date: 2026-10-01
Beads issue: `full_spooky_proto-jjy.12`

The [recorder reply queue session](2026-10-01-recorder-reply-queue.md) showed that the
target keeps and sends the recording outcome after a closed-port recording, but a
host whose port open discards received data (pyserial on Windows) still loses it.
This session checks the read-only `RECORD RESULT` query, which reports the last
finished recording on request, using the stock pyserial open throughout.

## Setup and provenance

| Item | Value |
| --- | --- |
| Source revision | `1075e0e` plus the jjy.12 change (`CM7/App/recording_result.[ch]`, `CM7/Core/Src/radio_recorder.c`, `CM7/App/diagnostics.c`, `CM7/CMakeLists.txt`, `tests/`) |
| Bench image | `build/bench-jjy12-result-20261001a.json`, build ID `jjy12-result-20261001a`, Debug preset; patch `build/jjy12-result-20261001a.patch` |
| CM7 / CM4 SHA-256 | `54435b0dceb34da9e48bc03e7c5db814d9a3a1a8d1c303a936c39de4677b1764` / `d5ab1687e85e3d5a975b5e5c98bdfa8ca445b7aa8bbabc11c6dd008f94a91809` |
| Flash | Spooky Bench `flash`, pass, both cores verified (run `2026-10-01T202147.136850_0000-2b3fb103`); `DIAG IDENTITY` reported `BUILD=jjy12-result-20261001a` |
| Compiler | GNU Tools for STM32 14.3.1; Release pair also built |
| Board | Nucleo prototype; target CDC `335A34763533`; probe `E66540F0A345382D` |
| SD | 64 GB SDHC/SDXC reference card |
| Driver | `build/sd_failure_jjy4.py` with pyserial's normal open; all CDC lines in `build/jjy12-session.jsonl` |
| Bench power, listening check, WAV inspection | Not performed |

The board was run remotely; nobody was at the bench.

## Results

| Label | Step | Reply |
| --- | --- | --- |
| `ident` | `RECORD RESULT` after boot, before any recording | `OK RECORD RESULT NONE` |
| `bp60-start` | `RECORD START 60`, port closed once `OK RECORD START` arrived | `REC074.WAV` started |
| `bp60-reopen` | Port reopened 75 s later (stock open), `RECORD RESULT` | `seq=1 outcome=PASS file=REC074.WAV frames=2883584 bytes=17301504 elapsed=60121ms finalized=1 reason=duration complete` |
| `active-stop` | `RECORD START 30`, then `RECORD RESULT` while active | Previous outcome, `seq=1 ... file=REC074.WAV` |
| `active-stop` | `RECORD STOP` | Pushed `OK RECORD PASS file=REC075.WAV frames=286720 ... reason=stopped` and `RECORD DIAG` |
| `after-stop` | `RECORD RESULT` | `seq=2 outcome=PASS file=REC075.WAV frames=286720 bytes=1720320 elapsed=6025ms finalized=1 reason=stopped`, matching the pushed line |

`HELP` lists `OK RECORD STATUS|RESULT|LATENCY|START [seconds]|STOP`. Both recordings
completed with exact frame counts. `RECORD LATENCY` after `REC074` reported
`usb-superseded=10 usb-lost=0`, and `DIAG STATUS` stayed at `HAS_FAULT=0`.

The ABORT outcome was not produced on hardware: it needs a card removal or a full
card, which needs someone at the bench. Its formatting and the PASS/ABORT rule are
covered by `tests/recording_result_test.c` (18/18 host tests pass).

## Verdict

Pass: with a host whose port open discards received data, the outcome of a recording
that finished while the port was closed is recovered with `RECORD RESULT` after
reconnect. The ABORT path is host-tested only.
