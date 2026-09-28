# 0007. One bounded M7 event queue with run-to-completion dispatch

- **Status:** Accepted 2026-09-28
- **Date:** 2026-09-28
- **Supersedes:** none
- **Beads:** `full_spooky_proto-8lw.13`

## Context

[Decision 0006](0006-statesmith-behavior-model.md) item 13 forbids generated machines
from dispatching into one another synchronously. Commands and cross-region events must
pass through a bounded M7 event queue with declared capacity, overrun policy and
counters. This record settles that queue and the order in which machines see events.

Constraints:

- Principle III: the CLI and the physical controls use the same command handling. Lost
  input must end in release-all reconciliation, never in a latched control.
- Principle IV: every queue declares its capacity and overrun policy, and its capacity
  and overrun counters are visible in diagnostics. Sizes come from measurement or
  explicit arithmetic and stay configurable until bench trials fix them.
- Principle I: nothing on this path may delay audio capture or SD writes.
- Today every M7 service runs in one cooperative foreground loop with a 5 ms delay
  per pass. The USB CLI, the diagnostic IPC poll and every timer are foreground code.
  ISRs and DMA callbacks already hand work to foreground services through their own
  bounded buffers, such as the radio and PDM block queues.
- The product IPC (`full_spooky_proto-54w.4`) is polled in the foreground too. It will
  deliver M4 input events and report IPC restart, overflow and staleness.
- Presentation receives published state and domain events through the outbound IPC,
  not from M7 machines.

## Options

1. **Direct calls between machines.** Rejected by decision 0006: a machine's action
   would re-enter another machine mid-transition.
2. **One queue per producer, served round-robin.** Rejected. Events from different
   producers lose their relative order, so a CLI command and a completion can be
   seen in the opposite order from how they happened. It also multiplies buffers and
   counters.
3. **Priority queues, with service events ahead of input.** Rejected. Reordering breaks
   causality between an input and the completion it caused. Protecting service events
   from an input flood needs only reserved capacity, not reordering.
4. **Allow ISRs to post, under a critical section.** Deferred. No current event source
   runs in an ISR, and interrupt masking on this path would add latency to audio
   interrupts. Revisit when a measured latency needs it.
5. **One FIFO with one consumer, foreground producers only, admission by class with
   reserved capacity, and a bounded dispatch per loop pass.** Chosen.

## Decision

**The queue**

1. The M7 has one application event queue: a fixed-capacity FIFO of fixed-size
   entries. Each entry carries an event type, its admission class, a sequence number,
   the post time in milliseconds and two 32-bit arguments. The layout is fixed-width
   and checked by static assertions.
2. Only foreground code posts. ISRs and DMA callbacks keep handing work to their
   foreground service through that service's own bounded buffer, and the service
   posts. Posting never blocks and returns whether the event was admitted.
3. One foreground service drains the queue. Each call dispatches at most the events
   that were queued when the call started. Events posted during dispatch wait for the
   next call, so a chain of events cannot starve the rest of the loop.

**Admission and overrun**

4. Every event belongs to one of three classes:
   - *Input*: input events from the input adapter.
   - *External command*: commands from the CLI.
   - *Internal*: everything else, including service events from foreground services,
     timer expiries, commands issued by machines, cross-region events, and the
     reconcile event.
5. Input and external commands are admitted only while more than the reserved number
   of slots is free. Internal events may use the reserved slots. Overrun always
   rejects the newest event, never an event already queued.
6. A rejected input sets a reconcile flag. While it is set, further input is rejected.
   As soon as an internal slot is free, the adapter posts one reconcile event, and
   then clears the flag. Every machine that tracks held controls releases them, so a
   dropped release cannot leave a control latched (Principle III).
7. A rejected external command is answered to its source as busy, for example
   `ERR BUSY` on the CLI. It has no other effect.
8. A rejected internal event is a design error, because the reserve is sized so it
   cannot happen. It is counted and recorded as a diagnostic fault event. Host tests
   and the recording regression check that this counter stays zero.

**Dispatch order**

9. Dispatch is run-to-completion: one event is fully handled, including every machine
   it is routed to, before the next event starts.
10. A static routing table maps each event type to the machines that take it. When
    an event goes to more than one machine, they run in the fixed order Session,
    Radio, Context, InputResolution, which puts recording first (Principle I).
11. An `in(Region.State)` guard reads the other machine's state at the moment the
    guard runs. That state reflects every event dispatched before, and the current
    event's effect on machines earlier in the routing order. It never reflects events
    still in the queue. Machines that must react when another region changes state
    take that region's change event rather than relying on timing. InputResolution
    already does this by withdrawing its prompt on a session change.
12. A machine posts cross-region events and commands through its port. Nothing in a
    port calls another machine's service interface. Domain events for presentation go
    to the outbound IPC, which has its own bounds under `full_spooky_proto-54w.4`.

**Sizes and observability**

13. Starting sizes: a capacity of 32 entries of 20 bytes, 640 bytes in all, with 8
    reserved for internal events. Both stay configurable until bench trials fix them
    (Principle IV). The arithmetic:
    - Humans produce few discrete inputs: at most 6 presses and 6 releases, plus one
      coalesced detent report per encoder, 16 events in a loop stall of 50 ms, the
      point where diagnostics already record a stall.
    - That fits in the 24 slots open to input and commands, with room for a CLI
      command.
    - One event's handling rarely posts more than 2 internal events. Posts during
      dispatch wait for the next call, so the reserve of 8 covers four events in the
      same call, each posting two.
14. Diagnostics report the capacity, reserve, current count, high-water mark, events
    posted and dispatched, rejections per class, reconcile events posted, and the
    longest time an event waited between post and dispatch. A `DIAG QUEUE` command
    returns them on one line. The internal-rejection fault also appears in `DIAG`
    history.

## Consequences

- Machines stay independent: each is driven only by the dispatcher, through its port.
  Host tests can drive the queue and the machines together without hardware.
- Cross-region reactions are delayed until the next dispatch call. With a 5 ms loop,
  that is one pass, well below what a person notices.
- The queue exists only on the M7. The product IPC (`full_spooky_proto-54w.4`) feeds
  it with input events and reports IPC restart, overflow and staleness as a reconcile
  event.
- An ISR cannot post directly. A future interrupt-driven producer needs a new
  decision that measures its latency and defines its critical section.
- The first implementation is portable C in `CM7/App/` with host tests. It is wired
  into the foreground loop with the first machine that uses it,
  `full_spooky_proto-8lw.14`. Until then the queue changes nothing in firmware behavior.
- Revisit if bench trials show a high-water mark near capacity, internal rejections,
  or waits long enough to be noticed, or if a producer must run in interrupt context.

## Evidence

None yet. Host tests for ordering, admission, reconcile after rejected input, bounded
dispatch and counters belong to `full_spooky_proto-8lw.13`. Bench high-water and wait
figures belong to the recording regression once the queue is wired.
