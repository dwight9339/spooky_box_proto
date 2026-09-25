#ifndef SPOOKY_RADIO_CONTROL_SERVICE_H
#define SPOOKY_RADIO_CONTROL_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "stm32h7xx_hal.h"

typedef enum
{
  RADIO_BAND_FM = 0,
  RADIO_BAND_AM,
  RADIO_BAND_SW,
  RADIO_BAND_LW,
  RADIO_BAND_COUNT
} RadioBand;

typedef struct
{
  const char *name;
  uint32_t minimum_khz;
  uint32_t maximum_khz;
  uint32_t step_khz;
} RadioBandInfo;

typedef struct
{
  RadioBand band;
  uint32_t frequency_khz;
  uint8_t rssi_dbuv;
  uint8_t snr_db;
  bool valid;
} RadioTuneStatus;

typedef enum
{
  RADIO_CONTROL_FAULT_NONE = 0,
  RADIO_CONTROL_FAULT_START,
  RADIO_CONTROL_FAULT_TUNE,
  RADIO_CONTROL_FAULT_BAND_SWITCH
} RadioControlFault;

typedef struct
{
  /* False after reset is asserted; a failed band switch needs a restart. */
  bool powered;
  RadioBand band;
  uint32_t target_khz;        /* Most recent requested tune target. */
  RadioTuneStatus tune;       /* Most recent successful tune result. */
  RadioControlFault last_fault;
} RadioControlStatus;

/* Owns the Si4735 on I2C1, its reset line and the SW antenna switch. It does
 * not touch audio DMA or the codec; callers sequence muting around transitions. */
bool RadioControl_ProbeControlPath(I2C_HandleTypeDef *i2c);
bool RadioControl_Start(I2C_HandleTypeDef *i2c);
bool RadioControl_Tune(uint32_t frequency_khz, RadioTuneStatus *result);
bool RadioControl_TuneStep(bool up, RadioTuneStatus *result);
bool RadioControl_SwitchBand(RadioBand band, RadioTuneStatus *result);
void RadioControl_PowerDown(void);
void RadioControl_HoldReset(void);
void RadioControl_LogTune(const char *prefix,
                          const RadioTuneStatus *tune_status);
const RadioBandInfo *RadioControl_GetBandInfo(RadioBand band);
bool RadioControl_GetStatus(RadioControlStatus *status);

#endif /* SPOOKY_RADIO_CONTROL_SERVICE_H */
