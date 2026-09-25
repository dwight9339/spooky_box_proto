# 0002. Windows-first Spooky Bench host

- **Status:** Accepted
- **Date:** 2026-09-23. First recorded in the Spooky Bench plan's "Decision in brief";
  extracted into this record 2026-09-25 without changing the decision.
- **Supersedes:** none
- **Beads:** `full_spooky_proto-5yv` (bench extensions)

## Context

The project needs unattended hardware testing (flash, reset, UART capture, target
diagnostics) that developers and agents invoke the same way. Before this decision,
two assumptions were in circulation: that a Raspberry Pi host was required first, and
that the M4 owned diagnostics. The repository does not support the second: UART7,
diagnostic history and device USB CDC are on the M7, and the M4 sleeps in normal
builds.

## Decision

Build a native, Windows-first Python `spookybench` command surface in this
repository, with portable test logic and explicit operating-system adapters.

- Wrap the existing CMSIS-DAP/OpenOCD deployment recipe and reuse the sibling Spooky
  Probe capture and decoder package.
- Accept prebuilt, matched M7/M4 artifacts. Compiling is a separate concern, even when
  builds and bench runs share the same PC.
- Order of work: device discovery and status, bounded UART capture and the existing
  diagnostic queries first. Then probe, paired flash and reset. Automate the existing
  `IpcSmoke` experiment only after those operations pass hardware acceptance.
- Do not require trace, power switching, CI or a core-ownership migration first.
- Keep UART7, diagnostic history and USB CDC on the M7. Never initialize UART7 from
  both cores, and never make the M7 wait for a diagnostic consumer. An M4 diagnostic
  aggregator would need its own decision and a validated ownership handoff.
- The bench runs on the development PC with both USB data connections: OpenOCD to the
  Pico CMSIS-DAP for SWD, Pico CDC for UART7 text, and the H755's own USB CDC for
  diagnostic commands. UART7 has no command receiver.
- Use a local Python environment and a PC bench profile, with no Pi, WSL, SSH, daemon
  or custom agent integration. Agents run the same CLI as developers and consume its
  JSON results and saved artifacts.

## Consequences

- A Raspberry Pi is a later option for a permanently connected bench, using the same
  command and result contract. It is not a prerequisite.
- Initial unattended runs depend on the PC staying awake and connected. Host sleep or
  disconnect is not a target failure.
- A Pico-only USB connection cannot run `DIAG STATUS` or `IPC STATUS`.

## Evidence

- Original audit and rationale:
  [2026-09-23 Spooky Bench plan](../history/2026-09-23-spooky-bench-plan.md).
- Implementation and live acceptance: [setup](../procedures/spooky-bench-setup.md),
  [controls](../procedures/spooky-bench-controls.md) and
  [September 24 results](../evidence/2026-09-24-bench-results.md).
