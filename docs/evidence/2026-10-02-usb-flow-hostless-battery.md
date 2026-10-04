# USB command flow control, host-less recording and battery telemetry

Date: 2026-10-02 (bench timestamps 18:15-19:14 UTC)
Beads issues: `full_spooky_proto-8lw.24`, `full_spooky_proto-8lw.20`,
`full_spooky_proto-8lw.19`

## The changes

- **8lw.24, command input.** The USB CDC OUT endpoint is re-armed only while the
  256-byte command ring has room for another 64-byte packet (`CM7/App/cdc_rx_flow.c`).
  Otherwise the device NAKs and the host keeps its data. `DIAG USB` reports the receive
  counters.
- **8lw.24, interrupt fix (found on this bench).** The HAL's USB-reset handler unmasks
  an interrupt for every NAKed OUT token (`DOEPMSK.NAKM`), and its handler only clears
  the flag. Image `a` left it unmasked; image `b` masks it when the CDC interface is
  configured.
- **8lw.20, host-less recording.** The recorder queues replies only while a host has the
  CDC interface configured. Lines produced with no host, and lines still queued when the
  host goes away, count in `RECORD LATENCY usb-detached` and are not faults.
- **8lw.19, battery telemetry.** `BATTERY READ` and `CHARGE STATUS` read the fuel gauge
  on each request outside recording. While capturing they use the last snapshot, and
  both replies now report its age (`AGE_S`).

## Setup and provenance

| Item | Value |
| --- | --- |
| Image `a` | `build/bench-8lw-m2-bugs-20261002a.json`, Debug, `5457de6` (main after PRs #90 and #91) plus `build/8lw-m2-bugs-20261002a.patch`. Flash run `2026-10-02T181544.750641_0000-c2b0e65a` |
| Image `b` | `build/bench-8lw-m2-bugs-20261002b.json`, image `a` plus the `NAKM` mask (`build/8lw-m2-bugs-20261002b.patch`). Flash run `2026-10-02T181939.352425_0000-5d4c92ea` |
| Card | 64 GB reference `SD64G` |
| Host | Windows, CDC through the usual powered USB hub |
| Drivers | `build/cdc_flood_8lw24.py` (sends N lines at a fixed period while a reader thread classifies every reply) and `build/sd_failure_jjy4.py`; all CDC lines are in `build/8lw-m2-session.jsonl` |

## Results

### Command flood (8lw.24)

Each flood sent 1,000 `DIAG IDENTITY` lines, one every 5 ms. A merged or truncated
line would have come back as `ERR unknown command`, `ERR usage` or
`ERR command too long`.

| Image | Condition | Replies | Corrupt | Send time | `DIAG USB` | Recording |
| --- | --- | --- | --- | --- | --- | --- |
| a | Idle | 1000 | 0 | 10.8 s | `RX_PAUSES=922 RX_OVERRUNS=0` | - |
| a | During `RECORD START 60` | 1000 | 0 | 11.8 s | `RX_OVERRUNS=0` | **ABORT, radio queue overrun** after 1.2 s. Recorder passes of 198-312 ms, `convert-max=210ms`, `LOOP_MAX_MS=324` |
| b | Idle | 1000 | 0 | 5.9 s | `RX_OVERRUNS=0` | - |
| b | During `RECORD START 60` | 1000 | 0 | 9.5 s | `RX_PAUSES=1903` (cumulative) `RX_OVERRUNS=0` | `REC086.WAV` PASS 60.074 s, max write 51 ms, `convert-max=15ms`, queues 1/8, `HAS_FAULT=0` |

With image `a`, the pauses themselves starved the CPU. A conversion that takes 15 ms
of pure CPU took 210 ms, so interrupts were taking almost all of the time. The
stalls began when the flood began. While the endpoint is paused, the host retries the
OUT transfer continuously, and each NAK raised an interrupt. Masking `NAKM` removed
the stall: the recording was clean, and the idle flood ran almost twice as fast.

### Host-less recording (8lw.20)

The user unplugged the CDC cable during `RECORD START 300` (`REC090.WAV`) for at least
20 s and replugged it. Two earlier attempts (`REC087`, `REC088`) missed the window.

| Check | Result |
| --- | --- |
| Mid-recording, after the replug (54.6 s) | `OK RECORD ACTIVE`, max write 50 ms; `usb-detached` 3 → 17; `usb-lost=0`; `HAS_FAULT=0`; `DIAG USB` counters reset, so the device re-enumerated |
| First recorder line after the replug | `RECORD progress=54.9s`, current, not a stale queued line |
| Outcome | `OK RECORD PASS file=REC090.WAV audio=300.032s`, queues 1/8, max write 50 ms, margin OK |
| Fault history | `DIAG STATUS HAS_FAULT=0`, `DIAG LAST NONE`, no `USB_BACKPRESSURE` in any reply this session |

Since `jjy.10`, the fault flood described in `8lw.20` (one fault per line) no longer
happened. A host-less recording still evicted one or two replies, and that set
`HAS_FAULT`. Queued lines were also delivered stale when a host connected later.
Both are gone.

### Battery telemetry (8lw.19)

| When | Reply |
| --- | --- |
| Idle, repeated | `AGE_S=0` each time; the current changes between reads (−45 to −65 mA) |
| During `RECORD START 30` (`REC089`) at 6, 12 and 18 s | `CHARGE ... AGE_S=19`, `AGE_S=25`, `BATTERY ... AGE_S=31`: the last snapshot, with no I2C |
| Right after that recording | `AGE_S=0`, current −65 mA (a live read) |

**Charger case, direct PC port (19:12-19:14 UTC).** The user moved the CDC cable from the
hub to a direct PC USB port. The board was on the final-code IpcSmoke pair
(`8lw-m2-bugs-ipcsmoke-20261002b`). Results:

- **On the PC port:** `CHARGE VBUS=1 STATE=CHARGING CURRENT=298 mA ... AGE_S=0`,
  `BATTERY ... SOC=27% VOLTAGE=3705 mV ... AGE_S=0`. The battery charges from a port that
  holds its voltage.
- **Cable out for about 20 s, then back into the same port:** the first reply was
  `CHARGE ... STATE=CHARGING CURRENT=301 mA ... AGE_S=0`, and the next one 299 mA.
  `DIAG IDENTITY` still reported `BOOT=10`, so the board did not reboot, and the
  `DIAG USB` counters had reset, so the device re-enumerated.

This shows the reply after a plug-in is a live reading. It does not reproduce the
original failure exactly: the old code returned a stale value only when its 5-minute
snapshot had been taken while unplugged, and that would need the cable out for more
than 5 minutes.

**Hub port (earlier in the session).** The battery did not charge from the usual
powered hub (`VBUS=1`, but `DISCHARGING` at about −55 mA, SOC 26%). The user has seen
that hub's VBUS sag to about 4.4 V and is handling the rig.

### Automated recording regression (M2 exit)

`spookybench test recording-regression --seconds 60` (tooling rule 7) flashes a paired
IpcSmoke build, records under IPC load, then retrieves the WAV and checks it with a CRC.
The first pair was built before the `NAKM` mask, from image `a`'s source, so it is
superseded. The second pair matches the final code.

| Pair | Flash and test run | Result |
| --- | --- | --- |
| `build/bench-8lw-m2-bugs-ipcsmoke-20261002a.json` (source of image `a`; superseded) | `2026-10-02T183925.414830_0000-e5f72848` | All six stages pass; `REC091.WAV` 60.075 s, max write 49 ms, queue high-water 0, no fault or log-drop deltas |
| `build/bench-8lw-m2-bugs-ipcsmoke-20261002b.json` (final code, `build/8lw-m2-bugs-ipcsmoke-20261002b.patch`) | `2026-10-02T184743.442343_0000-3e96adbf` | All six stages pass (boot smoke, prerequisites, recording, WAV inspection, accounting, post-transfer health); `REC092.WAV` 60.075 s, CRC `ca75d210`, max write 25 ms, queue high-water 0; `RADIO_OVR`, `PDM_OVR`, `SD_ERR`, `AUDIO_ERR` and `HAS_FAULT` deltas 0; no logger drops |

The regression's listening check is a judgment check; it is pending and was not done
in this session.

## Verdict

- **8lw.24:** pass with image `b`. Every streamed line gets exactly one reply, nothing
  is lost or merged, and a recording under a 5 ms command flood is clean.
- **8lw.20:** pass. A recording with the host unplugged completes with no fault and
  no stale replies on reconnect.
- **8lw.19:** pass for freshness: live reads outside recording, and an aged snapshot
  with its age during recording. On a direct PC port, the first read after a plug-in
  reports `CHARGING` live. The hub port does not charge the battery (a rig issue the
  user is handling).
- **M2 recording regression:** pass on the final code. Listening is still pending.

Host tests are not part of this evidence.
