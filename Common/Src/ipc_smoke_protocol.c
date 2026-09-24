#include "ipc_smoke_protocol.h"

uint32_t IpcSmoke_NextSequence(uint32_t sequence)
{
  ++sequence;
  return sequence == 0U ? 1U : sequence;
}

uint32_t IpcSmoke_Challenge(uint32_t sequence)
{
  return sequence ^ 0xA5C35A3CU;
}

IpcSmokeError IpcSmoke_Validate(const IpcSmokeFrame *frame)
{
  if (frame->magic == 0U) return IPC_SMOKE_EMPTY;
  if (frame->magic != IPC_SMOKE_MAGIC) return IPC_SMOKE_BAD_MAGIC;
  if (frame->version != IPC_SMOKE_VERSION) return IPC_SMOKE_BAD_VERSION;
  if (frame->size_bytes != sizeof(*frame)) return IPC_SMOKE_BAD_SIZE;
  if (frame->sequence == 0U) return IPC_SMOKE_BAD_SEQUENCE;
  return IPC_SMOKE_VALID;
}

void IpcSmoke_Observe(IpcSmokeHealth *health, const IpcSmokeFrame *peer,
                      uint32_t now_ms, uint32_t last_sent, bool check_echo)
{
  health->error = IpcSmoke_Validate(peer);
  if (health->error != IPC_SMOKE_VALID) return;
  health->peer_error = peer->peer_error;
  if (peer->ack_sequence != 0U)
  {
    /* Reject an acknowledgement ahead of our last publication. Unsigned
     * distance also handles sequence wrap (zero is reserved for no ack). */
    if ((last_sent == 0U) ||
        ((uint32_t)(last_sent - peer->ack_sequence) >= 0x80000000U))
    {
      health->error = IPC_SMOKE_BAD_SEQUENCE;
      return;
    }
    if (check_echo && (peer->payload != IpcSmoke_Challenge(peer->ack_sequence)))
    {
      health->error = IPC_SMOKE_BAD_ECHO;
      return;
    }
  }
  if (!health->seen_peer || (peer->sequence != health->peer_sequence))
  {
    health->seen_peer = true;
    health->peer_sequence = peer->sequence;
    health->peer_progress_ms = now_ms;
  }
  if ((peer->ack_sequence != 0U) &&
      (!health->seen_ack || (peer->ack_sequence != health->ack_sequence)))
  {
    health->seen_ack = true;
    health->ack_sequence = peer->ack_sequence;
    health->ack_progress_ms = now_ms;
    ++health->round_trips;
  }
}

IpcSmokeLink IpcSmoke_Link(const IpcSmokeHealth *health, uint32_t now_ms)
{
  if ((health->error > IPC_SMOKE_EMPTY) ||
      (health->peer_error > IPC_SMOKE_EMPTY)) return IPC_SMOKE_INCOMPATIBLE;
  if (!health->seen_peer) return IPC_SMOKE_WAITING;
  if ((now_ms - health->peer_progress_ms) >= IPC_SMOKE_STALE_MS)
    return IPC_SMOKE_STALE;
  if (!health->seen_ack) return IPC_SMOKE_WAITING;
  if ((now_ms - health->ack_progress_ms) >= IPC_SMOKE_STALE_MS)
    return IPC_SMOKE_STALE;
  return IPC_SMOKE_UP;
}
