#ifndef SPOOKY_AUDIO_PATH_SERVICE_H
#define SPOOKY_AUDIO_PATH_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "audio_timeline.h"
#include "stm32h7xx_hal.h"

/* Fault bits keep the legacy runtime flag values reported on UART. */
#define AUDIO_PATH_FAULT_RADIO_RX      0x1U /* SAI2 A radio capture */
#define AUDIO_PATH_FAULT_MONITOR_TX    0x2U /* SAI1 A headphone monitor */
#define AUDIO_PATH_FAULT_CODEC_OUTPUT  0x4U
#define AUDIO_PATH_FAULT_VOLUME_ADC    0x8U

typedef struct
{
  bool running;
  bool stream_enabled;
  uint32_t fault_flags;
  uint32_t rx_half_count;
  uint32_t rx_full_count;
} AudioPathStatus;

/* Owns the radio capture DMA (SAI2 A, DMA1 Stream 4), the monitor TX DMA on
 * SAI1 A, both DMA buffers and the SAI callbacks. Each received half-buffer is
 * first handed unchanged to the recorder (raw capture), then rendered into the
 * separate monitor buffer; monitor processing cannot alter recorded samples.
 * SAI1 A clocking is configured by the codec service before StartMonitor. */
bool AudioPath_Init(SAI_HandleTypeDef *radio_rx_sai,
                    SAI_HandleTypeDef *monitor_tx_sai);
bool AudioPath_StartCapture(void);
bool AudioPath_StartMonitor(void);

/* Gates both raw fan-out and the monitor copy, as during a receiver change. */
void AudioPath_SetStreamEnabled(bool enabled);
void AudioPath_MarkStopped(void);
void AudioPath_ReportFault(uint32_t fault_flags);
void AudioPath_Stop(void);
bool AudioPath_IsRunning(void);
bool AudioPath_GetStatus(AudioPathStatus *status);

/* Radio stream timeline (decision 0012). Each AudioPath_StartCapture begins a
 * new epoch at DMA index 0; positions count radio frames since then. */
typedef struct
{
  AudioTimelinePosition mic_start;   /* radio frame in progress at the snapshot */
  AudioTimelineAlignment alignment;  /* origin = mic_start + mic latency */
  uint32_t skip_frames;              /* drop from the next delivered half */
  uint32_t monitor_phase_frames;     /* SAI2 RX minus SAI1 TX DMA index */
  bool monitor_phase_valid;
} AudioPathCaptureStart;

/* Foreground only. Reads the radio DMA position with interrupts masked for a
 * few instructions. False before the stream starts or if the completed-half
 * count moved backwards (the stream must restart). */
bool AudioPath_GetPosition(AudioTimelinePosition *position);
/* Foreground only, with interrupts already masked by the caller, immediately
 * before the microphone DMA starts (decision 0012 item 4). False if the stream
 * is not running or gated, since gated halves are not delivered yet. */
bool AudioPath_AlignCaptureLocked(uint32_t mic_latency_frames,
                                  AudioPathCaptureStart *start);

#endif /* SPOOKY_AUDIO_PATH_SERVICE_H */
