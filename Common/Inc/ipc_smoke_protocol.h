#ifndef SPOOKY_IPC_SMOKE_PROTOCOL_H
#define SPOOKY_IPC_SMOKE_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>

/* This is a diagnostic ABI, not the future UI/application protocol. */
#define IPC_SMOKE_MAGIC 0x53504B59U
#ifndef IPC_SMOKE_VERSION
#define IPC_SMOKE_VERSION 1U
#endif
#define IPC_SMOKE_PERIOD_MS 100U
#define IPC_SMOKE_STALE_MS 2000U

typedef struct
{
  uint32_t magic;
  uint32_t version;
  uint32_t size_bytes;
  uint32_t sequence;
  uint32_t uptime_ms;
  uint32_t ack_sequence;
  uint32_t payload;
  uint32_t peer_error;
} IpcSmokeFrame;

_Static_assert(sizeof(IpcSmokeFrame) == 32U, "IPC smoke ABI must be 32 bytes");

typedef enum
{
  IPC_SMOKE_VALID = 0,
  IPC_SMOKE_EMPTY,
  IPC_SMOKE_BAD_MAGIC,
  IPC_SMOKE_BAD_VERSION,
  IPC_SMOKE_BAD_SIZE,
  IPC_SMOKE_BAD_SEQUENCE,
  IPC_SMOKE_BAD_ECHO
} IpcSmokeError;

typedef enum
{
  IPC_SMOKE_WAITING = 0,
  IPC_SMOKE_UP,
  IPC_SMOKE_STALE,
  IPC_SMOKE_INCOMPATIBLE
} IpcSmokeLink;

typedef struct
{
  uint32_t peer_sequence;
  uint32_t ack_sequence;
  uint32_t peer_progress_ms;
  uint32_t ack_progress_ms;
  uint32_t round_trips;
  IpcSmokeError error;
  uint32_t peer_error;
  bool seen_peer;
  bool seen_ack;
} IpcSmokeHealth;

uint32_t IpcSmoke_NextSequence(uint32_t sequence);
uint32_t IpcSmoke_Challenge(uint32_t sequence);
IpcSmokeError IpcSmoke_Validate(const IpcSmokeFrame *frame);
/* Timeouts use local observation time, never the other core's tick epoch. */
void IpcSmoke_Observe(IpcSmokeHealth *health, const IpcSmokeFrame *peer,
                      uint32_t now_ms, uint32_t last_sent, bool check_echo);
IpcSmokeLink IpcSmoke_Link(const IpcSmokeHealth *health, uint32_t now_ms);

#endif
