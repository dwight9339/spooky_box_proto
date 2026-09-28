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
