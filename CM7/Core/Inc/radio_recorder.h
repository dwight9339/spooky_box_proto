#ifndef RADIO_RECORDER_H
#define RADIO_RECORDER_H

#include <stdbool.h>
#include <stdint.h>
#include "stm32h7xx_hal.h"
#include "sm/session_port.h"

#ifdef __cplusplus
extern "C" {
#endif

void RadioRecorder_Init(DFSDM_Filter_HandleTypeDef *filter);
bool RadioRecorder_HandleCommand(const char *command, bool radio_ready);
bool RadioRecorder_CanStart(uint32_t seconds, bool radio_ready);
bool RadioRecorder_OpenFile(uint32_t seconds);
bool RadioRecorder_StartCapture(void);
void RadioRecorder_RequestStop(void);
bool RadioRecorder_TargetReached(void);
void RadioRecorder_StopCapture(void);
bool RadioRecorder_FinalizeFile(void);
void RadioRecorder_PublishSessionEvent(SesPublished event);
void RadioRecorder_SendStorageStatus(void);
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
