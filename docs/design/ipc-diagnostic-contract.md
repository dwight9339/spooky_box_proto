# Diagnostic IPC contract

This is the contract for the opt-in `IpcSmoke`/`IpcMismatch` diagnostic experiment: one
latest diagnostic frame in each direction between M7 and M4. It is not the product
IPC. Normal Debug and Release builds suspend the M4 tick and leave M4 asleep after
boot, and `IPC STATUS` reports `DISABLED` in those builds.

- Bench procedure: [IPC smoke test](../procedures/ipc-smoke-test.md).
- Qualification status: Beads `full_spooky_proto-jjy`.
- Evidence: [2026-09-23 results](../evidence/2026-09-23-bench-results.md#initial-ipc-hardware-results)
  and [2026-09-24 IPC load results](../evidence/2026-09-24-bench-results.md#phase-3-ipc-progress-under-recording-load).

## Contract and memory

The experiment exchanges one latest diagnostic frame in each direction. It
does not implement application commands, input queues, audio transfer, or UI
ownership. This keeps the first bench question small: can both cores exchange
validated data repeatedly while the existing audio workload runs?

ST documents SRAM4 as shared memory accessible to both cores, mapped at
`0x38000000`. See [AN5557](https://www.st.com.cn/resource/en/application_note/an5557-stm32h745755-and-stm32h747757-lines-dualcore-architecture-stmicroelectronics.pdf)
and [AN5617](https://www.st.com/resource/en/application_note/an5617-introduction-to-interprocessor-communications-for-stm32h745755-and-stm32h747757-mcus-stmicroelectronics.pdf).

| Item | Experiment decision |
| --- | --- |
| Shared region | First 256 bytes of SRAM4, reserved in both linker scripts. |
| Contents | Two 32-byte frames at offsets 0 (M7 writer) and 32 (M4 writer). |
| Initialization | `NOLOAD`, outside `.bss`; M7 clears both frames before releasing M4 from boot STOP. |
| Cache policy | M7 MPU region 7: normal, shareable, noncacheable, nonbufferable, execute-never. Configured before first mailbox access. M4 has no D-cache. |
| Mutual exclusion | HSEM 1, one nonblocking fast-take per service attempt; failed takes increment `BUSY`. HSEM 0 remains boot-only. |
| Ordering | DMB after lock acquisition and before release; volatile shared accesses occur only while holding HSEM 1, except pre-release initialization. |
| Scheduling | Foreground service every 100 ms. M4 leaves SysTick running and uses WFI between interrupts. No new IPC IRQ or doorbell. |
| Health | Local observation time; stale after 2 seconds without peer sequence or acknowledgement progress. |
| Reset/power | Paired system reset only. Independent core restart and product sleep coordination are not supported. `SLEEP START` is rejected in experiment builds. |

All frame fields are `uint32_t`, in order: magic, ABI version, size, publication
sequence, local uptime in ms, acknowledged peer sequence, payload, peer error.
Sequence zero means absent; wrap skips zero. M7 publishes a deterministic
challenge (`sequence XOR 0xA5C35A3C`); M4 echoes the received payload and the
sequence it belongs to. M7 checks both. No pointers or C enums cross the wire.

The portable validation and health code lives in `Common/Src/ipc_smoke_protocol.c`.
The STM32 transport is `Common/Src/ipc_smoke.c`; CLI formatting is separate in
`CM7/App/ipc_smoke_cli.c`. Both linkers assert a 64-byte mailbox when present.
No packet retries, unbounded waits, logging, or storage operations occur under
the IPC semaphore. Existing interrupts may preempt the holder; the other core
skips that attempt rather than waiting.

MPU region 7 and HSEM 1 are reserved for this experiment. Revisit this choice
before adding another MPU policy, cache enable sequence, or power manager.
M7 must initialize before **any** access to the mailbox, including by M4.

## Error codes and health semantics

Error codes: 0 valid, 1 empty/unpublished, 2 magic, 3 version, 4 size,
5 sequence, 6 echo. An incompatible peer's error is also surfaced. A missing
peer remains WAITING; a previously responsive peer becomes STALE.

This heartbeat measures **foreground progress**. Existing SD stress commands,
SD timeouts, and other blocking bring-up operations can delay M7 service and
cause a stale report even while its CPU and audio interrupts run. That is
useful evidence for the service-budget work, not proof the other core crashed.
Do not move it to SysTick just to conceal a blocked foreground loop.

## Direction after qualification (target, not implemented)

Keep the diagnostic ABI separate from product state. Add a versioned semantic
snapshot and bounded M4 input queue, with overflow counters and explicit
resynchronization of held controls. Prove ordering, queue saturation, stale
state, and reconnect behavior before assigning physical UI peripherals to M4.
Add HSEM doorbells only when needed; the first polling test does not decide
the final transport or scheduling architecture.
