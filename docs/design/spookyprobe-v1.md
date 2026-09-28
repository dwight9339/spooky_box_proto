# Spooky Probe logger and diagnostics v1

This is the integration contract for parallel work in the device and Spookyprobe
repos. The STM32 implementation is in `CM7/App/target_logger.*` and
`CM7/App/diagnostics.*`; portable event definitions are in
`Common/Inc/diag_history.h`. Live capture/decoder evidence is in the
[September 24 results](../evidence/2026-09-24-bench-results.md); qualification status
is tracked in Beads `full_spooky_proto-jjy`.

## Two independent inputs

| Input | Transport | Content |
| --- | --- | --- |
| Target text | UART7, 115200 baud, 8N1, no flow control | Existing human-readable printf output |
| Target diagnostics | Device USB CDC command port | Commands and line-oriented responses below |

UART7 TX is PE8, connected to Pico GP5 RX; PE7 RX connects to Pico GP4 TX.
UART ownership remains on M7. This slice does not implement UART command input,
M4 log forwarding, or automatic diagnostic streaming through the Pico.
The host must distinguish the probe's USB serial port from the device's USB CDC
port. Do not send diagnostic commands to the UART log capture port.

Text is an arbitrary byte stream: reads can split lines or combine many lines.
It has no wire sequence, timestamp, CRC, or guaranteed delivery. Save original
bytes; attach host receive timestamps without treating them as MCU event times.
The 4096-byte target queue rejects complete writes that exceed 512 bytes or its
remaining capacity. A printf invocation can comprise multiple writes, so a
partial or missing line is possible. UART failure can also truncate a line.
Target counters, probe overflow, and host capture loss are distinct quantities.

## USB command protocol

Send one command at a time, terminated by newline. Responses use CRLF. Other
device messages may interleave: identify diagnostics by prefix and keys, not by
assuming the next received line is the response. There is no request ID.
Each diagnostic line is at most 255 bytes including CRLF. Numeric fields are
unsigned decimal, 32-bit unless specified otherwise; counters, sequence, and
MCU milliseconds wrap modulo 2^32. Schema version is `V=1`; producer is `CORE=7`.

Commands and representative responses:

```text
LOG STATUS
OK LOG QUEUED=0 PEAK=300 DROP_WRITES=2 DROP_BYTES=120 TX_LOST=0 TX_BYTES=1024 TX_ERRORS=0 CONTEXT=0 FLIGHT=0
DIAG STATUS
OK DIAG V=1 CORE=7 COUNT=2 OVERWRITTEN=0 SD_MAX_MS=8 LOOP_MAX_MS=0 RADIO_OVR=0 PDM_OVR=0 SD_ERR=0 AUDIO_ERR=0 HAS_FAULT=0
DIAG QUEUE
OK DIAG QUEUE CAP=32 RESERVE=8 COUNT=0 PEAK=0 POSTED=0 DISPATCHED=0 REJ_INPUT=0 REJ_CMD=0 REJ_INTERNAL=0 RECONCILES=0 MAX_WAIT_MS=0
DIAG LAST
OK DIAG LAST NONE
DIAG DUMP
OK DIAG DUMP V=1 CORE=7 FIRST=1 COUNT=2
DIAG EVENT SEQ=1 MS=0 EVENT=BOOT A=1 B=7
DIAG EVENT SEQ=2 MS=2000 EVENT=SD_WRITE A=8 B=65537
OK DIAG END COUNT=2 GAPS=0
```

`QUEUED` includes in-flight bytes; `FLIGHT` is the current UART chunk size.
`PEAK` is peak queued bytes. `DROP_*` counts rejected producer writes/bytes;
`CONTEXT` counts writes rejected from IRQ or masked-IRQ context. `TX_LOST` counts
bytes discarded on transport failure/timeout, conservatively including bytes
that may already have left the UART. `TX_BYTES` counts completed chunks.
`TX_ERRORS` counts transport failures/timeouts. All are boot-lifetime totals.

`DIAG QUEUE` reports the M7 application event queue
([decision 0007](../decisions/0007-m7-event-queue.md)). `CAP` and `RESERVE` are the
capacity and the slots reserved for internal events. `COUNT` is the current number of
queued events and `PEAK` its high-water mark. `POSTED` and `DISPATCHED` count admitted
and handled events. `REJ_INPUT`, `REJ_CMD` and `REJ_INTERNAL` count rejected input
events, CLI commands and internal events. `RECONCILES` counts reconcile events posted
after rejected input. `MAX_WAIT_MS` is the longest time an event waited between post
and dispatch. All are boot-lifetime totals. `REJ_INTERNAL` must stay zero; any
increase is recorded as an `EVENT_QUEUE_LOSS` fault.

The RAM history holds 128 events, overwrites oldest entries, and preserves the
latest fault and cumulative counters separately. `DIAG LAST` returns either
`NONE` or `OK DIAG LAST SEQ=... MS=... EVENT=... A=... B=...`.
History and counters do not survive reset. BOOT starts a new sequence epoch;
after reconnect or an ambiguous reset, start a new host session rather than
silently joining two epochs. MCU milliseconds pause during charging sleep and
are not wall-clock time.

`DIAG DUMP` captures the first sequence and count, not a frozen copy of events.
It streams at most one line per foreground service. If an entry is overwritten
before sending, its row is `DIAG GAP SEQ=...`. END COUNT includes event and gap
rows, with GAPS giving the latter count. New events beyond the captured range
belong to a later dump. Sequence zero is valid after wrap.

`DIAG STOP` cancels a pending response and attempts `OK DIAG STOP`. A conflicting
diagnostic command attempts `ERR DIAG reply active; use DIAG STOP`. Invalid
commands attempt a usage error. These immediate acknowledgements can be lost
when USB is busy. Data responses retry while USB is busy; after five seconds
without an accepted line the pending response is abandoned. Disconnect, reset,
STOP, or this timeout may leave a dump without END: mark it incomplete.
An accepted USB send is not proof of host receipt.

## Event meanings

| ID / EVENT | A | B |
| --- | --- | --- |
| 1 BOOT | Schema version (1) | Core (7) |
| 2 RECORD_START | Requested seconds (1–3600) | Sample rate Hz |
| 3 RECORD_END | Frames written | Bit 0: aborted/finalization failed; bit 1: finalized |
| 4 SD_WRITE | Data block write duration ms | Radio queue count bits 0–15, PDM count bits 16–31 |
| 5 SD_ERROR | FatFs result for failed data block write | Bytes written (short write can have result 0) |
| 6 RADIO_OVERRUN | Radio queue count | PDM queue count |
| 7 PDM_OVERRUN | PDM queue count | Radio queue count |
| 8 AUDIO_ERROR | Source: 1 SAI2 RX, 2 SAI1 TX, 3 DFSDM | HAL error bits |
| 9 IPC_LINK | 0 waiting, 1 up, 2 stale, 3 incompatible | IPC validation error enum |
| 10 LOOP_STALL | Foreground diagnostics service gap ms (threshold 50) | Reserved 0 |
| 11 LOG_LOSS | Cumulative rejected writes | Cumulative rejected bytes |
| 12 LOG_ERROR | Cumulative transport errors | Cumulative transport discarded bytes |
| 13 SLEEP | 1 entry, 2 charging report wake | Reserved 0 |
| 14 EVENT_QUEUE_LOSS | Cumulative rejected internal events (a fault) | Queue high-water mark |
| 15 SESSION_MISMATCH | Authoritative Session machine state: 0 idle, 1 recording, 2 finalizing (a fault) | Recorder active: 1 yes, 0 no |
| 16 COMMAND_REJECTED | Semantic command-action ID from `command_policy.h` | Session state: 0 idle, 1 recording, 2 finalizing |

IPC events are present only in IPC experiment builds. Logger and event-queue loss
events are sampled at most once per second, so one event can summarize multiple
losses. Overrun and
audio error events are latched rather than emitted continuously in IRQs. SD_WRITE
covers recorder data writes, not every filesystem operation or SD stress test.
RECORD_END bit 0 also covers finalization failure. This is not a HardFault/NMI or
reset-persistent crash recorder. Parsers must preserve unknown event names and
keys for forward compatibility, and reject unsupported schema versions clearly.
