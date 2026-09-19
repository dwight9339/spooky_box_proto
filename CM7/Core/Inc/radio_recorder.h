#ifndef RADIO_RECORDER_H
#define RADIO_RECORDER_H

#include <stdbool.h>
#include <stdint.h>
#include "stm32h7xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

void RadioRecorder_Init(DFSDM_Filter_HandleTypeDef *filter);
bool RadioRecorder_HandleCommand(const char *command, bool radio_ready);
void RadioRecorder_Service(void);
void RadioRecorder_OnRadioSamples(const int16_t *samples,
                                  uint32_t sample_count);
void RadioRecorder_NotifyRadioError(void);
bool RadioRecorder_IsActive(void);
void RadioRecorder_Stop(void);

#ifdef __cplusplus
}
#endif

#endif
