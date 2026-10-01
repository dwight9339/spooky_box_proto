# Host tests

Run in a shell with a native C11 compiler, CMake, and Ninja on PATH. On
Windows use a Visual Studio Developer Command Prompt (VS 2019 16.11 or newer).
These tests do not use the ARM toolchain or connect to the board.

The separate Python Spooky Bench tests live in `host/tests`; see
[bench setup](../docs/procedures/spooky-bench-setup.md#simulation-and-tests). They do not
change or replace these native firmware tests.

```text
cmake -S tests -B build/host -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/host
ctest --test-dir build/host --output-on-failure
```

Tests of StateSmith machines add the generated C from `CM7/App/sm` with
`spooky_generated_sm(<test> <Name>Sm.c)`. It suppresses unused-parameter warnings, and
MSVC's unreachable-code warning, for those files only.

The InputResolution tests drive the generated machine through its real port against a
fake integration that records deliveries, commands, published events and timer calls.
They cover each behavior of decision 0005 items 1-9: the start and stop prompts,
swallowing before the prompt, confirm, cancel, dismissal, the swallowed rest of the
hold, withdrawal on a session change, pre-held releases delivered exactly once, and
release-all from every state. A seeded random sequence checks that every input gets
exactly one outcome and that only a confirming Encoder 0 press issues a command.

The Session tests drive the generated Session machine through its port with scripted
action results: start success, each rejection and abort path, stop and repeated stop,
block-driven completion, failed finalization and capture faults, plus a seeded random
check that a failure is never published as a success. The Session authority tests post
external commands and internal recorder outcomes through the real event queue and
dispatcher, verify that machine actions invoke the recorder in order, and check that
an unreported recorder change still produces a disagreement diagnostic.

The Radio tests drive the generated Radio machine through its port against a fake
radio service: start outcomes, range rejection, a tune that waits for completion,
issue and completion failures, latest-wins replacement of a waiting command with a
superseded answer, a band switch waiting behind a tune, band-switch and audio
faults abandoning work in progress, band-edge stop and wrap, stale completions, and
a seeded random check that every command is answered exactly once. The radio
control tests run the real radio control service against a fake Si4735 on a fake
I2C bus and tick. They check that a tune returns before it completes, that each
poll is at most one status transaction and respects the poll interval, that a tune
fails at the 2 s device timeout, that a stuck receiver fails a poll or an issue
within the provisional 5 ms device-ready bound, and that the published result
changes only when a tune completes. The fake is not a model of Si4735 timing.

The command-policy tests cover every decision-0008 action in Idle, Recording and
Finalizing, verify that no action is deferred, require one stable acknowledgement for
every rejection, and check the current CLI-to-action mappings. These are portable
policy tests, not recording evidence.

The storage-lease test checks exclusive ownership across recorder, SD stress,
WAV transfer and status clients, including busy acquisition and invalid-release
counters. Media mounting and SDMMC behavior remain hardware-only evidence.

The reply-queue test checks the recorder's USB reply policy: with the port closed for
a whole recording, progress lines coalesce to the newest one and the outcome and
diagnostic replies survive in order; a full queue evicts progress before replies, then
the oldest reply, and counts both. USB CDC timing remains hardware-only evidence.

The event-queue tests cover decision 0007: post order, sequence and time, admission
by class with the internal reserve, reject-newest overrun, one reconcile event after
rejected input, bounded run-to-completion dispatch, the reserve's exact limit when
handled events post internal events, tick and sequence wrap, and counters. The
logger/diagnostic tests also check the `DIAG QUEUE` line and the `EVENT_QUEUE_LOSS`
fault.

The audio-timeline tests cover the portable arithmetic of decision 0012: 32-bit
counter extension across wrap, DMA snapshots taken while a half-buffer completion is
pending but not yet counted, a monotonic walk across many buffers, stream restart
epochs and stale positions, backwards counts, start alignment of the radio and
microphone tracks against a modelled common signal, and event-stamp uncertainty
clipped at an origin. The firmware does not yet link this module, so these tests say
nothing about DMA timing, the DFSDM start latency or recording alignment on the
board.

The IPC tests compile the same portable protocol code as both cores. They
exercise invalid headers, version/size mismatches, first handshake, missing
acknowledgements, corrupted echoes, stale repeated packets, recovery, sequence
wrap, tick wrap, and independent core clock epochs. Checks remain enabled in
Release. The firmware builds also enforce the packet layout with C11 static
assertions and the mailbox allocation with linker assertions.

The logger/diagnostic tests compile the real App adapters against fake HAL and
USB transports, as well as the portable buffers. They check queue saturation,
buffer lifetime while UART owns a chunk, wrap and refill, context rejection,
transport errors, timeout across tick wrap, bounded sleep drain/abort, event
overwrite, preserved latest fault, and sequence wrap. CLI tests cover USB busy
retry, finite dumps, overwrite gaps, cancellation, timeout, and streamed HELP.
The HAL stub is not a UART timing or NVIC simulator.

These tests do **not** establish SRAM visibility, MPU/cache behavior, HSEM
operation, boot timing, or recording reliability. Those require the
[IPC bench procedure](../docs/procedures/ipc-smoke-test.md).
