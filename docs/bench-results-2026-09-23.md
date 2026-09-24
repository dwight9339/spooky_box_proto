# Bench observations: 2026-09-23

User-reported hardware observations and supplied CLI output. These are not
automated Spooky Bench results. The user identified the IPC variants below, but
exact STM32 image hashes and the flashed Pico UF2 hash were not supplied. The
recording run's preset was not identified. Attribute results only to the stated
workloads/variants; full regression acceptance remains pending.

## Source checkpoints

The work was subsequently committed as target firmware `799bdb9` in
`full_spooky_proto` and probe/host tooling `ce039ca` in the sibling `spookyprobe`
repo. These identify source checkpoints, not independently verified hashes of
the binaries used in the user-reported tests. Target editor configuration and
probe editor/SDK setup changes were deliberately left outside those commits.

Final offline checks before committing passed: target native suites 2/2, probe
native suite 1/1, and probe Python tests 14/14. Debug, Release, IpcSmoke and
IpcMismatch target firmware builds had passed earlier in the implementation
session. Outstanding hardware checks below remain outstanding after committing.

## Display and system power

The user flashed the new Spookyprobe UF2 to the Pico. With the Battery
Babysitter's manual SYSOFF switch described as "off" and the assembly running
from USB, `UI DISPLAY TEST` illuminated the display but produced a whirring sound
somewhere on the board and loss of device USB CDC. CDC returned only after a
power cycle. The sound was reported as coming from the board, not headphones.

After changing SYSOFF to the position described as "on", reconnecting USB, and
repeating the display test, the user reported normal operation. These labels
record the user's switch positions, not a verified signal polarity or switch
truth table. No rail waveforms, current measurements, reset cause, or component
source of the noise were captured. Power-path involvement is suggested; the
electrical cause is not established.

## Recording with the display on

The user reported successful recording with the display on and supplied:

```text
record start 60
OK RECORD START file=REC001.WAV duration=60s format=48000Hz/16-bit/3ch [radio-L,radio-R,mic]
RECORD progress=4.9s queues=0/8,0/8 max-write=25ms
RECORD progress=9.9s queues=0/8,0/8 max-write=25ms
RECORD progress=15.0s queues=0/8,0/8 max-write=25ms
RECORD progress=20.0s queues=0/8,0/8 max-write=25ms
RECORD progress=25.0s queues=0/8,0/8 max-write=27ms
RECORD progress=30.1s queues=0/8,0/8 max-write=27ms
RECORD progress=35.1s queues=0/8,0/8 max-write=27ms
RECORD progress=40.1s queues=1/8,0/8 max-write=27ms
RECORD progress=45.1s queues=0/8,0/8 max-write=27ms
RECORD progress=50.1s queues=0/8,0/8 max-write=27ms
RECORD progress=55.2s queues=0/8,0/8 max-write=27ms
OK RECORD PASS file=REC001.WAV frames=2883584 bytes=17301504 audio=60.074s elapsed=60116ms
RECORD DIAG queues radio=1/8 pdm=1/8 max-write=27ms peaks=1801,1798,197
```

The firmware reports completion/finalization without a recorder abort, radio and
PDM queue high-water marks of 1/8 each, and a maximum recorder data-block write
duration of 27 ms. This supports the short recording/display coexistence check.
The nonzero peaks show recorded sample activity; they do not establish channel
quality, absence of glitches, or sample synchronization. The WAV itself has not
been provided for independent inspection.

Still pending: WAV channel/playback inspection, LOG STATUS and DIAG
STATUS/LAST/DUMP evidence, confirmation of concurrent Pico capture, longer
recording, reconnect/overload and sleep checks. IPC evidence and its remaining
checks are recorded below. Record exact firmware identities with subsequent results.

## Initial IPC hardware results

User-reported `IPC_SMOKE` result:

```text
ipc status
OK IPC LINK=UP VERSION=1 PEER_VERSION=1 TX=165 RX=175 ACK=164 ROUNDTRIPS=164 PEER_SEEN=1 ACK_SEEN=1 RX_AGE=42 ACK_AGE=42 ERROR=0 PEER_ERROR=0 BUSY=0
```

The initial healthy-link check passes: both ABI versions are 1, peer and
acknowledgement have been seen, the link is UP, and both reported error fields
are zero. ROUNDTRIPS=164 provides evidence of repeated validated acknowledgement
progress before this snapshot. RX_AGE and ACK_AGE are local observation ages of
42 ms, not measured round-trip latency. TX and RX need not match: each core
publishes independently. This single snapshot does not establish sustained
progress across a timed observation or under a recording workload.

User-reported `IPC_MISMATCH` result:

```text
ipc status
OK IPC LINK=INCOMPATIBLE VERSION=1 PEER_VERSION=2 TX=251 RX=0 ACK=0 ROUNDTRIPS=0 PEER_SEEN=0 ACK_SEEN=0 RX_AGE=0 ACK_AGE=0 ERROR=3 PEER_ERROR=0 BUSY=0
```

The deliberate version-mismatch check passes: M7 reports ABI 1 versus peer ABI 2,
INCOMPATIBLE, and error 3 (bad version). The peer has not been accepted as valid,
so zero accepted RX/ACK/ROUNDTRIPS, clear SEEN flags, and zero age fields are
expected; they do not mean zero-latency communication. PEER_ERROR=0 does not
override the local version rejection or establish compatible peer health.

## IPC progress, recording, and reboot follow-up

The user subsequently supplied these results using `IPC_SMOKE`:

```text
ipc status
OK IPC LINK=UP VERSION=1 PEER_VERSION=1 TX=534 RX=551 ACK=533 ROUNDTRIPS=533 PEER_SEEN=1 ACK_SEEN=1 RX_AGE=48 ACK_AGE=48 ERROR=0 PEER_ERROR=0 BUSY=0
ipc status
OK IPC LINK=UP VERSION=1 PEER_VERSION=1 TX=582 RX=600 ACK=581 ROUNDTRIPS=581 PEER_SEEN=1 ACK_SEEN=1 RX_AGE=42 ACK_AGE=42 ERROR=0 PEER_ERROR=0 BUSY=0
record start 60
OK RECORD START file=REC002.WAV duration=60s format=48000Hz/16-bit/3ch [radio-L,radio-R,mic]
RECORD progress=4.9s queues=0/8,0/8 max-write=25ms
RECORD progress=9.9s queues=0/8,0/8 max-write=25ms
RECORD progress=15.0s queues=0/8,0/8 max-write=25ms
RECORD progress=20.0s queues=0/8,0/8 max-write=25ms
RECORD progress=25.0s queues=0/8,0/8 max-write=25ms
RECORD progress=30.0s queues=1/8,0/8 max-write=25ms
RECORD progress=35.0s queues=0/8,0/8 max-write=25ms
RECORD progress=40.1s queues=0/8,0/8 max-write=25ms
RECORD progress=45.1s queues=0/8,0/8 max-write=25ms
RECORD progress=50.0s queues=1/8,0/8 max-write=25ms
RECORD progress=55.1s queues=0/8,0/8 max-write=25ms
OK RECORD PASS file=REC002.WAV frames=2883584 bytes=17301504 audio=60.074s elapsed=60115ms
RECORD DIAG queues radio=1/8 pdm=1/8 max-write=25ms peaks=1893,1899,530
```

Both status samples show UP, matching versions and zero errors/busy attempts.
Between samples TX, ACK and ROUNDTRIPS each advanced by 48, and RX advanced by
49. This passes the between-sample progress check; host timestamps were not
supplied, so no measured rate or precise observation interval is claimed.

REC002 completed/finalized in the IpcSmoke build without a reported recorder
abort. Both queue high-water marks were 1/8 and the maximum data write was 25 ms.
This passes the short firmware-level recording check with the IPC experiment
enabled. Display state and simultaneous Pico capture were not stated for this
run. No IPC status during/immediately after recording was supplied, so uninterrupted
IPC progress under recording load is not established by this transcript.

The user additionally reported that IPC status was UP after reboot. Record this
as user-observed reboot recovery; the reboot method, post-reboot counters and
exact image hashes were not supplied. It does not establish the full paired-reset,
cold-start or independent-core recovery matrix.

## IPC before and after REC003

The user supplied this further IpcSmoke recording with status on both sides:

```text
ipc status
OK IPC LINK=UP VERSION=1 PEER_VERSION=1 TX=492 RX=509 ACK=491 ROUNDTRIPS=491 PEER_SEEN=1 ACK_SEEN=1 RX_AGE=54 ACK_AGE=54 ERROR=0 PEER_ERROR=0 BUSY=0
record start 60
OK RECORD START file=REC003.WAV duration=60s format=48000Hz/16-bit/3ch [radio-L,radio-R,mic]
RECORD progress=4.9s queues=0/8,0/8 max-write=21ms
RECORD progress=9.9s queues=0/8,0/8 max-write=21ms
RECORD progress=15.0s queues=0/8,0/8 max-write=25ms
RECORD progress=19.9s queues=1/8,0/8 max-write=25ms
RECORD progress=25.0s queues=0/8,0/8 max-write=25ms
RECORD progress=30.0s queues=0/8,0/8 max-write=25ms
RECORD progress=34.9s queues=1/8,0/8 max-write=49ms
RECORD progress=40.0s queues=0/8,0/8 max-write=49ms
RECORD progress=45.0s queues=0/8,0/8 max-write=49ms
RECORD progress=50.0s queues=1/8,0/8 max-write=49ms
RECORD progress=55.0s queues=0/8,0/8 max-write=49ms
OK RECORD PASS file=REC003.WAV frames=2883584 bytes=17301504 audio=60.074s elapsed=60117ms
RECORD DIAG queues radio=1/8 pdm=1/8 max-write=49ms peaks=2298,2294,670
ipc status
OK IPC LINK=UP VERSION=1 PEER_VERSION=1 TX=2065 RX=2142 ACK=2064 ROUNDTRIPS=2064 PEER_SEEN=1 ACK_SEEN=1 RX_AGE=24 ACK_AGE=24 ERROR=0 PEER_ERROR=0 BUSY=0
```

The post-record link check passes: UP with matching versions, both SEEN flags,
zero reported errors/busy attempts, and forward progress since the pre-record
snapshot (TX/ACK/ROUNDTRIPS +1573; RX +1633). Host timestamps and the interval
between commands were not supplied; these deltas are not a measured 60-second
rate. Endpoints alone do not exclude a transient stale interval during recording.

REC003 completed/finalized without a reported recorder abort; both queues peaked
at 1/8 and maximum data-write time was 49 ms. This adds successful post-record
IPC health to the short recording evidence. It does not replace WAV inspection
or establish a worst-case SD latency bound.

Remaining checks: longer recording, observation of IPC health during the load,
explicit cold-start evidence, and M4 halt/stale/resume if the debugger supports
independent halt. WAV playback/channel inspection and LOG/DIAG evidence remain
outstanding. Restore normal Debug for charging-sleep checks; experiment builds
intentionally reject SLEEP START.
