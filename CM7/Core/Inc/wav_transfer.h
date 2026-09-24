#ifndef WAV_TRANSFER_H
#define WAV_TRANSFER_H

#include <stdbool.h>
#include "stm32h7xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

void WavTransfer_Init(SD_HandleTypeDef *sd);
bool WavTransfer_HandleCommand(const char *command);
void WavTransfer_Service(void);
bool WavTransfer_IsActive(void);
void WavTransfer_Stop(void);

#ifdef __cplusplus
}
#endif

#endif /* WAV_TRANSFER_H */
