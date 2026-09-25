#ifndef SPOOKY_CODEC_VOLUME_SERVICE_H
#define SPOOKY_CODEC_VOLUME_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "stm32h7xx_hal.h"

typedef enum
{
  CODEC_VOLUME_ERROR_NONE = 0,
  CODEC_VOLUME_ERROR_OUTPUT,
  CODEC_VOLUME_ERROR_ADC
} CodecVolumeError;

typedef struct
{
  bool ready;
  bool volume_muted;
  bool transition_muted;
  bool line_in_inserted;
  bool headphone_inserted;
  uint32_t volume_adc;
  uint8_t headphone_volume_code;
  uint8_t attenuation_half_db;
  CodecVolumeError last_error;
} CodecVolumeStatus;

/* Owns SGTL5000 control, SAI1 MCLK setup, ADC3 volume sampling and jack state. */
bool CodecVolume_Init(I2C_HandleTypeDef *i2c, ADC_HandleTypeDef *volume_adc,
                      SAI_HandleTypeDef *codec_sai);
bool CodecVolume_ProbeControlPath(I2C_HandleTypeDef *i2c,
                                 SAI_HandleTypeDef *codec_sai);
bool CodecVolume_Service(void);
bool CodecVolume_RefreshVolume(void);
bool CodecVolume_SetTransitionMuted(bool muted);
bool CodecVolume_GetStatus(CodecVolumeStatus *status);

#endif /* SPOOKY_CODEC_VOLUME_SERVICE_H */
