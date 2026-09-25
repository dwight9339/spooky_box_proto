/* Extracted from the proven M7 radio bring-up without changing the Si4735
 * command sequence, property values, delays or timeouts. */
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

static HAL_StatusTypeDef WaitCts(uint8_t *status)
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
  } while ((HAL_GetTick() - start_tick) < RADIO_DEVICE_TIMEOUT_MS);

  return HAL_TIMEOUT;
}

static HAL_StatusTypeDef Command(const uint8_t *command,
                                 uint16_t command_length,
                                 uint8_t *response,
                                 uint16_t response_length)
{
  uint8_t status = 0U;

  if ((radio_i2c == NULL) || (command == NULL) || (command_length == 0U) ||
      (response == NULL) || (response_length == 0U))
  {
    return HAL_ERROR;
  }

  if (WaitCts(NULL) != HAL_OK)
  {
    return HAL_TIMEOUT;
  }
  if (HAL_I2C_Master_Transmit(radio_i2c, SI4735_I2C_ADDRESS_HAL,
                              (uint8_t *)command, command_length,
                              RADIO_I2C_TIMEOUT_MS) != HAL_OK)
  {
    return HAL_ERROR;
  }
  if (WaitCts(&status) != HAL_OK)
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

static bool TuneFrequency(RadioBand band, uint32_t frequency_khz,
                          RadioTuneStatus *tune_status)
{
  const RadioBandConfig *config;
  uint8_t tune[6] = {0U};
  const uint8_t get_interrupt = SI4735_CMD_GET_INT_STATUS;
  uint8_t get_tune_status[2];
  uint8_t response[8] = {0U};
  uint8_t status = 0U;
  uint16_t device_frequency;
  uint16_t tune_length;
  uint32_t start_tick;

  if ((tune_status == NULL) || (band >= RADIO_BAND_COUNT))
  {
    return false;
  }
  config = &band_configs[band];
  radio_target_khz = frequency_khz;
  if ((frequency_khz < config->info.minimum_khz) ||
      (frequency_khz > config->info.maximum_khz))
  {
    return false;
  }

  /* FM command frequencies are in 10 kHz units; AM/SW/LW use 1 kHz. */
  device_frequency = (band == RADIO_BAND_FM)
    ? (uint16_t)((frequency_khz + 5U) / 10U)
    : (uint16_t)frequency_khz;
  tune[0] = config->tune_command;
  tune[1] = 0U;
  tune[2] = (uint8_t)(device_frequency >> 8);
  tune[3] = (uint8_t)device_frequency;
  tune[4] = (uint8_t)(config->antenna_capacitance >> 8);
  tune[5] = (uint8_t)config->antenna_capacitance;
  tune_length = (band == RADIO_BAND_FM) ? 5U : 6U;
  get_tune_status[0] = config->tune_status_command;
  get_tune_status[1] = 0x01U;

  if (Command(tune, tune_length, &status, sizeof(status)) != HAL_OK)
  {
    return false;
  }

  start_tick = HAL_GetTick();
  do
  {
    if (Command(&get_interrupt, sizeof(get_interrupt), &status,
                sizeof(status)) != HAL_OK)
    {
      return false;
    }
    if ((status & SI4735_STATUS_STCINT) != 0U)
    {
      break;
    }
    HAL_Delay(2U);
  } while ((HAL_GetTick() - start_tick) < RADIO_DEVICE_TIMEOUT_MS);

  if ((status & SI4735_STATUS_STCINT) == 0U ||
      Command(get_tune_status, sizeof(get_tune_status), response,
              sizeof(response)) != HAL_OK)
  {
    return false;
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
  return true;
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

bool RadioControl_Tune(uint32_t frequency_khz, RadioTuneStatus *result)
{
  const RadioBandInfo *info = &band_configs[radio_band].info;
  uint32_t target_khz;

  radio_target_khz = frequency_khz;
  if ((result == NULL) || (frequency_khz < info->minimum_khz) ||
      (frequency_khz > info->maximum_khz))
  {
    radio_last_fault = RADIO_CONTROL_FAULT_TUNE;
    return false;
  }
  target_khz = (radio_band == RADIO_BAND_FM)
    ? ((frequency_khz + 5U) / 10U) * 10U
    : frequency_khz;
  if (!TuneFrequency(radio_band, target_khz, result))
  {
    radio_last_fault = RADIO_CONTROL_FAULT_TUNE;
    return false;
  }
  radio_last_fault = RADIO_CONTROL_FAULT_NONE;
  return true;
}

bool RadioControl_TuneStep(bool up, RadioTuneStatus *result)
{
  const RadioBandInfo *info = &band_configs[radio_band].info;
  const uint32_t current_khz = radio_tune_status.frequency_khz;
  uint32_t target_khz;

  if (up)
  {
    target_khz = (current_khz <= (info->maximum_khz - info->step_khz))
      ? current_khz + info->step_khz
      : info->maximum_khz;
  }
  else
  {
    target_khz = (current_khz >= (info->minimum_khz + info->step_khz))
      ? current_khz - info->step_khz
      : info->minimum_khz;
  }
  if ((result == NULL) || !TuneFrequency(radio_band, target_khz, result))
  {
    radio_last_fault = RADIO_CONTROL_FAULT_TUNE;
    return false;
  }
  radio_last_fault = RADIO_CONTROL_FAULT_NONE;
  return true;
}

bool RadioControl_SwitchBand(RadioBand band, RadioTuneStatus *result)
{
  const uint8_t power_down = SI4735_CMD_POWER_DOWN;
  const RadioBandConfig *config;
  uint8_t status;

  if ((band >= RADIO_BAND_COUNT) || (result == NULL))
  {
    return false;
  }
  config = &band_configs[band];

  /* Remove digital output cleanly before changing receiver function. */
  if (!SetProperty(SI4735_PROP_DIGITAL_RATE, 0U) ||
      (Command(&power_down, sizeof(power_down), &status,
               sizeof(status)) != HAL_OK))
  {
    radio_last_fault = RADIO_CONTROL_FAULT_BAND_SWITCH;
    return false;
  }
  HAL_Delay(1U);
  if (!PowerUpBand(band, radio_last_frequency_khz[band], result))
  {
    radio_last_fault = RADIO_CONTROL_FAULT_BAND_SWITCH;
    return false;
  }

  radio_last_fault = RADIO_CONTROL_FAULT_NONE;
  RadioControl_LogTune("switched to", result);
  printf("[radio] antenna path=%s\r\n",
         config->select_whip ? "SW whip" :
         (band == RADIO_BAND_FM ? "FM input" : "AM/LW loop"));
  return true;
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
  return true;
}
