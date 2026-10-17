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
  bool tune_in_flight;        /* A non-blocking tune has not completed yet. */
  bool band_switch_in_flight; /* A band switch has not completed yet. */
} RadioControlStatus;

typedef enum
{
  RADIO_TUNE_POLL_IDLE = 0,   /* no tune in flight */
  RADIO_TUNE_POLL_PENDING,
  RADIO_TUNE_POLL_DONE,
  RADIO_TUNE_POLL_FAILED      /* bus error or no completion within 2 s */
} RadioTunePoll;

typedef enum
{
  RADIO_BAND_POLL_IDLE = 0,   /* no band switch in flight */
  RADIO_BAND_POLL_PENDING,
  RADIO_BAND_POLL_DONE,
  RADIO_BAND_POLL_FAILED      /* ERR, bus error, or a step's wait exceeded 2 s */
} RadioBandPoll;

/* Owns the Si4735 on I2C1, its reset line and the SW antenna switch. It does
 * not touch audio DMA or the codec; callers sequence muting around transitions. */
bool RadioControl_ProbeControlPath(I2C_HandleTypeDef *i2c);
bool RadioControl_Start(I2C_HandleTypeDef *i2c);
/* Non-blocking in-band tuning (full_spooky_proto-54w.28). BeginTune issues the tune
 * in the current band and returns; PollTune performs at most one bounded status
 * transaction per call and reports completion. One tune is in flight at a time; a
 * band switch or reset abandons it. The published tune status changes only when
 * a tune completes. */
bool RadioControl_TuneInRange(uint32_t frequency_khz);
uint32_t RadioControl_StepTarget(bool up, bool wrap);
bool RadioControl_BeginTune(uint32_t frequency_khz);
RadioTunePoll RadioControl_PollTune(RadioTuneStatus *result);
/* Non-blocking band switch (full_spooky_proto-54w.12.2). BeginSwitchBand starts
 * the proven power-cycle sequence into the band without touching the bus and
 * abandons any tune in flight; it refuses an invalid band, an unpowered receiver
 * or a switch already in flight. Each PollSwitchBand call performs at most one
 * bus step and never waits: one command write with a status read before and
 * after it, or one completion poll of the band's first tune. Fixed delays and device waits elapse between calls. DONE
 * reports the first tune's result in the new band; after FAILED the receiver
 * state is unknown and the caller holds it in reset. Callers mute and gate the
 * audio around the switch. */
bool RadioControl_BeginSwitchBand(RadioBand band);
RadioBandPoll RadioControl_PollSwitchBand(RadioTuneStatus *result);
#if defined(SPOOKY_RADIO_BAND_QUALIFICATION)
/* Qualification only (full_spooky_proto-54w.12.3): the next band switch fails
 * after its POWER_UP is written, as if the receiver had answered ERR. */
void RadioControl_ArmSwitchFault(void);
#endif
/* Does nothing while the receiver is held in reset. */
void RadioControl_PowerDown(void);
void RadioControl_HoldReset(void);
void RadioControl_LogTune(const char *prefix,
                          const RadioTuneStatus *tune_status);
const RadioBandInfo *RadioControl_GetBandInfo(RadioBand band);
bool RadioControl_GetStatus(RadioControlStatus *status);

#endif /* SPOOKY_RADIO_CONTROL_SERVICE_H */
