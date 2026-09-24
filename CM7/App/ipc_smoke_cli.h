#ifndef SPOOKY_IPC_SMOKE_CLI_H
#define SPOOKY_IPC_SMOKE_CLI_H
#include <stdbool.h>
/* Input is the existing CLI's uppercase, trimmed command. */
bool IpcSmokeCli_HandleCommand(const char *command);
#endif
