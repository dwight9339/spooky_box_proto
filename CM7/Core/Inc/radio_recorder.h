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
/* Closes and deletes a prepared file that capture never used. */
void RadioRecorder_DiscardFile(void);
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
/* True only while DMA producers may enqueue recording blocks. Final file sync
 * and close keep IsActive true but return false here. */
bool RadioRecorder_IsCapturing(void);
void RadioRecorder_Stop(void);

#if defined(SPOOKY_DEMO)
/* Demo-only rolling capture (decision 0011 item 14, p04.5; demo_rolling.c).
 * Outside a session the recorder streams into the rolling segments; a session
 * start turns it off and it restarts when the session ends. */
typedef struct
{
  bool enabled;
  bool rolling;               /* streaming into the rolling segments now */
  uint8_t radio_high_water;   /* since the stream started */
  uint8_t pdm_high_water;
  uint32_t max_write_ms;
  uint32_t blocks;            /* written since the stream started */
  uint32_t starts;            /* streams started since boot */
} RadioRecorderRollingStats;

/* Off stops rolling at once and keeps it off; false (nothing changes) while a
 * capture save is in progress. On lets it start on a later pass. */
bool RadioRecorder_SetRolling(bool enabled);
void RadioRecorder_GetRollingStats(RadioRecorderRollingStats *stats);
/* Capturing for a session, as opposed to rolling capture. The demo image keys
 * the I2C2 sensor guards on this, so EMF stays live outside sessions. */
bool RadioRecorder_IsSessionCapturing(void);
#endif

#ifdef __cplusplus
}
#endif

#endif
