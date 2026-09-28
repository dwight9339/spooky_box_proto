#ifndef WAV_TRANSFER_H
#define WAV_TRANSFER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void WavTransfer_Init(void);
bool WavTransfer_HandleCommand(const char *command);
void WavTransfer_Service(void);
bool WavTransfer_IsActive(void);
void WavTransfer_Stop(void);

#ifdef __cplusplus
}
#endif

#endif /* WAV_TRANSFER_H */
