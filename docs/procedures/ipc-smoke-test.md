# IPC smoke test procedure

Bench procedure for the opt-in diagnostic IPC experiment. The frame layout, memory and
cache policy, error codes and health semantics are defined in the
[diagnostic IPC contract](../design/ipc-diagnostic-contract.md). Qualification status is
tracked in Beads `full_spooky_proto-jjy`. Results belong in `docs/evidence/`.

## Build and deploy when back at the bench

```text
cmake --preset IpcSmoke
cmake --build --preset IpcSmoke
```

The matching images are under `build/IpcSmoke/firmware/CM7` and `CM4`.
Use the dedicated PlatformIO environment to build, flash **both**, verify,
and reset:

```text
pio run -e spooky_box_ipc_smoke -t deploy
```

Do not use `deploy_cm7` for this experiment. A previous sleeping M4 image
cannot acknowledge packets. Follow the existing power/wiring procedure in
[BRINGUP.md](../../BRINGUP.md) before deployment.

## Acceptance sequence

1. First validate the refactored normal Debug image: console, radio/headphones,
   volume/jack behavior, battery, short recording, and charging sleep with its
   10-second RTC self-test. This separates extraction regressions from IPC.
2. Deploy `IpcSmoke`, wait for normal boot/CDC readiness, issue `IPC STATUS`
   several times at least a second apart. Require `LINK=UP`, `VERSION=1`,
   `PEER_VERSION=1`, increasing TX/RX/ACK/ROUNDTRIPS, both SEEN fields 1, and
   error fields 0. Ages are milliseconds; zero age with SEEN=0 means unseen.
   `ROUNDTRIPS` counts distinct acknowledgements observed, not every packet
   or a latency measurement. `OK` acknowledges the diagnostic command; the
   `LINK` field is the actual health result.
3. Run `RECORD START 60`, inspect `RECORD STATUS` and `IPC STATUS`, then inspect
   the resulting WAV as in BRINGUP. Require no recorder overrun or abort and
   continued IPC progress.
4. If the debugger can halt only M4 while M7 continues, halt M4 and wait over
   two seconds. Require `STALE`; resume M4 and require recovery. If halted
   inside HSEM 1, BUSY may rise until resume. Do not force-unlock it while
   M4 could resume a protected access. Paired reset is the recovery fallback.
5. Build/deploy the deliberate mismatch using
   `pio run -e spooky_box_ipc_mismatch -t deploy`. This uses M7 ABI 1 and M4
   ABI 2. Require `INCOMPATIBLE`, peer version 2, and error 3 (bad version),
   not `UP`. Restore the matching `IpcSmoke` pair afterwards.
6. Repeat paired reset/cold start and check recovery. Save the CLI output,
   image revision, queue high-water/write latency, and workload with results.
   Restore the default environment with
   `pio run -e spooky_box_picoprobe -t deploy` for ordinary sleep/power tests.

The error codes and the meaning of `WAITING`, `STALE` and `INCOMPATIBLE` are defined in
the [contract](../design/ipc-diagnostic-contract.md#error-codes-and-health-semantics).
