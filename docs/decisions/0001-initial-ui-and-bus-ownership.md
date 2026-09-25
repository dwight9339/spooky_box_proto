# 0001. Initial UI and bus ownership

- **Status:** Accepted
- **Date:** 2026-09-24. First recorded in `docs/architecture.md`; extracted into this
  record 2026-09-25 without changing the decision.
- **Supersedes:** none
- **Beads:** `full_spooky_proto-8lw` (service boundaries)

## Context

The UI matrix, magnetometer and fuel gauge all use the I2C2 controller. Buttons,
encoders, direct LEDs and the SSD1309 display have working M7 bring-up drivers. The
target architecture gives the M4 input scanning and UI presentation. A split that let
both cores run HAL calls on one controller, or hand peripherals over at runtime, would
be invalid (Constitution Principle III).

## Decision

For the first product slice, M7 remains the sole owner of I2C2, PB10/PB11, the
matrix at `0x30`, the magnetometer at `0x35`, and the fuel gauge at `0x55`.
Matrix enable and magnetometer interrupt pins belong to that M7 service as
well. The matrix therefore remains a bounded M7 renderer driven directly from
authoritative semantic state. Moving only the matrix would create two HAL
owners for one controller; moving the whole I2C2 domain would also move sensor
acquisition across the core boundary before the product protocol is proven.

After the UI bring-up module is split and product IPC passes its restart and
staleness gates, M4 takes ownership of the button and encoder GPIOs, the direct
LED GPIOs, and the SSD1309 display including SPI6, chip select, data/command and
reset. This is a staged build-time transfer, not a runtime handoff: a peripheral
and its pins have exactly one owner in any image pair. Until that milestone,
the current M7 bring-up driver owns all of them. The generated M4 TIM16/PF6
configuration currently overlaps the M7 BTN0 LED bring-up and must be removed
or reassigned as part of CubeMX reconciliation before M4 UI integration.

M4 owns electrical input processing only: 1 kHz sampling, switch debounce,
quadrature decoding, monotonic event sequence/time, and current held-state
reporting. It emits press/release transitions and signed encoder detents. M7 is
the sole gesture resolver. It uses the context in which a press began to decide
clicks, holds, Shift and multi-button chords, then routes the resulting product
command. Peer restart, queue overflow or stale input must cause a release-all
reconciliation so Shift or PTT cannot remain latched.

I2C2 currently uses polling HAL calls from the foreground and has no IRQ or DMA
ownership to transfer. Its D2PCLK1 clock selection, peripheral reset and GPIO
alternate functions remain M7 responsibilities. The M4 UI build must keep a
1 kHz time base active instead of the normal sleeping-M4 tick policy. SPI6 uses
D3PCLK1 and blocking foreground transfers today; its M4 renderer must bound
update work and lower frame rate before it can affect recording. The framebuffer
stays local to M4. Cross-core state and events use the versioned IPC region and
its explicit barriers/cache policy rather than shared driver objects or buffers.

## Consequences

A later measurement may justify moving the entire I2C2 domain to M4, including
sensor acquisition and matrix output, but that is a new ownership decision with
sensor-publication and recovery tests. A separate RP2040-style coprocessor is
not part of the default plan.

- The M7 matrix renderer stays bounded and is driven from authoritative semantic state.
- M4 UI integration is blocked on the UI driver split, CubeMX reconciliation of
  TIM16/PF6, and product IPC qualification (restart and staleness gates).

## Evidence

- UI bring-up and the SSD1309 mapping: [BRINGUP.md](../../BRINGUP.md).
- IPC status before product IPC: [IPC smoke test](../procedures/ipc-smoke-test.md) and
  [September 24 results](../evidence/2026-09-24-bench-results.md).
