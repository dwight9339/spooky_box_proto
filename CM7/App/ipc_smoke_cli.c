#include "ipc_smoke_cli.h"
#include "usb_test.h"
#include <string.h>
#if defined(SPOOKY_IPC_SMOKE)
#include "ipc_smoke.h"
#include "main.h"
#include <stdio.h>
#endif

bool IpcSmokeCli_HandleCommand(const char *command)
{
  if (strcmp(command, "IPC STATUS") != 0) return false;
#if defined(SPOOKY_IPC_SMOKE)
  const IpcSmokeDiagnostics *d = IpcSmoke_GetDiagnostics();
  const uint32_t now = HAL_GetTick();
  const char *const names[] = {"WAITING", "UP", "STALE", "INCOMPATIBLE"};
  char response[384];
  (void)snprintf(response, sizeof(response),
    "OK IPC LINK=%s VERSION=%lu PEER_VERSION=%lu TX=%lu RX=%lu ACK=%lu "
    "ROUNDTRIPS=%lu PEER_SEEN=%u ACK_SEEN=%u RX_AGE=%lu ACK_AGE=%lu "
    "ERROR=%u PEER_ERROR=%lu BUSY=%lu\r\n",
    names[IpcSmoke_Link(&d->health, now)],
    (unsigned long)IPC_SMOKE_VERSION, (unsigned long)d->peer.version,
    (unsigned long)d->local_sequence, (unsigned long)d->health.peer_sequence,
    (unsigned long)d->health.ack_sequence, (unsigned long)d->health.round_trips,
    (unsigned int)d->health.seen_peer, (unsigned int)d->health.seen_ack,
    (unsigned long)(d->health.seen_peer ? now - d->health.peer_progress_ms : 0U),
    (unsigned long)(d->health.seen_ack ? now - d->health.ack_progress_ms : 0U),
    (unsigned int)d->health.error, (unsigned long)d->health.peer_error,
    (unsigned long)d->lock_busy);
  (void)UsbTest_SendText(response);
#else
  (void)UsbTest_SendText("OK IPC DISABLED; build preset IpcSmoke for bench test\r\n");
#endif
  return true;
}
