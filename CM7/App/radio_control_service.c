/* Extracted from the proven M7 radio bring-up. The start path keeps the proven
 * Si4735 command sequence, property values, delays and timeouts.
 * In-band tuning is also available without blocking (full_spooky_proto-54w.28):
 * RadioControl_BeginTune issues TUNE_FREQ and returns, and RadioControl_PollTune
 * performs at most one status transaction per call until the tune completes. Its
 * device-ready waits are bounded by RADIO_FAST_CTS_TIMEOUT_MS, so a stuck
 * receiver shows up as a failed tune instead of a stalled foreground loop.
 *
 * Band transition sequence, with the caller's muting. It keeps the proven
 * commands, property values, delays and timeouts but does not block
 * (full_spooky_proto-54w.12.2): RadioControl_BeginSwitchBand starts it without
 * touching the bus, and each RadioControl_PollSwitchBand call advances it by at
 * most one bus step (one command write with a status read before and after it,
 * or one completion poll of the first tune). Every receiver command waits for CTS before and after
 * it is sent, each wait up to 2 s across calls; the fixed delays, also waited
 * across calls, add up to 71 ms.
 *   1. Caller mutes the codec over I2C4 and closes the stream gate.
 *   2. Set the digital output rate to zero (10 ms).
 *   3. Power down the receiver (1 ms).
 *   4. Select the band's antenna path.
 *   5. Power up in the band's function with digital output.
 *   6. Set reference clock, prescaler, volume and hard mute (10 ms after each).
 *   7. Tune the band's last frequency and poll completion every 2 ms, up to 2 s.
 *   8. Set digital output rate and format (10 ms after each).
 *   9. Caller opens the stream gate and unmutes the codec. */
#include "radio_control_service.h"

#include "main.h"

#include <stdio.h>

#define SI4735_I2C_ADDRESS_HAL         (0x11U << 1U)
#define SI4735_STATUS_CTS              0x80U
#define SI4735_STATUS_ERR              0x40U
#define SI4735_STATUS_STCINT           0x01U
#define SI4735_CMD_POWER_UP            0x01U
#define SI4735_CMD_GET_REV             0x10U
#define SI4735_CMD_POWER_DOWN          0x11U
#define SI4735_CMD_SET_PROPERTY        0x12U
#define SI4735_CMD_GET_INT_STATUS      0x14U
#define SI4735_CMD_FM_TUNE_FREQ        0x20U
#define SI4735_CMD_FM_TUNE_STATUS      0x22U
#define SI4735_CMD_AM_TUNE_FREQ        0x40U
#define SI4735_CMD_AM_TUNE_STATUS      0x42U
#define SI4735_POWER_UP_FM_ANALOG      0x00U
#define SI4735_POWER_UP_AM_ANALOG      0x01U
#define SI4735_POWER_UP_FM_DIGITAL     0xB5U
#define SI4735_ANALOG_AUDIO_OUTPUT     0x05U
#define SI4735_PROP_DIGITAL_FORMAT     0x0102U
#define SI4735_PROP_DIGITAL_RATE       0x0104U
#define SI4735_PROP_REFCLK_FREQ        0x0201U
#define SI4735_PROP_REFCLK_PRESCALE    0x0202U
#define SI4735_PROP_RX_VOLUME          0x4000U
#define SI4735_PROP_RX_HARD_MUTE       0x4001U

#define RADIO_I2C_TIMEOUT_MS           100U
#define RADIO_DEVICE_TIMEOUT_MS        2000U
/* Bounded device-ready wait for the non-blocking tune path. The Si4735 normally
 * reports CTS within a millisecond of these commands; the bound stays provisional
 * until bench latency evidence sets it (Principle IV). */
#define RADIO_FAST_CTS_TIMEOUT_MS      5U
/* The blocking path polls tune completion every 2 ms; the non-blocking path keeps
 * the same minimum interval between status transactions. */
#define RADIO_TUNE_POLL_INTERVAL_MS    2U
#define RADIO_DIGITAL_RATE_HZ          48000U
#define RADIO_DEFAULT_FM_KHZ           99100U

typedef struct
{
  RadioBandInfo info;
  uint32_t default_khz;
  uint8_t power_up_function;
  uint8_t tune_command;
  uint8_t tune_status_command;
  uint16_t antenna_capacitance;
  bool select_whip;
} RadioBandConfig;

static const RadioBandConfig band_configs[RADIO_BAND_COUNT] =
{
  [RADIO_BAND_FM] = {
    {"FM", 87500U, 108000U, 100U}, RADIO_DEFAULT_FM_KHZ,
    SI4735_POWER_UP_FM_ANALOG, SI4735_CMD_FM_TUNE_FREQ,
    SI4735_CMD_FM_TUNE_STATUS, 0U, false
  },
  [RADIO_BAND_AM] = {
    {"AM", 520U, 1710U, 10U}, 1000U,
    SI4735_POWER_UP_AM_ANALOG, SI4735_CMD_AM_TUNE_FREQ,
    SI4735_CMD_AM_TUNE_STATUS, 0U, false
  },
  [RADIO_BAND_SW] = {
    {"SW", 2300U, 23000U, 5U}, 6000U,
    SI4735_POWER_UP_AM_ANALOG, SI4735_CMD_AM_TUNE_FREQ,
    SI4735_CMD_AM_TUNE_STATUS, 1U, true
  },
  [RADIO_BAND_LW] = {
    {"LW", 153U, 279U, 9U}, 198U,
    SI4735_POWER_UP_AM_ANALOG, SI4735_CMD_AM_TUNE_FREQ,
    SI4735_CMD_AM_TUNE_STATUS, 0U, false
  }
};

static I2C_HandleTypeDef *radio_i2c;
static bool radio_powered;
static RadioBand radio_band = RADIO_BAND_FM;
static uint32_t radio_target_khz;
static RadioTuneStatus radio_tune_status;
static RadioControlFault radio_last_fault;
static uint32_t radio_last_frequency_khz[RADIO_BAND_COUNT] =
{
  RADIO_DEFAULT_FM_KHZ, 1000U, 6000U, 198U
};

/* The non-blocking tune in flight, if any. */
static bool tune_in_flight;
static RadioBand tune_band;
static uint32_t tune_start_tick;
static uint32_t tune_last_poll_tick;

/* The non-blocking band switch in flight, if any: its step in the sequence
 * above, where in that step it is, and when the current wait began. */
typedef enum
{
  SWITCH_STEP_RATE_OFF = 0,
  SWITCH_STEP_POWER_DOWN,
  SWITCH_STEP_POWER_UP,
  SWITCH_STEP_REFCLK,
  SWITCH_STEP_PRESCALE,
  SWITCH_STEP_VOLUME,
  SWITCH_STEP_HARD_MUTE,
  SWITCH_STEP_TUNE,
  SWITCH_STEP_RATE_ON,
  SWITCH_STEP_FORMAT,
  SWITCH_STEP_COUNT
} SwitchStep;

typedef enum
{
  SWITCH_PHASE_SEND = 0, /* wait for CTS, then write the step's command */
  SWITCH_PHASE_REPLY,    /* wait for CTS after the command */
  SWITCH_PHASE_TUNE,     /* the first tune: wait for its completion */
  SWITCH_PHASE_DELAY     /* the step's fixed delay */
} SwitchPhase;

static const char *const switch_step_names[SWITCH_STEP_COUNT] =
{
  "DIGITAL_RATE=0", "POWER_DOWN", "POWER_UP", "REFCLK_FREQ", "REFCLK_PRESCALE",
  "RX_VOLUME", "RX_HARD_MUTE", "TUNE", "DIGITAL_RATE", "DIGITAL_FORMAT"
};

static bool switch_in_flight;
static RadioBand switch_band;
static SwitchStep switch_step;
static SwitchPhase switch_phase;
static uint32_t switch_wait_tick;
static uint32_t switch_last_poll_tick;
static RadioTuneStatus switch_result;

static HAL_StatusTypeDef WaitCtsWithin(uint8_t *status, uint32_t timeout_ms)
{
  uint32_t start_tick = HAL_GetTick();
  uint8_t value = 0U;

  do
  {
    if (HAL_I2C_Master_Receive(radio_i2c, SI4735_I2C_ADDRESS_HAL, &value, 1U,
                               RADIO_I2C_TIMEOUT_MS) == HAL_OK)
    {
      if ((value & SI4735_STATUS_ERR) != 0U)
      {
        return HAL_ERROR;
      }
      if ((value & SI4735_STATUS_CTS) != 0U)
      {
        if (status != NULL)
        {
          *status = value;
        }
        return HAL_OK;
      }
    }
    HAL_Delay(1U);
  } while ((HAL_GetTick() - start_tick) < timeout_ms);

  return HAL_TIMEOUT;
}

static HAL_StatusTypeDef WaitCts(uint8_t *status)
{
  return WaitCtsWithin(status, RADIO_DEVICE_TIMEOUT_MS);
}

static HAL_StatusTypeDef CommandWithin(const uint8_t *command,
                                       uint16_t command_length,
                                       uint8_t *response,
                                       uint16_t response_length,
                                       uint32_t cts_timeout_ms)
{
  uint8_t status = 0U;

  if ((radio_i2c == NULL) || (command == NULL) || (command_length == 0U) ||
      (response == NULL) || (response_length == 0U))
  {
    return HAL_ERROR;
  }

  if (WaitCtsWithin(NULL, cts_timeout_ms) != HAL_OK)
  {
    return HAL_TIMEOUT;
  }
  if (HAL_I2C_Master_Transmit(radio_i2c, SI4735_I2C_ADDRESS_HAL,
                              (uint8_t *)command, command_length,
                              RADIO_I2C_TIMEOUT_MS) != HAL_OK)
  {
    return HAL_ERROR;
  }
  if (WaitCtsWithin(&status, cts_timeout_ms) != HAL_OK)
  {
    return HAL_TIMEOUT;
  }

  if (response_length == 1U)
  {
    response[0] = status;
    return HAL_OK;
  }
  if (HAL_I2C_Master_Receive(radio_i2c, SI4735_I2C_ADDRESS_HAL, response,
                             response_length, RADIO_I2C_TIMEOUT_MS) != HAL_OK)
  {
    return HAL_ERROR;
  }
  if (((response[0] & SI4735_STATUS_CTS) == 0U) ||
      ((response[0] & SI4735_STATUS_ERR) != 0U))
  {
    return HAL_ERROR;
  }
  return HAL_OK;
}

static HAL_StatusTypeDef Command(const uint8_t *command,
                                 uint16_t command_length,
                                 uint8_t *response,
                                 uint16_t response_length)
{
  return CommandWithin(command, command_length, response, response_length,
                       RADIO_DEVICE_TIMEOUT_MS);
}

bool RadioControl_ProbeControlPath(I2C_HandleTypeDef *i2c)
{
  const uint8_t power_up[] = {
    SI4735_CMD_POWER_UP,
    SI4735_POWER_UP_FM_ANALOG,
    SI4735_ANALOG_AUDIO_OUTPUT
  };
  const uint8_t get_rev = SI4735_CMD_GET_REV;
  const uint8_t power_down = SI4735_CMD_POWER_DOWN;
  uint8_t response[9] = {0U};
  uint8_t status = 0U;
  bool passed = false;

  if (i2c == NULL)
  {
    return false;
  }
  radio_i2c = i2c;
  radio_powered = false;

  printf("\r\n[radio] Si4735 control-path smoke test\r\n");
  printf("[radio] PA10 reset, PA8 RCLK=LSE/1 (32768 Hz), "
         "I2C1 PB8/PB9, address 0x11\r\n");

  HAL_GPIO_WritePin(RADIO_RST_GPIO_Port, RADIO_RST_Pin, GPIO_PIN_RESET);
  HAL_Delay(2U);
  if ((HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_8) != GPIO_PIN_SET) ||
      (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_9) != GPIO_PIN_SET))
  {
    printf("[radio] FAIL: I2C1 is not idle-high while reset is asserted\r\n");
    goto done;
  }
  printf("[radio] reset low; bus idle-high; GPIO1=%u INT=%u\r\n",
         (unsigned int)HAL_GPIO_ReadPin(RADIO_GPIO_1_GPIO_Port,
                                        RADIO_GPIO_1_Pin),
         (unsigned int)HAL_GPIO_ReadPin(RADIO_INT_GPIO_Port,
                                        RADIO_INT_Pin));

  HAL_GPIO_WritePin(RADIO_RST_GPIO_Port, RADIO_RST_Pin, GPIO_PIN_SET);
  HAL_Delay(15U);
  if ((HAL_I2C_IsDeviceReady(i2c, SI4735_I2C_ADDRESS_HAL, 3U,
                             RADIO_I2C_TIMEOUT_MS) != HAL_OK) ||
      (WaitCts(NULL) != HAL_OK))
  {
    printf("[radio] FAIL: no ready device at 0x11; I2C error=0x%08lX\r\n",
           (unsigned long)HAL_I2C_GetError(i2c));
    goto done;
  }
  printf("[radio] I2C probe and CTS passed\r\n");

  if (Command(power_up, sizeof(power_up), &status, sizeof(status)) != HAL_OK)
  {
    printf("[radio] FAIL: FM analog POWER_UP\r\n");
    goto done;
  }
  if (Command(&get_rev, sizeof(get_rev), response, sizeof(response)) != HAL_OK)
  {
    printf("[radio] FAIL: GET_REV\r\n");
    goto done;
  }

  printf("[radio] PASS: part=0x%02X fw=%c.%c patch=0x%04X "
         "comp=%c.%c chip=%c\r\n",
         response[1], response[2], response[3],
         ((uint16_t)response[4] << 8) | response[5],
         response[6], response[7], response[8]);
  passed = true;

  if (Command(&power_down, sizeof(power_down), &status,
              sizeof(status)) != HAL_OK)
  {
    printf("[radio] WARN: POWER_DOWN did not complete; asserting reset\r\n");
  }

done:
  HAL_GPIO_WritePin(RADIO_RST_GPIO_Port, RADIO_RST_Pin, GPIO_PIN_RESET);
  printf("[radio] reset asserted after smoke test\r\n");
  return passed;
}

static bool SetProperty(uint16_t property, uint16_t value)
{
  const uint8_t command[] = {
    SI4735_CMD_SET_PROPERTY, 0U,
    (uint8_t)(property >> 8), (uint8_t)property,
    (uint8_t)(value >> 8), (uint8_t)value
  };
  uint8_t status;

  if (Command(command, sizeof(command), &status, sizeof(status)) != HAL_OK)
  {
    printf("[radio] FAIL: SET_PROPERTY 0x%04X=0x%04X\r\n",
           property, value);
    return false;
  }
  HAL_Delay(10U);
  return true;
}

/* Converts a requested frequency to what the receiver will report: FM command
 * frequencies are in 10 kHz units; AM, SW and LW use 1 kHz. */
static uint32_t RoundToDeviceUnit(RadioBand band, uint32_t frequency_khz)
{
  return (band == RADIO_BAND_FM) ? ((frequency_khz + 5U) / 10U) * 10U
                                 : frequency_khz;
}

/* The band's TUNE_FREQ command for a frequency; returns its length. */
static uint16_t TuneCommand(RadioBand band, uint32_t frequency_khz,
                            uint8_t tune[6])
{
  const RadioBandConfig *config = &band_configs[band];
  const uint16_t device_frequency = (band == RADIO_BAND_FM)
    ? (uint16_t)((frequency_khz + 5U) / 10U)
    : (uint16_t)frequency_khz;

  tune[0] = config->tune_command;
  tune[1] = 0U;
  tune[2] = (uint8_t)(device_frequency >> 8);
  tune[3] = (uint8_t)device_frequency;
  tune[4] = (uint8_t)(config->antenna_capacitance >> 8);
  tune[5] = (uint8_t)config->antenna_capacitance;
  return (band == RADIO_BAND_FM) ? 5U : 6U;
}

static bool IssueTune(RadioBand band, uint32_t frequency_khz,
                      uint32_t cts_timeout_ms)
{
  const RadioBandConfig *config = &band_configs[band];
  uint8_t tune[6] = {0U};
  uint8_t status = 0U;
  uint16_t tune_length;

  radio_target_khz = frequency_khz;
  if ((frequency_khz < config->info.minimum_khz) ||
      (frequency_khz > config->info.maximum_khz))
  {
    return false;
  }
  tune_length = TuneCommand(band, frequency_khz, tune);

  return CommandWithin(tune, tune_length, &status, sizeof(status),
                       cts_timeout_ms) == HAL_OK;
}

typedef enum
{
  TUNE_POLL_PENDING = 0,
  TUNE_POLL_DONE,
  TUNE_POLL_BUS_FAILED
} TunePollResult;

/* One GET_INT_STATUS transaction, and TUNE_STATUS once STC is set. */
static TunePollResult PollTuneOnce(RadioBand band, uint32_t cts_timeout_ms,
                                   RadioTuneStatus *tune_status)
{
  const RadioBandConfig *config = &band_configs[band];
  const uint8_t get_interrupt = SI4735_CMD_GET_INT_STATUS;
  const uint8_t get_tune_status[2] = {config->tune_status_command, 0x01U};
  uint8_t response[8] = {0U};
  uint8_t status = 0U;
  uint16_t device_frequency;

  if (CommandWithin(&get_interrupt, sizeof(get_interrupt), &status,
                    sizeof(status), cts_timeout_ms) != HAL_OK)
  {
    return TUNE_POLL_BUS_FAILED;
  }
  if ((status & SI4735_STATUS_STCINT) == 0U)
  {
    return TUNE_POLL_PENDING;
  }
  if (CommandWithin(get_tune_status, sizeof(get_tune_status), response,
                    sizeof(response), cts_timeout_ms) != HAL_OK)
  {
    return TUNE_POLL_BUS_FAILED;
  }

  device_frequency = ((uint16_t)response[2] << 8) | response[3];
  tune_status->band = band;
  tune_status->frequency_khz = (band == RADIO_BAND_FM)
    ? (uint32_t)device_frequency * 10U
    : (uint32_t)device_frequency;
  tune_status->rssi_dbuv = response[4];
  tune_status->snr_db = response[5];
  tune_status->valid = (response[1] & 1U) != 0U;
  radio_band = band;
  radio_tune_status = *tune_status;
  radio_last_frequency_khz[band] = tune_status->frequency_khz;
  return TUNE_POLL_DONE;
}

/* The blocking tune used while powering up a band: the proven sequence, polling
 * completion every 2 ms for up to 2 s. */
static bool TuneFrequency(RadioBand band, uint32_t frequency_khz,
                          RadioTuneStatus *tune_status)
{
  TunePollResult result = TUNE_POLL_PENDING;
  uint32_t start_tick;

  if ((tune_status == NULL) || (band >= RADIO_BAND_COUNT) ||
      !IssueTune(band, frequency_khz, RADIO_DEVICE_TIMEOUT_MS))
  {
    return false;
  }

  start_tick = HAL_GetTick();
  do
  {
    result = PollTuneOnce(band, RADIO_DEVICE_TIMEOUT_MS, tune_status);
    if (result != TUNE_POLL_PENDING)
    {
      break;
    }
    HAL_Delay(RADIO_TUNE_POLL_INTERVAL_MS);
  } while ((HAL_GetTick() - start_tick) < RADIO_DEVICE_TIMEOUT_MS);

  return result == TUNE_POLL_DONE;
}

static bool PowerUpBand(RadioBand band, uint32_t frequency_khz,
                        RadioTuneStatus *tune_status)
{
  const RadioBandConfig *config;
  uint8_t power_up[3];
  uint8_t status = 0U;

  if ((band >= RADIO_BAND_COUNT) || (tune_status == NULL))
  {
    return false;
  }
  config = &band_configs[band];
  HAL_GPIO_WritePin(RADIO_SW_SWITCH_GPIO_Port, RADIO_SW_SWITCH_Pin,
                    config->select_whip ? GPIO_PIN_SET : GPIO_PIN_RESET);
  power_up[0] = SI4735_CMD_POWER_UP;
  power_up[1] = config->power_up_function;
  power_up[2] = SI4735_POWER_UP_FM_DIGITAL;

  if (Command(power_up, sizeof(power_up), &status, sizeof(status)) != HAL_OK)
  {
    printf("[radio] FAIL: %s digital POWER_UP\r\n", config->info.name);
    return false;
  }
  if (!SetProperty(SI4735_PROP_REFCLK_FREQ, 32768U) ||
      !SetProperty(SI4735_PROP_REFCLK_PRESCALE, 1U) ||
      !SetProperty(SI4735_PROP_RX_VOLUME, 50U) ||
      !SetProperty(SI4735_PROP_RX_HARD_MUTE, 0U))
  {
    return false;
  }

  /* AM-family digital clocks do not start until the first tune completes. */
  if (!TuneFrequency(band, frequency_khz, tune_status))
  {
    printf("[radio] FAIL: %s tune timeout/status\r\n", config->info.name);
    return false;
  }

  if (!SetProperty(SI4735_PROP_DIGITAL_RATE, RADIO_DIGITAL_RATE_HZ) ||
      !SetProperty(SI4735_PROP_DIGITAL_FORMAT, 0x0000U))
  {
    return false;
  }
  return true;
}

void RadioControl_LogTune(const char *prefix,
                          const RadioTuneStatus *tune_status)
{
  const RadioBandConfig *config;

  if ((prefix == NULL) || (tune_status == NULL) ||
      (tune_status->band >= RADIO_BAND_COUNT))
  {
    return;
  }
  config = &band_configs[tune_status->band];
  printf("[radio] %s %s %lu kHz (%lu.%03lu MHz): "
         "RSSI=%u dBuV SNR=%u dB valid=%u\r\n",
         prefix, config->info.name,
         (unsigned long)tune_status->frequency_khz,
         (unsigned long)(tune_status->frequency_khz / 1000U),
         (unsigned long)(tune_status->frequency_khz % 1000U),
         tune_status->rssi_dbuv, tune_status->snr_db, tune_status->valid);
}

bool RadioControl_Start(I2C_HandleTypeDef *i2c)
{
  RadioTuneStatus tune_status;

  if (i2c == NULL)
  {
    return false;
  }
  radio_i2c = i2c;
  radio_last_fault = RADIO_CONTROL_FAULT_NONE;

  printf("\r\n[radio] Si4735 digital multi-band setup\r\n");
  HAL_GPIO_WritePin(RADIO_RST_GPIO_Port, RADIO_RST_Pin, GPIO_PIN_RESET);
  HAL_Delay(2U);
  HAL_GPIO_WritePin(RADIO_RST_GPIO_Port, RADIO_RST_Pin, GPIO_PIN_SET);
  HAL_Delay(15U);
  radio_powered = true;
  if ((WaitCts(NULL) != HAL_OK) ||
      !PowerUpBand(RADIO_BAND_FM, RADIO_DEFAULT_FM_KHZ, &tune_status))
  {
    radio_last_fault = RADIO_CONTROL_FAULT_START;
    return false;
  }
  RadioControl_LogTune("tuned", &tune_status);
  printf("[radio] digital output enabled: 48 kHz, 16-bit stereo I2S\r\n");
  return true;
}

bool RadioControl_TuneInRange(uint32_t frequency_khz)
{
  const RadioBandInfo *info = &band_configs[radio_band].info;
  return (frequency_khz >= info->minimum_khz) &&
         (frequency_khz <= info->maximum_khz);
}

/* One band step from the last completed frequency. At the band edge in the step
 * direction, `wrap` moves to the opposite edge and otherwise the edge holds. */
uint32_t RadioControl_StepTarget(bool up, bool wrap)
{
  const RadioBandInfo *info = &band_configs[radio_band].info;
  const uint32_t current_khz = radio_tune_status.frequency_khz;

  if (up)
  {
    if (current_khz <= (info->maximum_khz - info->step_khz))
    {
      return current_khz + info->step_khz;
    }
    return (wrap && (current_khz >= info->maximum_khz)) ? info->minimum_khz
                                                         : info->maximum_khz;
  }
  if (current_khz >= (info->minimum_khz + info->step_khz))
  {
    return current_khz - info->step_khz;
  }
  return (wrap && (current_khz <= info->minimum_khz)) ? info->maximum_khz
                                                       : info->minimum_khz;
}

bool RadioControl_BeginTune(uint32_t frequency_khz)
{
  uint32_t target_khz;

  if (tune_in_flight || !radio_powered)
  {
    return false;
  }
  if (!RadioControl_TuneInRange(frequency_khz))
  {
    radio_target_khz = frequency_khz;
    radio_last_fault = RADIO_CONTROL_FAULT_TUNE;
    return false;
  }
  target_khz = RoundToDeviceUnit(radio_band, frequency_khz);
  if (!IssueTune(radio_band, target_khz, RADIO_FAST_CTS_TIMEOUT_MS))
  {
    radio_last_fault = RADIO_CONTROL_FAULT_TUNE;
    return false;
  }
  tune_in_flight = true;
  tune_band = radio_band;
  tune_start_tick = HAL_GetTick();
  tune_last_poll_tick = tune_start_tick;
  return true;
}

RadioTunePoll RadioControl_PollTune(RadioTuneStatus *result)
{
  RadioTuneStatus completed;
  const uint32_t now = HAL_GetTick();

  if (!tune_in_flight)
  {
    return RADIO_TUNE_POLL_IDLE;
  }
  if ((now - tune_last_poll_tick) < RADIO_TUNE_POLL_INTERVAL_MS)
  {
    return RADIO_TUNE_POLL_PENDING;
  }
  tune_last_poll_tick = now;

  switch (PollTuneOnce(tune_band, RADIO_FAST_CTS_TIMEOUT_MS, &completed))
  {
    case TUNE_POLL_DONE:
      tune_in_flight = false;
      radio_last_fault = RADIO_CONTROL_FAULT_NONE;
      if (result != NULL)
      {
        *result = completed;
      }
      return RADIO_TUNE_POLL_DONE;
    case TUNE_POLL_BUS_FAILED:
      tune_in_flight = false;
      radio_last_fault = RADIO_CONTROL_FAULT_TUNE;
      return RADIO_TUNE_POLL_FAILED;
    case TUNE_POLL_PENDING:
    default:
      break;
  }
  if ((now - tune_start_tick) >= RADIO_DEVICE_TIMEOUT_MS)
  {
    tune_in_flight = false;
    radio_last_fault = RADIO_CONTROL_FAULT_TUNE;
    return RADIO_TUNE_POLL_FAILED;
  }
  return RADIO_TUNE_POLL_PENDING;
}

bool RadioControl_BeginSwitchBand(RadioBand band)
{
  if ((band >= RADIO_BAND_COUNT) || !radio_powered || switch_in_flight)
  {
    return false;
  }
  tune_in_flight = false; /* a band switch abandons the tune in flight */
  switch_in_flight = true;
  switch_band = band;
  switch_step = SWITCH_STEP_RATE_OFF;
  switch_phase = SWITCH_PHASE_SEND;
  switch_wait_tick = HAL_GetTick();
  return true;
}

/* The step's command and the fixed delay after its reply, as in the proven
 * sequence; returns the command's length. */
static uint16_t SwitchCommand(SwitchStep step, uint8_t command[6],
                              uint32_t *delay_ms)
{
  const RadioBandConfig *config = &band_configs[switch_band];
  uint16_t property;
  uint16_t value;

  *delay_ms = 10U;
  switch (step)
  {
    case SWITCH_STEP_POWER_DOWN:
      command[0] = SI4735_CMD_POWER_DOWN;
      *delay_ms = 1U;
      return 1U;
    case SWITCH_STEP_POWER_UP:
      command[0] = SI4735_CMD_POWER_UP;
      command[1] = config->power_up_function;
      command[2] = SI4735_POWER_UP_FM_DIGITAL;
      *delay_ms = 0U;
      return 3U;
    case SWITCH_STEP_TUNE:
      *delay_ms = 0U;
      return TuneCommand(switch_band, radio_last_frequency_khz[switch_band],
                         command);
    case SWITCH_STEP_REFCLK:
      property = SI4735_PROP_REFCLK_FREQ;
      value = 32768U;
      break;
    case SWITCH_STEP_PRESCALE:
      property = SI4735_PROP_REFCLK_PRESCALE;
      value = 1U;
      break;
    case SWITCH_STEP_VOLUME:
      property = SI4735_PROP_RX_VOLUME;
      value = 50U;
      break;
    case SWITCH_STEP_HARD_MUTE:
      property = SI4735_PROP_RX_HARD_MUTE;
      value = 0U;
      break;
    case SWITCH_STEP_RATE_ON:
      property = SI4735_PROP_DIGITAL_RATE;
      value = RADIO_DIGITAL_RATE_HZ;
      break;
    case SWITCH_STEP_FORMAT:
      property = SI4735_PROP_DIGITAL_FORMAT;
      value = 0x0000U;
      break;
    case SWITCH_STEP_RATE_OFF:
    default:
      /* Remove digital output cleanly before changing receiver function. */
      property = SI4735_PROP_DIGITAL_RATE;
      value = 0U;
      break;
  }
  command[0] = SI4735_CMD_SET_PROPERTY;
  command[1] = 0U;
  command[2] = (uint8_t)(property >> 8);
  command[3] = (uint8_t)property;
  command[4] = (uint8_t)(value >> 8);
  command[5] = (uint8_t)value;
  return 6U;
}

static RadioBandPoll FailSwitch(void)
{
  switch_in_flight = false;
  radio_last_fault = RADIO_CONTROL_FAULT_BAND_SWITCH;
  printf("[radio] FAIL: %s band switch at %s\r\n",
         band_configs[switch_band].info.name, switch_step_names[switch_step]);
  return RADIO_BAND_POLL_FAILED;
}

/* The next step, or the end of the sequence. */
static RadioBandPoll NextSwitchStep(uint32_t now, RadioTuneStatus *result)
{
  const RadioBandConfig *config = &band_configs[switch_band];

  switch_step = (SwitchStep)((uint32_t)switch_step + 1U);
  switch_phase = SWITCH_PHASE_SEND;
  switch_wait_tick = now;
  if (switch_step < SWITCH_STEP_COUNT)
  {
    return RADIO_BAND_POLL_PENDING;
  }
  switch_in_flight = false;
  radio_last_fault = RADIO_CONTROL_FAULT_NONE;
  if (result != NULL)
  {
    *result = switch_result;
  }
  printf("[radio] antenna path=%s\r\n",
         config->select_whip ? "SW whip" :
         (switch_band == RADIO_BAND_FM ? "FM input" : "AM/LW loop"));
  return RADIO_BAND_POLL_DONE;
}

/* One status read and what it allows in the current step: write the step's
 * command (SEND), or finish the step once the receiver has taken it (REPLY). */
static RadioBandPoll SwitchBusStep(uint32_t now, RadioTuneStatus *result)
{
  uint8_t command[6] = {0U};
  uint16_t length;
  uint32_t delay_ms;
  uint8_t status = 0U;

  /* A read the bus refuses counts as not ready, as in WaitCtsWithin; ERR fails
   * the step. */
  if (HAL_I2C_Master_Receive(radio_i2c, SI4735_I2C_ADDRESS_HAL, &status, 1U,
                             RADIO_I2C_TIMEOUT_MS) != HAL_OK)
  {
    status = 0U;
  }
  if ((status & SI4735_STATUS_ERR) != 0U)
  {
    return FailSwitch();
  }
  if ((status & SI4735_STATUS_CTS) == 0U)
  {
    return ((now - switch_wait_tick) >= RADIO_DEVICE_TIMEOUT_MS)
      ? FailSwitch() : RADIO_BAND_POLL_PENDING;
  }

  length = SwitchCommand(switch_step, command, &delay_ms);
  if (switch_phase == SWITCH_PHASE_SEND)
  {
    if (switch_step == SWITCH_STEP_POWER_UP)
    {
      HAL_GPIO_WritePin(RADIO_SW_SWITCH_GPIO_Port, RADIO_SW_SWITCH_Pin,
                        band_configs[switch_band].select_whip ? GPIO_PIN_SET
                                                              : GPIO_PIN_RESET);
    }
    else if (switch_step == SWITCH_STEP_TUNE)
    {
      radio_target_khz = radio_last_frequency_khz[switch_band];
    }
    if (HAL_I2C_Master_Transmit(radio_i2c, SI4735_I2C_ADDRESS_HAL, command,
                                length, RADIO_I2C_TIMEOUT_MS) != HAL_OK)
    {
      return FailSwitch();
    }
    switch_phase = SWITCH_PHASE_REPLY;
    switch_wait_tick = now;
    return RADIO_BAND_POLL_PENDING;
  }

  /* The command's reply: CTS after it. */
  if (switch_step == SWITCH_STEP_TUNE)
  {
    /* AM-family digital clocks do not start until the first tune completes. */
    switch_phase = SWITCH_PHASE_TUNE;
    switch_wait_tick = now;
    switch_last_poll_tick = now;
    return RADIO_BAND_POLL_PENDING;
  }
  if (delay_ms != 0U)
  {
    switch_phase = SWITCH_PHASE_DELAY;
    switch_wait_tick = now;
    return RADIO_BAND_POLL_PENDING;
  }
  return NextSwitchStep(now, result);
}

RadioBandPoll RadioControl_PollSwitchBand(RadioTuneStatus *result)
{
  const uint32_t now = HAL_GetTick();
  RadioBandPoll poll;
  uint8_t command[6] = {0U};
  uint32_t delay_ms;

  if (!switch_in_flight)
  {
    return RADIO_BAND_POLL_IDLE;
  }
  switch (switch_phase)
  {
    case SWITCH_PHASE_DELAY:
      (void)SwitchCommand(switch_step, command, &delay_ms);
      /* Strictly longer, like HAL_Delay: the wait began part way into a tick. */
      if ((now - switch_wait_tick) <= delay_ms)
      {
        return RADIO_BAND_POLL_PENDING;
      }
      poll = NextSwitchStep(now, result);
      if (poll != RADIO_BAND_POLL_PENDING)
      {
        return poll;
      }
      break; /* the next step's command goes out in this poll */

    case SWITCH_PHASE_TUNE:
      if ((now - switch_last_poll_tick) < RADIO_TUNE_POLL_INTERVAL_MS)
      {
        return RADIO_BAND_POLL_PENDING;
      }
      switch_last_poll_tick = now;
      switch (PollTuneOnce(switch_band, RADIO_FAST_CTS_TIMEOUT_MS, &switch_result))
      {
        case TUNE_POLL_DONE:
          return NextSwitchStep(now, result);
        case TUNE_POLL_BUS_FAILED:
          return FailSwitch();
        case TUNE_POLL_PENDING:
        default:
          break;
      }
      return ((now - switch_wait_tick) >= RADIO_DEVICE_TIMEOUT_MS)
        ? FailSwitch() : RADIO_BAND_POLL_PENDING;

    case SWITCH_PHASE_SEND:
    case SWITCH_PHASE_REPLY:
    default:
      break;
  }

  /* A command just written is usually answered at once, so its reply is read
   * in the same poll: at most one write, with a status read before and after
   * it, and no waiting. */
  if (switch_phase != SWITCH_PHASE_SEND)
  {
    return SwitchBusStep(now, result);
  }
  poll = SwitchBusStep(now, result);
  if ((poll == RADIO_BAND_POLL_PENDING) && (switch_phase == SWITCH_PHASE_REPLY))
  {
    poll = SwitchBusStep(now, result);
  }
  return poll;
}

void RadioControl_PowerDown(void)
{
  const uint8_t power_down = SI4735_CMD_POWER_DOWN;
  uint8_t status;

  (void)Command(&power_down, sizeof(power_down), &status, sizeof(status));
}

void RadioControl_HoldReset(void)
{
  HAL_GPIO_WritePin(RADIO_RST_GPIO_Port, RADIO_RST_Pin, GPIO_PIN_RESET);
  radio_powered = false;
  tune_in_flight = false;
  switch_in_flight = false;
}

const RadioBandInfo *RadioControl_GetBandInfo(RadioBand band)
{
  return (band < RADIO_BAND_COUNT) ? &band_configs[band].info : NULL;
}

bool RadioControl_GetStatus(RadioControlStatus *status)
{
  if (status == NULL)
  {
    return false;
  }
  status->powered = radio_powered;
  status->band = radio_band;
  status->target_khz = radio_target_khz;
  status->tune = radio_tune_status;
  status->last_fault = radio_last_fault;
  status->tune_in_flight = tune_in_flight;
  status->band_switch_in_flight = switch_in_flight;
  return true;
}
