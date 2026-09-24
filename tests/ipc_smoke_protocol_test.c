#include "ipc_smoke_protocol.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

/* Unlike assert(), these checks remain active in Release builds. */
#define CHECK(condition) do { if (!(condition)) { \
  fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
  exit(EXIT_FAILURE); } } while (0)

static IpcSmokeFrame response(uint32_t sequence, uint32_t ack)
{
  IpcSmokeFrame frame = {IPC_SMOKE_MAGIC, IPC_SMOKE_VERSION,
    sizeof(IpcSmokeFrame), sequence, 123456U, ack,
    IpcSmoke_Challenge(ack), IPC_SMOKE_VALID};
  return frame;
}

static void test_validation(void)
{
  IpcSmokeFrame frame = {0};
  _Static_assert(sizeof(IpcSmokeFrame) == 32U, "Frame size changed");
  _Static_assert(offsetof(IpcSmokeFrame, peer_error) == 28U, "Frame offsets changed");
  CHECK(IpcSmoke_Validate(&frame) == IPC_SMOKE_EMPTY);
  frame = response(1U, 0U);
  CHECK(IpcSmoke_Validate(&frame) == IPC_SMOKE_VALID);
  frame.magic ^= 1U;
  CHECK(IpcSmoke_Validate(&frame) == IPC_SMOKE_BAD_MAGIC);
  frame = response(1U, 0U);
  ++frame.version;
  CHECK(IpcSmoke_Validate(&frame) == IPC_SMOKE_BAD_VERSION);
  frame = response(1U, 0U);
  --frame.size_bytes;
  CHECK(IpcSmoke_Validate(&frame) == IPC_SMOKE_BAD_SIZE);
  frame = response(0U, 0U);
  CHECK(IpcSmoke_Validate(&frame) == IPC_SMOKE_BAD_SEQUENCE);
  CHECK(IpcSmoke_NextSequence(UINT32_MAX) == 1U);
  CHECK(IpcSmoke_NextSequence(0U) == 1U);
}

static void test_handshake_and_stale(void)
{
  IpcSmokeHealth health = {0};
  IpcSmokeFrame frame = {0};
  IpcSmoke_Observe(&health, &frame, 100U, 0U, true);
  CHECK(IpcSmoke_Link(&health, 100U) == IPC_SMOKE_WAITING);
  frame = response(1U, 0U);
  IpcSmoke_Observe(&health, &frame, 200U, 1U, true);
  CHECK(IpcSmoke_Link(&health, 200U) == IPC_SMOKE_WAITING);
  frame = response(2U, 1U);
  IpcSmoke_Observe(&health, &frame, 300U, 2U, true);
  CHECK(IpcSmoke_Link(&health, 300U) == IPC_SMOKE_UP);
  CHECK(health.round_trips == 1U);
  /* Rereading an unchanged mailbox must not extend its freshness. */
  IpcSmoke_Observe(&health, &frame, 2200U, 3U, true);
  CHECK(health.round_trips == 1U);
  CHECK(IpcSmoke_Link(&health, 2299U) == IPC_SMOKE_UP);
  CHECK(IpcSmoke_Link(&health, 2300U) == IPC_SMOKE_STALE);
  frame = response(3U, 2U);
  IpcSmoke_Observe(&health, &frame, 2400U, 3U, true);
  CHECK(IpcSmoke_Link(&health, 2400U) == IPC_SMOKE_UP);
  /* A moving peer heartbeat alone is insufficient if acknowledgements stop. */
  frame.sequence = 4U;
  IpcSmoke_Observe(&health, &frame, 4400U, 4U, true);
  CHECK(IpcSmoke_Link(&health, 4400U) == IPC_SMOKE_STALE);
}

static void test_bad_peer_and_recovery(void)
{
  IpcSmokeHealth health = {0};
  IpcSmokeFrame frame = response(1U, 1U);
  ++frame.version;
  IpcSmoke_Observe(&health, &frame, 100U, 1U, true);
  CHECK(IpcSmoke_Link(&health, 100U) == IPC_SMOKE_INCOMPATIBLE);
  CHECK(!health.seen_peer);
  frame = response(2U, 1U);
  frame.payload ^= 1U;
  IpcSmoke_Observe(&health, &frame, 200U, 1U, true);
  CHECK(health.error == IPC_SMOKE_BAD_ECHO);
  CHECK(health.round_trips == 0U);
  frame = response(3U, 2U); /* Cannot acknowledge a future publication. */
  IpcSmoke_Observe(&health, &frame, 300U, 1U, true);
  CHECK(health.error == IPC_SMOKE_BAD_SEQUENCE);
  frame = response(4U, 1U);
  frame.peer_error = IPC_SMOKE_BAD_VERSION;
  IpcSmoke_Observe(&health, &frame, 400U, 1U, true);
  CHECK(IpcSmoke_Link(&health, 400U) == IPC_SMOKE_INCOMPATIBLE);
  frame = response(5U, 2U);
  IpcSmoke_Observe(&health, &frame, 500U, 2U, true);
  CHECK(IpcSmoke_Link(&health, 500U) == IPC_SMOKE_UP);
  /* M4 validates the header/ack but accepts M7's new challenge, not an echo. */
  frame.payload = IpcSmoke_Challenge(5U);
  IpcSmoke_Observe(&health, &frame, 600U, 2U, false);
  CHECK(health.error == IPC_SMOKE_VALID);
}

static void test_wrap_and_clock_epochs(void)
{
  IpcSmokeHealth health = {0};
  IpcSmokeFrame frame = response(UINT32_MAX, UINT32_MAX);
  frame.uptime_ms = 42U; /* Peer tick epoch has no bearing on local age. */
  IpcSmoke_Observe(&health, &frame, UINT32_MAX - 999U, UINT32_MAX, true);
  CHECK(IpcSmoke_Link(&health, 999U) == IPC_SMOKE_UP);
  CHECK(IpcSmoke_Link(&health, 1000U) == IPC_SMOKE_STALE);
  frame = response(1U, 1U);
  IpcSmoke_Observe(&health, &frame, 1100U, 1U, true);
  CHECK(IpcSmoke_Link(&health, 1100U) == IPC_SMOKE_UP);
  CHECK(health.round_trips == 2U);
}

int main(void)
{
  test_validation();
  test_handshake_and_stale();
  test_bad_peer_and_recovery();
  test_wrap_and_clock_epochs();
  puts("IPC protocol: validation, handshake, stale detection, errors, recovery, wrap passed");
  return EXIT_SUCCESS;
}
