/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "board_diagnostics.h"
#include "target_logger.h"
#include "diagnostics.h"
#include "ipc_smoke_cli.h"
#if defined(SPOOKY_IPC_SMOKE)
#include "ipc_smoke.h"
#endif
#include "prototype_power.h"
#include "fuel_gauge_test.h"
#include "magnetometer_test.h"
#include "radio_recorder.h"
#include "sd_test.h"
#include "ui_board_test.h"
#include "usb_test.h"
#include "wav_transfer.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

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
  uint32_t default_khz;
  uint32_t step_khz;
  uint8_t power_up_function;
  uint8_t tune_command;
  uint8_t tune_status_command;
  uint16_t antenna_capacitance;
  bool select_whip;
} RadioBandConfig;

typedef struct
{
  RadioBand band;
  uint32_t frequency_khz;
  uint8_t rssi_dbuv;
  uint8_t snr_db;
  bool valid;
} RadioTuneStatus;

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

#define SPOOKY_MINIMAL_BRINGUP
#define SGTL5000_I2C_ADDRESS_HAL       (0x0AU << 1U)
#define SGTL5000_CHIP_ID_REGISTER      0x0000U
#define SI4735_I2C_ADDRESS_HAL         (0x11U << 1U)
#define SI4735_STATUS_CTS              0x80U
#define SI4735_STATUS_ERR              0x40U
#define SI4735_CMD_POWER_UP            0x01U
#define SI4735_CMD_GET_REV             0x10U
#define SI4735_CMD_POWER_DOWN          0x11U
#define SI4735_POWER_UP_FM_ANALOG      0x00U
#define SI4735_ANALOG_AUDIO_OUTPUT     0x05U
#define BRINGUP_I2C_TIMEOUT_MS         100U
#define BRINGUP_DEVICE_TIMEOUT_MS      2000U
#define RADIO_AUDIO_SAMPLE_RATE_HZ     48000U
#define RADIO_AUDIO_FREQUENCY_KHZ      99100U
#define RADIO_AUDIO_BUFFER_SAMPLES     2048U
#define RADIO_AUDIO_START_TIMEOUT_MS   500U
#define RADIO_AUDIO_HP_VOLUME_CODE     0x54U /* -30 dB; 0x18 is 0 dB. */
#define VOLUME_SAMPLE_PERIOD_MS        10U
#define VOLUME_MUTE_THRESHOLD          1024U
#define HP_VOLUME_0DB_CODE             0x18U
#define HP_VOLUME_MIN_CODE             0x7FU

#define SI4735_CMD_SET_PROPERTY        0x12U
#define SI4735_CMD_GET_INT_STATUS      0x14U
#define SI4735_CMD_FM_TUNE_FREQ        0x20U
#define SI4735_CMD_FM_TUNE_STATUS      0x22U
#define SI4735_CMD_AM_TUNE_FREQ        0x40U
#define SI4735_CMD_AM_TUNE_STATUS      0x42U
#define SI4735_POWER_UP_AM_ANALOG      0x01U
#define SI4735_POWER_UP_FM_DIGITAL     0xB5U
#define SI4735_STATUS_STCINT           0x01U
#define SI4735_PROP_DIGITAL_FORMAT     0x0102U
#define SI4735_PROP_DIGITAL_RATE       0x0104U
#define SI4735_PROP_REFCLK_FREQ        0x0201U
#define SI4735_PROP_REFCLK_PRESCALE    0x0202U
#define SI4735_PROP_RX_VOLUME          0x4000U
#define SI4735_PROP_RX_HARD_MUTE       0x4001U

#define SGTL5000_CHIP_DIG_POWER        0x0002U
#define SGTL5000_CHIP_CLK_CTRL         0x0004U
#define SGTL5000_CHIP_I2S_CTRL         0x0006U
#define SGTL5000_CHIP_SSS_CTRL         0x000AU
#define SGTL5000_CHIP_ADCDAC_CTRL      0x000EU
#define SGTL5000_CHIP_DAC_VOL          0x0010U
#define SGTL5000_CHIP_ANA_HP_CTRL      0x0022U
#define SGTL5000_CHIP_ANA_CTRL         0x0024U
#define SGTL5000_CHIP_LINREG_CTRL      0x0026U
#define SGTL5000_CHIP_REF_CTRL         0x0028U
#define SGTL5000_CHIP_ANA_POWER        0x0030U
#define SGTL5000_CHIP_SHORT_CTRL       0x003CU

/* DUAL_CORE_BOOT_SYNC_SEQUENCE: Define for dual core boot synchronization    */
/*                             demonstration code based on hardware semaphore */
/* This define is present in both CM7/CM4 projects                            */
/* To comment when developping/debugging on a single core                     */
#define DUAL_CORE_BOOT_SYNC_SEQUENCE

#if defined(DUAL_CORE_BOOT_SYNC_SEQUENCE)
#ifndef HSEM_ID_0
#define HSEM_ID_0 (0U) /* HW semaphore 0*/
#endif
#endif /* DUAL_CORE_BOOT_SYNC_SEQUENCE */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

COM_InitTypeDef BspCOMInit;
ADC_HandleTypeDef hadc3;

DFSDM_Channel_HandleTypeDef hdfsdm1_channel0;
DFSDM_Filter_HandleTypeDef hdfsdm1_filter0;

I2C_HandleTypeDef hi2c1;
I2C_HandleTypeDef hi2c2;
I2C_HandleTypeDef hi2c4;

SAI_HandleTypeDef hsai_BlockA1;
SAI_HandleTypeDef hsai_BlockB1;
SAI_HandleTypeDef hsai_BlockA2;
DMA_HandleTypeDef hdma_sai1_a;
DMA_HandleTypeDef hdma_sai1_b;

SD_HandleTypeDef hsd1;

SPI_HandleTypeDef hspi6;

/* USER CODE BEGIN PV */

DMA_HandleTypeDef hdma_sai2_a;

static uint16_t radio_audio_rx_buffer[RADIO_AUDIO_BUFFER_SAMPLES]
  __attribute__((section(".dma_buffer"), aligned(32)));
static uint16_t radio_audio_tx_buffer[RADIO_AUDIO_BUFFER_SAMPLES]
  __attribute__((section(".dma_buffer"), aligned(32)));
static volatile uint32_t radio_audio_rx_half_count;
static volatile uint32_t radio_audio_rx_full_count;
static volatile uint32_t radio_audio_error_flags;
static volatile bool radio_audio_copy_enabled;
static bool radio_audio_running;
static bool radio_audio_headphones_unmuted;
static RadioTuneStatus radio_tune_status;
static bool volume_ready;
static uint32_t volume_filtered;
static uint32_t volume_last_sample_tick;
static uint8_t volume_last_code = RADIO_AUDIO_HP_VOLUME_CODE;
static bool volume_last_muted = true;
static RadioBand radio_band = RADIO_BAND_FM;
static const RadioBandConfig radio_band_configs[RADIO_BAND_COUNT] =
{
  [RADIO_BAND_FM] = {
    "FM", 87500U, 108000U, RADIO_AUDIO_FREQUENCY_KHZ, 100U,
    SI4735_POWER_UP_FM_ANALOG, SI4735_CMD_FM_TUNE_FREQ,
    SI4735_CMD_FM_TUNE_STATUS, 0U, false
  },
  [RADIO_BAND_AM] = {
    "AM", 520U, 1710U, 1000U, 10U,
    SI4735_POWER_UP_AM_ANALOG, SI4735_CMD_AM_TUNE_FREQ,
    SI4735_CMD_AM_TUNE_STATUS, 0U, false
  },
  [RADIO_BAND_SW] = {
    "SW", 2300U, 23000U, 6000U, 5U,
    SI4735_POWER_UP_AM_ANALOG, SI4735_CMD_AM_TUNE_FREQ,
    SI4735_CMD_AM_TUNE_STATUS, 1U, true
  },
  [RADIO_BAND_LW] = {
    "LW", 153U, 279U, 198U, 9U,
    SI4735_POWER_UP_AM_ANALOG, SI4735_CMD_AM_TUNE_FREQ,
    SI4735_CMD_AM_TUNE_STATUS, 0U, false
  }
};
static uint32_t radio_last_frequency_khz[RADIO_BAND_COUNT] =
{
  RADIO_AUDIO_FREQUENCY_KHZ, 1000U, 6000U, 198U
};

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void PeriphCommonClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_I2C1_Init(void);
static void MX_I2C4_Init(void);
static void MX_SAI1_Init(void);
static void MX_SDMMC1_SD_Init(void);
static void MX_ADC3_Init(void);
static void MX_DFSDM1_Init(void);
static void MX_I2C2_Init(void);
static void MX_SAI2_Init(void);
static void MX_SPI6_Init(void);
/* USER CODE BEGIN PFP */

static HAL_StatusTypeDef Bringup_CodecStartClock(uint32_t *mclk_hz);
static bool Bringup_CodecProbe(void);
static HAL_StatusTypeDef Bringup_RadioWaitCts(uint8_t *status);
static HAL_StatusTypeDef Bringup_RadioCommand(const uint8_t *command,
                                               uint16_t command_length,
                                               uint8_t *response,
                                               uint16_t response_length);
static bool Bringup_RadioProbe(void);
static void Bringup_Run(void);
static bool RadioTuneFrequency(RadioBand band, uint32_t frequency_khz,
                               RadioTuneStatus *tune_status);
static bool VolumeControlInit(void);
static bool VolumeControlService(bool force);
static void UsbCliCommand(const char *line);
static void RadioAudio_Service(void);
static uint32_t RadioAudioMeasureFs(GPIO_TypeDef *port, uint32_t pin);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

static HAL_StatusTypeDef Bringup_CodecStartClock(uint32_t *mclk_hz)
{
  uint32_t sai_clock_hz;

  if (mclk_hz == NULL)
  {
    return HAL_ERROR;
  }

  /* SYS_MCLK must be running before the SGTL5000 control port responds. */
  __HAL_SAI_DISABLE(&hsai_BlockA1);
  hsai_BlockA1.Init.NoDivider = SAI_MASTERDIVIDER_ENABLE;
  hsai_BlockA1.Init.MckOverSampling = SAI_MCK_OVERSAMPLING_DISABLE;
  hsai_BlockA1.Init.OutputDrive = SAI_OUTPUTDRIVE_ENABLE;
  hsai_BlockA1.Init.FIFOThreshold = SAI_FIFOTHRESHOLD_1QF;
  hsai_BlockA1.Init.AudioFrequency = SAI_AUDIO_FREQUENCY_48K;
  hsai_BlockA1.Init.MckOutput = SAI_MCK_OUTPUT_ENABLE;
  if (HAL_SAI_InitProtocol(&hsai_BlockA1, SAI_I2S_STANDARD,
                           SAI_PROTOCOL_DATASIZE_16BIT, 2U) != HAL_OK)
  {
    return HAL_ERROR;
  }

  __HAL_SAI_ENABLE(&hsai_BlockA1);
  HAL_Delay(1U);

  sai_clock_hz = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_SAI1);
  *mclk_hz = (hsai_BlockA1.Init.Mckdiv != 0U)
               ? sai_clock_hz / hsai_BlockA1.Init.Mckdiv
               : sai_clock_hz;
  return HAL_OK;
}

static bool Bringup_CodecProbe(void)
{
  uint8_t data[2] = {0U, 0U};
  uint16_t chip_id;
  uint32_t mclk_hz = 0U;

  printf("\r\n[audio] SGTL5000 control-path smoke test\r\n");
  printf("[audio] I2C4 PF14/PF15; expected address 0x0A\r\n");

  if (Bringup_CodecStartClock(&mclk_hz) != HAL_OK)
  {
    printf("[audio] FAIL: could not start SAI1 MCLK\r\n");
    return false;
  }
  printf("[audio] PE2 MCLK approximately %lu Hz\r\n",
         (unsigned long)mclk_hz);

  if (HAL_I2C_IsDeviceReady(&hi2c4, SGTL5000_I2C_ADDRESS_HAL, 3U,
                            BRINGUP_I2C_TIMEOUT_MS) != HAL_OK)
  {
    printf("[audio] FAIL: no ACK at 0x0A; I2C error=0x%08lX\r\n",
           (unsigned long)HAL_I2C_GetError(&hi2c4));
    __HAL_SAI_DISABLE(&hsai_BlockA1);
    return false;
  }

  if (HAL_I2C_Mem_Read(&hi2c4, SGTL5000_I2C_ADDRESS_HAL,
                       SGTL5000_CHIP_ID_REGISTER, I2C_MEMADD_SIZE_16BIT,
                       data, sizeof(data), BRINGUP_I2C_TIMEOUT_MS) != HAL_OK)
  {
    printf("[audio] FAIL: CHIP_ID read; I2C error=0x%08lX\r\n",
           (unsigned long)HAL_I2C_GetError(&hi2c4));
    __HAL_SAI_DISABLE(&hsai_BlockA1);
    return false;
  }

  chip_id = ((uint16_t)data[0] << 8) | data[1];
  __HAL_SAI_DISABLE(&hsai_BlockA1);
  if ((chip_id >> 8) != 0xA0U)
  {
    printf("[audio] FAIL: CHIP_ID=0x%04X, expected 0xA0xx\r\n", chip_id);
    return false;
  }

  printf("[audio] PASS: CHIP_ID=0x%04X; MCLK stopped; outputs unchanged\r\n",
         chip_id);
  return true;
}

static HAL_StatusTypeDef Bringup_RadioWaitCts(uint8_t *status)
{
  uint32_t start_tick = HAL_GetTick();
  uint8_t value = 0U;

  do
  {
    if (HAL_I2C_Master_Receive(&hi2c1, SI4735_I2C_ADDRESS_HAL, &value, 1U,
                               BRINGUP_I2C_TIMEOUT_MS) == HAL_OK)
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
  } while ((HAL_GetTick() - start_tick) < BRINGUP_DEVICE_TIMEOUT_MS);

  return HAL_TIMEOUT;
}

static HAL_StatusTypeDef Bringup_RadioCommand(const uint8_t *command,
                                               uint16_t command_length,
                                               uint8_t *response,
                                               uint16_t response_length)
{
  uint8_t status = 0U;

  if ((command == NULL) || (command_length == 0U) ||
      (response == NULL) || (response_length == 0U))
  {
    return HAL_ERROR;
  }

  if (Bringup_RadioWaitCts(NULL) != HAL_OK)
  {
    return HAL_TIMEOUT;
  }
  if (HAL_I2C_Master_Transmit(&hi2c1, SI4735_I2C_ADDRESS_HAL,
                              (uint8_t *)command, command_length,
                              BRINGUP_I2C_TIMEOUT_MS) != HAL_OK)
  {
    return HAL_ERROR;
  }
  if (Bringup_RadioWaitCts(&status) != HAL_OK)
  {
    return HAL_TIMEOUT;
  }

  if (response_length == 1U)
  {
    response[0] = status;
    return HAL_OK;
  }
  if (HAL_I2C_Master_Receive(&hi2c1, SI4735_I2C_ADDRESS_HAL, response,
                             response_length, BRINGUP_I2C_TIMEOUT_MS) != HAL_OK)
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

static bool Bringup_RadioProbe(void)
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
  if ((HAL_I2C_IsDeviceReady(&hi2c1, SI4735_I2C_ADDRESS_HAL, 3U,
                             BRINGUP_I2C_TIMEOUT_MS) != HAL_OK) ||
      (Bringup_RadioWaitCts(NULL) != HAL_OK))
  {
    printf("[radio] FAIL: no ready device at 0x11; I2C error=0x%08lX\r\n",
           (unsigned long)HAL_I2C_GetError(&hi2c1));
    goto done;
  }
  printf("[radio] I2C probe and CTS passed\r\n");

  if (Bringup_RadioCommand(power_up, sizeof(power_up), &status,
                           sizeof(status)) != HAL_OK)
  {
    printf("[radio] FAIL: FM analog POWER_UP\r\n");
    goto done;
  }
  if (Bringup_RadioCommand(&get_rev, sizeof(get_rev), response,
                           sizeof(response)) != HAL_OK)
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

  if (Bringup_RadioCommand(&power_down, sizeof(power_down), &status,
                           sizeof(status)) != HAL_OK)
  {
    printf("[radio] WARN: POWER_DOWN did not complete; asserting reset\r\n");
  }

done:
  HAL_GPIO_WritePin(RADIO_RST_GPIO_Port, RADIO_RST_Pin, GPIO_PIN_RESET);
  printf("[radio] reset asserted after smoke test\r\n");
  return passed;
}

typedef struct
{
  uint16_t reg;
  uint16_t value;
} CodecRegisterValue;

static bool CodecWriteChecked(uint16_t reg, uint16_t value)
{
  uint8_t data[2] = {(uint8_t)(value >> 8), (uint8_t)value};
  uint8_t readback_data[2] = {0U, 0U};
  uint16_t readback;
  uint16_t verify_mask = 0xFFFFU;

  if (HAL_I2C_Mem_Write(&hi2c4, SGTL5000_I2C_ADDRESS_HAL, reg,
                        I2C_MEMADD_SIZE_16BIT, data, sizeof(data),
                        BRINGUP_I2C_TIMEOUT_MS) != HAL_OK ||
      HAL_I2C_Mem_Read(&hi2c4, SGTL5000_I2C_ADDRESS_HAL, reg,
                       I2C_MEMADD_SIZE_16BIT, readback_data,
                       sizeof(readback_data), BRINGUP_I2C_TIMEOUT_MS) != HAL_OK)
  {
    printf("[audio] FAIL: codec register 0x%04X I2C access\r\n", reg);
    return false;
  }

  readback = ((uint16_t)readback_data[0] << 8) | readback_data[1];
  if (reg == SGTL5000_CHIP_ADCDAC_CTRL)
  {
    /* DAC volume-ramp busy flags are live read-only bits. */
    verify_mask = 0xCFFFU;
  }
  if ((readback & verify_mask) != (value & verify_mask))
  {
    printf("[audio] FAIL: codec reg 0x%04X wrote 0x%04X read 0x%04X\r\n",
           reg, value, readback);
    return false;
  }
  return true;
}

static bool CodecSetHeadphoneMute(bool mute)
{
  const bool effective_mute = mute || (volume_ready && volume_last_muted);
  const uint8_t volume_code = volume_ready
    ? volume_last_code : RADIO_AUDIO_HP_VOLUME_CODE;

  if (effective_mute)
  {
    if (!CodecWriteChecked(SGTL5000_CHIP_ANA_CTRL, 0x0133U) ||
        !CodecWriteChecked(SGTL5000_CHIP_ADCDAC_CTRL, 0x020CU))
    {
      return false;
    }
  }

  if (!CodecWriteChecked(SGTL5000_CHIP_ANA_HP_CTRL,
                         ((uint16_t)volume_code << 8) | volume_code))
  {
    return false;
  }

  if (!effective_mute)
  {
    return CodecWriteChecked(SGTL5000_CHIP_ADCDAC_CTRL, 0x0200U) &&
           CodecWriteChecked(SGTL5000_CHIP_ANA_CTRL, 0x0123U);
  }
  return true;
}

static bool VolumeReadRaw(uint16_t *raw)
{
  if ((raw == NULL) || (HAL_ADC_Start(&hadc3) != HAL_OK) ||
      (HAL_ADC_PollForConversion(&hadc3, 2U) != HAL_OK))
  {
    (void)HAL_ADC_Stop(&hadc3);
    return false;
  }
  *raw = (uint16_t)HAL_ADC_GetValue(&hadc3);
  (void)HAL_ADC_Stop(&hadc3);
  return true;
}

static bool VolumeControlInit(void)
{
  uint32_t initial_sum = 0U;

  volume_ready = false;
  if (HAL_ADCEx_Calibration_Start(&hadc3, ADC_CALIB_OFFSET_LINEARITY,
                                  ADC_SINGLE_ENDED) != HAL_OK)
  {
    printf("[audio] FAIL: ADC3 calibration for volume pot\r\n");
    return false;
  }
  for (uint32_t index = 0U; index < 8U; ++index)
  {
    uint16_t raw;
    if (!VolumeReadRaw(&raw))
    {
      printf("[audio] FAIL: volume-pot ADC conversion\r\n");
      return false;
    }
    initial_sum += raw;
  }
  volume_filtered = initial_sum / 8U;
  volume_last_sample_tick = HAL_GetTick();
  volume_last_code = RADIO_AUDIO_HP_VOLUME_CODE;
  volume_last_muted = true;
  volume_ready = true;
  printf("[audio] volume pot PF10/ADC3 ready; initial ADC=%lu\r\n",
         (unsigned long)volume_filtered);
  return true;
}

static bool VolumeControlService(bool force)
{
  const uint32_t now = HAL_GetTick();
  uint32_t scaled;
  uint32_t code_delta;
  uint16_t raw;
  uint8_t code;
  bool muted;
  uint8_t previous_code;
  bool previous_muted;

  if (!volume_ready)
  {
    return false;
  }
  if (!force && ((now - volume_last_sample_tick) < VOLUME_SAMPLE_PERIOD_MS))
  {
    return true;
  }
  volume_last_sample_tick = now;
  if (!VolumeReadRaw(&raw))
  {
    printf("[audio] FAIL: volume-pot ADC conversion\r\n");
    return false;
  }

  /* A 1/8 IIR filter rejects wiper noise without making the knob sluggish. */
  volume_filtered = ((volume_filtered * 7U) + raw + 4U) / 8U;
  muted = volume_filtered <= VOLUME_MUTE_THRESHOLD;
  if (muted)
  {
    code = HP_VOLUME_MIN_CODE;
  }
  else
  {
    scaled = ((volume_filtered - VOLUME_MUTE_THRESHOLD) *
              (HP_VOLUME_MIN_CODE - HP_VOLUME_0DB_CODE)) /
             (65535U - VOLUME_MUTE_THRESHOLD);
    code = (uint8_t)(HP_VOLUME_MIN_CODE - scaled);
  }

  code_delta = (code > volume_last_code) ? (code - volume_last_code)
                                         : (volume_last_code - code);
  /* One-dB hysteresis plus the codec zero-cross detector limits zipper noise. */
  if (!force && (muted == volume_last_muted) && (code_delta < 2U))
  {
    return true;
  }

  previous_code = volume_last_code;
  previous_muted = volume_last_muted;
  volume_last_code = code;
  volume_last_muted = muted;
  if (muted != previous_muted)
  {
    if (!CodecSetHeadphoneMute(!radio_audio_headphones_unmuted))
    {
      volume_last_code = previous_code;
      volume_last_muted = previous_muted;
      return false;
    }
  }
  else if (!CodecWriteChecked(SGTL5000_CHIP_ANA_HP_CTRL,
                              ((uint16_t)code << 8) | code))
  {
    volume_last_code = previous_code;
    volume_last_muted = previous_muted;
    return false;
  }

  if (muted)
  {
    printf("[audio] volume ADC=%lu MUTED\r\n",
           (unsigned long)volume_filtered);
  }
  else
  {
    const uint32_t attenuation_half_db = code - HP_VOLUME_0DB_CODE;
    printf("[audio] volume ADC=%lu -%lu.%lu dB\r\n",
           (unsigned long)volume_filtered,
           (unsigned long)(attenuation_half_db / 2U),
           (unsigned long)((attenuation_half_db & 1U) ? 5U : 0U));
  }
  return true;
}

static bool CodecStartDigitalHeadphones(void)
{
  static const CodecRegisterValue startup[] =
  {
    {SGTL5000_CHIP_ANA_CTRL,    0x0133U},
    {SGTL5000_CHIP_ADCDAC_CTRL, 0x020CU},
    {SGTL5000_CHIP_ANA_POWER,   0x4260U},
    {SGTL5000_CHIP_LINREG_CTRL, 0x006CU},
    {SGTL5000_CHIP_REF_CTRL,    0x01EFU},
    {SGTL5000_CHIP_SHORT_CTRL,  0x1106U},
    {SGTL5000_CHIP_CLK_CTRL,    0x0008U},
    {SGTL5000_CHIP_I2S_CTRL,    0x0130U},
    {SGTL5000_CHIP_SSS_CTRL,    0x0010U},
    {SGTL5000_CHIP_DAC_VOL,     0x3C3CU},
    {SGTL5000_CHIP_ANA_HP_CTRL, 0x7F7FU},
    {SGTL5000_CHIP_DIG_POWER,   0x0021U},
    {SGTL5000_CHIP_ANA_POWER,   0x42FCU}
  };
  uint8_t chip_id_data[2];
  uint16_t chip_id;
  uint32_t mclk_hz = 0U;

  printf("\r\n[audio] SGTL5000 I2S -> DAC -> headphone setup\r\n");
  if (Bringup_CodecStartClock(&mclk_hz) != HAL_OK)
  {
    printf("[audio] FAIL: SAI1 MCLK start\r\n");
    return false;
  }
  printf("[audio] PE2 MCLK approximately %lu Hz\r\n",
         (unsigned long)mclk_hz);

  if (HAL_I2C_Mem_Read(&hi2c4, SGTL5000_I2C_ADDRESS_HAL,
                       SGTL5000_CHIP_ID_REGISTER, I2C_MEMADD_SIZE_16BIT,
                       chip_id_data, sizeof(chip_id_data),
                       BRINGUP_I2C_TIMEOUT_MS) != HAL_OK)
  {
    printf("[audio] FAIL: CHIP_ID read\r\n");
    return false;
  }
  chip_id = ((uint16_t)chip_id_data[0] << 8) | chip_id_data[1];
  if ((chip_id >> 8) != 0xA0U)
  {
    printf("[audio] FAIL: CHIP_ID=0x%04X\r\n", chip_id);
    return false;
  }

  for (uint32_t index = 0U; index < sizeof(startup) / sizeof(startup[0]);
       ++index)
  {
    if (!CodecWriteChecked(startup[index].reg, startup[index].value))
    {
      return false;
    }
  }
  HAL_Delay(450U);
  printf("[audio] PASS: CHIP_ID=0x%04X; headphone path configured muted\r\n",
         chip_id);
  return true;
}

static bool RadioSetProperty(uint16_t property, uint16_t value)
{
  const uint8_t command[] = {
    SI4735_CMD_SET_PROPERTY, 0U,
    (uint8_t)(property >> 8), (uint8_t)property,
    (uint8_t)(value >> 8), (uint8_t)value
  };
  uint8_t status;

  if (Bringup_RadioCommand(command, sizeof(command), &status,
                           sizeof(status)) != HAL_OK)
  {
    printf("[radio] FAIL: SET_PROPERTY 0x%04X=0x%04X\r\n",
           property, value);
    return false;
  }
  HAL_Delay(10U);
  return true;
}

static uint32_t __attribute__((optimize("O3")))
RadioAudioMeasureFs(GPIO_TypeDef *port, uint32_t pin)
{
  enum { PROBE_PERIODS = 64U };
  volatile uint32_t * const idr = &port->IDR;
  const uint32_t timeout_cycles = SystemCoreClock / 100U;
  uint32_t primask;
  uint32_t wait_start;
  uint32_t start_cycles;
  uint32_t elapsed_cycles;
  uint32_t captured = 0U;
  bool timed_out = false;

  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  primask = __get_PRIMASK();
  __disable_irq();

  /* Synchronize to one rising frame-sync edge. */
  wait_start = DWT->CYCCNT;
  while ((*idr & pin) != 0U)
  {
    if ((DWT->CYCCNT - wait_start) > timeout_cycles)
    {
      timed_out = true;
      break;
    }
  }
  wait_start = DWT->CYCCNT;
  while (!timed_out && ((*idr & pin) == 0U))
  {
    if ((DWT->CYCCNT - wait_start) > timeout_cycles)
    {
      timed_out = true;
      break;
    }
  }

  start_cycles = DWT->CYCCNT;
  while (!timed_out && (captured < PROBE_PERIODS))
  {
    wait_start = DWT->CYCCNT;
    while ((*idr & pin) != 0U)
    {
      if ((DWT->CYCCNT - wait_start) > timeout_cycles)
      {
        timed_out = true;
        break;
      }
    }
    wait_start = DWT->CYCCNT;
    while (!timed_out && ((*idr & pin) == 0U))
    {
      if ((DWT->CYCCNT - wait_start) > timeout_cycles)
      {
        timed_out = true;
        break;
      }
    }
    if (!timed_out)
    {
      ++captured;
    }
  }
  elapsed_cycles = DWT->CYCCNT - start_cycles;
  __set_PRIMASK(primask);

  if (timed_out || (elapsed_cycles == 0U))
  {
    return 0U;
  }
  return (uint32_t)(((uint64_t)captured * SystemCoreClock) / elapsed_cycles);
}

static bool RadioSai2Start(void)
{
  uint32_t sai2_kernel_hz;
  uint32_t measured_fs_hz;

  memset(&hsai_BlockA2, 0, sizeof(hsai_BlockA2));
  hsai_BlockA2.Instance = SAI2_Block_A;
  hsai_BlockA2.Init.AudioMode = SAI_MODEMASTER_RX;
  hsai_BlockA2.Init.Synchro = SAI_ASYNCHRONOUS;
  hsai_BlockA2.Init.OutputDrive = SAI_OUTPUTDRIVE_ENABLE;
  hsai_BlockA2.Init.NoDivider = SAI_MASTERDIVIDER_DISABLE;
  hsai_BlockA2.Init.MckOverSampling = SAI_MCK_OVERSAMPLING_DISABLE;
  hsai_BlockA2.Init.MckOutput = SAI_MCK_OUTPUT_DISABLE;
  hsai_BlockA2.Init.FIFOThreshold = SAI_FIFOTHRESHOLD_1QF;
  hsai_BlockA2.Init.AudioFrequency = SAI_AUDIO_FREQUENCY_MCKDIV;
  hsai_BlockA2.Init.Mckdiv = 16U;
  hsai_BlockA2.Init.SynchroExt = SAI_SYNCEXT_DISABLE;
  hsai_BlockA2.Init.MonoStereoMode = SAI_STEREOMODE;
  hsai_BlockA2.Init.CompandingMode = SAI_NOCOMPANDING;
  hsai_BlockA2.Init.TriState = SAI_OUTPUT_NOTRELEASED;

  /* Use the wire-native 32-bit stereo frame. The sibling jumper experiment's
     64-bit compensation made this board capture at about 24 kHz, which the
     48 kHz codec then replayed one octave high. */
  if (HAL_SAI_InitProtocol(&hsai_BlockA2, SAI_I2S_STANDARD,
                           SAI_PROTOCOL_DATASIZE_16BIT, 2U) != HAL_OK)
  {
    printf("[sai2] FAIL: SAI2A init error=0x%08lX\r\n",
           (unsigned long)HAL_SAI_GetError(&hsai_BlockA2));
    return false;
  }

  sai2_kernel_hz = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_SAI2);
  if ((sai2_kernel_hz == 0U) ||
      ((uint64_t)HAL_RCC_GetPCLK2Freq() < (2ULL * sai2_kernel_hz)))
  {
    printf("[sai2] FAIL: clocks PCLK2=%lu SAI2=%lu Hz\r\n",
           (unsigned long)HAL_RCC_GetPCLK2Freq(),
           (unsigned long)sai2_kernel_hz);
    return false;
  }

  memset(&hdma_sai2_a, 0, sizeof(hdma_sai2_a));
  hdma_sai2_a.Instance = DMA1_Stream4;
  hdma_sai2_a.Init.Request = DMA_REQUEST_SAI2_A;
  hdma_sai2_a.Init.Direction = DMA_PERIPH_TO_MEMORY;
  hdma_sai2_a.Init.PeriphInc = DMA_PINC_DISABLE;
  hdma_sai2_a.Init.MemInc = DMA_MINC_ENABLE;
  hdma_sai2_a.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
  hdma_sai2_a.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
  hdma_sai2_a.Init.Mode = DMA_CIRCULAR;
  hdma_sai2_a.Init.Priority = DMA_PRIORITY_VERY_HIGH;
  hdma_sai2_a.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
  if (HAL_DMA_Init(&hdma_sai2_a) != HAL_OK)
  {
    printf("[sai2] FAIL: DMA1 Stream4 init error=0x%08lX\r\n",
           (unsigned long)HAL_DMA_GetError(&hdma_sai2_a));
    return false;
  }
  __HAL_LINKDMA(&hsai_BlockA2, hdmarx, hdma_sai2_a);
  HAL_NVIC_SetPriority(DMA1_Stream4_IRQn, 0U, 0U);
  HAL_NVIC_EnableIRQ(DMA1_Stream4_IRQn);
  HAL_NVIC_SetPriority(SAI2_IRQn, 0U, 0U);
  HAL_NVIC_EnableIRQ(SAI2_IRQn);

  memset(radio_audio_rx_buffer, 0, sizeof(radio_audio_rx_buffer));
  memset(radio_audio_tx_buffer, 0, sizeof(radio_audio_tx_buffer));
#if (__DCACHE_PRESENT == 1U)
  if ((SCB->CCR & SCB_CCR_DC_Msk) != 0U)
  {
    SCB_CleanDCache_by_Addr((uint32_t *)radio_audio_rx_buffer,
                            sizeof(radio_audio_rx_buffer));
    SCB_CleanDCache_by_Addr((uint32_t *)radio_audio_tx_buffer,
                            sizeof(radio_audio_tx_buffer));
  }
#endif
  radio_audio_rx_half_count = 0U;
  radio_audio_rx_full_count = 0U;
  radio_audio_error_flags = 0U;
  radio_audio_copy_enabled = false;
  if (HAL_SAI_Receive_DMA(&hsai_BlockA2, (uint8_t *)radio_audio_rx_buffer,
                          RADIO_AUDIO_BUFFER_SAMPLES) != HAL_OK)
  {
    printf("[sai2] FAIL: RX DMA start error=0x%08lX\r\n",
           (unsigned long)HAL_SAI_GetError(&hsai_BlockA2));
    return false;
  }
  measured_fs_hz = RadioAudioMeasureFs(GPIOD, GPIO_PIN_12);
  printf("[sai2] PD11 RX, PD12 FS, PD13 SCK; kernel=%lu Hz; "
         "measured FS=%lu Hz\r\n", (unsigned long)sai2_kernel_hz,
         (unsigned long)measured_fs_hz);
  printf("[sai2] frame=%lu bits active=%lu bits slots=%lu MCKDIV=%lu; "
         "expect 48 kHz/1.536 MHz\r\n",
         (unsigned long)hsai_BlockA2.FrameInit.FrameLength,
         (unsigned long)hsai_BlockA2.FrameInit.ActiveFrameLength,
         (unsigned long)hsai_BlockA2.SlotInit.SlotNumber,
         (unsigned long)hsai_BlockA2.Init.Mckdiv);
  if ((measured_fs_hz < 47500U) || (measured_fs_hz > 48500U))
  {
    printf("[sai2] FAIL: radio frame sync is not 48 kHz\r\n");
    (void)HAL_SAI_DMAStop(&hsai_BlockA2);
    return false;
  }
  return true;
}

static bool RadioTuneFrequency(RadioBand band, uint32_t frequency_khz,
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
  config = &radio_band_configs[band];
  if ((frequency_khz < config->minimum_khz) ||
      (frequency_khz > config->maximum_khz))
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

  if (Bringup_RadioCommand(tune, tune_length, &status,
                           sizeof(status)) != HAL_OK)
  {
    return false;
  }

  start_tick = HAL_GetTick();
  do
  {
    if (Bringup_RadioCommand(&get_interrupt, sizeof(get_interrupt), &status,
                             sizeof(status)) != HAL_OK)
    {
      return false;
    }
    if ((status & SI4735_STATUS_STCINT) != 0U)
    {
      break;
    }
    HAL_Delay(2U);
  } while ((HAL_GetTick() - start_tick) < BRINGUP_DEVICE_TIMEOUT_MS);

  if ((status & SI4735_STATUS_STCINT) == 0U ||
      Bringup_RadioCommand(get_tune_status, sizeof(get_tune_status), response,
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

static bool RadioPowerUpBand(RadioBand band, uint32_t frequency_khz,
                             RadioTuneStatus *tune_status)
{
  const RadioBandConfig *config;
  uint8_t power_up[3];
  uint8_t status = 0U;

  if ((band >= RADIO_BAND_COUNT) || (tune_status == NULL))
  {
    return false;
  }
  config = &radio_band_configs[band];
  HAL_GPIO_WritePin(RADIO_SW_SWITCH_GPIO_Port, RADIO_SW_SWITCH_Pin,
                    config->select_whip ? GPIO_PIN_SET : GPIO_PIN_RESET);
  power_up[0] = SI4735_CMD_POWER_UP;
  power_up[1] = config->power_up_function;
  power_up[2] = SI4735_POWER_UP_FM_DIGITAL;

  if (Bringup_RadioCommand(power_up, sizeof(power_up), &status,
                           sizeof(status)) != HAL_OK)
  {
    printf("[radio] FAIL: %s digital POWER_UP\r\n", config->name);
    return false;
  }
  if (!RadioSetProperty(SI4735_PROP_REFCLK_FREQ, 32768U) ||
      !RadioSetProperty(SI4735_PROP_REFCLK_PRESCALE, 1U) ||
      !RadioSetProperty(SI4735_PROP_RX_VOLUME, 50U) ||
      !RadioSetProperty(SI4735_PROP_RX_HARD_MUTE, 0U))
  {
    return false;
  }

  /* AM-family digital clocks do not start until the first tune completes. */
  if (!RadioTuneFrequency(band, frequency_khz, tune_status))
  {
    printf("[radio] FAIL: %s tune timeout/status\r\n", config->name);
    return false;
  }

  if (!RadioSetProperty(SI4735_PROP_DIGITAL_RATE,
                        RADIO_AUDIO_SAMPLE_RATE_HZ) ||
      !RadioSetProperty(SI4735_PROP_DIGITAL_FORMAT, 0x0000U))
  {
    return false;
  }
  return true;
}

static void RadioLogTune(const char *prefix,
                         const RadioTuneStatus *tune_status)
{
  const RadioBandConfig *config = &radio_band_configs[tune_status->band];

  printf("[radio] %s %s %lu kHz (%lu.%03lu MHz): "
         "RSSI=%u dBuV SNR=%u dB valid=%u\r\n",
         prefix, config->name,
         (unsigned long)tune_status->frequency_khz,
         (unsigned long)(tune_status->frequency_khz / 1000U),
         (unsigned long)(tune_status->frequency_khz % 1000U),
         tune_status->rssi_dbuv, tune_status->snr_db, tune_status->valid);
}

static bool RadioTuneAndEnableDigital(void)
{
  RadioTuneStatus tune_status;

  printf("\r\n[radio] Si4735 digital multi-band setup\r\n");
  HAL_GPIO_WritePin(RADIO_RST_GPIO_Port, RADIO_RST_Pin, GPIO_PIN_RESET);
  HAL_Delay(2U);
  HAL_GPIO_WritePin(RADIO_RST_GPIO_Port, RADIO_RST_Pin, GPIO_PIN_SET);
  HAL_Delay(15U);
  if ((Bringup_RadioWaitCts(NULL) != HAL_OK) ||
      !RadioPowerUpBand(RADIO_BAND_FM, RADIO_AUDIO_FREQUENCY_KHZ,
                        &tune_status))
  {
    return false;
  }
  RadioLogTune("tuned", &tune_status);
  radio_audio_rx_half_count = 0U;
  radio_audio_rx_full_count = 0U;
  printf("[radio] digital output enabled: 48 kHz, 16-bit stereo I2S\r\n");
  return true;
}

static bool RadioSwitchBand(RadioBand band, RadioTuneStatus *tune_status)
{
  const uint8_t power_down = SI4735_CMD_POWER_DOWN;
  const RadioBandConfig *config;
  uint8_t status;
  bool restore_headphones;

  if ((band >= RADIO_BAND_COUNT) || (tune_status == NULL))
  {
    return false;
  }
  config = &radio_band_configs[band];
  restore_headphones = radio_audio_headphones_unmuted;
  if (!CodecSetHeadphoneMute(true))
  {
    goto failed;
  }
  radio_audio_copy_enabled = false;

  /* Remove digital output cleanly before changing receiver function. */
  if (!RadioSetProperty(SI4735_PROP_DIGITAL_RATE, 0U) ||
      (Bringup_RadioCommand(&power_down, sizeof(power_down), &status,
                            sizeof(status)) != HAL_OK))
  {
    goto failed;
  }
  HAL_Delay(1U);
  if (!RadioPowerUpBand(band, radio_last_frequency_khz[band], tune_status))
  {
    goto failed;
  }

  radio_audio_copy_enabled = true;
  if (restore_headphones && !CodecSetHeadphoneMute(false))
  {
    goto failed;
  }
  RadioLogTune("switched to", tune_status);
  printf("[radio] antenna path=%s\r\n",
         config->select_whip ? "SW whip" :
         (band == RADIO_BAND_FM ? "FM input" : "AM/LW loop"));
  return true;

failed:
  radio_audio_copy_enabled = false;
  radio_audio_running = false;
  radio_audio_headphones_unmuted = false;
  (void)CodecSetHeadphoneMute(true);
  HAL_GPIO_WritePin(RADIO_RST_GPIO_Port, RADIO_RST_Pin, GPIO_PIN_RESET);
  BSP_LED_Off(LED_GREEN);
  BSP_LED_On(LED_RED);
  printf("[radio] FAIL: band switch; receiver reset and output muted\r\n");
  return false;
}

static void RadioAudioCopyHalf(uint32_t offset)
{
  const uint32_t half_samples = RADIO_AUDIO_BUFFER_SAMPLES / 2U;
#if (__DCACHE_PRESENT == 1U)
  if ((SCB->CCR & SCB_CCR_DC_Msk) != 0U)
  {
    SCB_InvalidateDCache_by_Addr((uint32_t *)&radio_audio_rx_buffer[offset],
                                 half_samples * sizeof(uint16_t));
  }
#endif
  RadioRecorder_OnRadioSamples(
    (const int16_t *)&radio_audio_rx_buffer[offset], half_samples);
  memcpy(&radio_audio_tx_buffer[offset], &radio_audio_rx_buffer[offset],
         half_samples * sizeof(uint16_t));
#if (__DCACHE_PRESENT == 1U)
  if ((SCB->CCR & SCB_CCR_DC_Msk) != 0U)
  {
    SCB_CleanDCache_by_Addr((uint32_t *)&radio_audio_tx_buffer[offset],
                            half_samples * sizeof(uint16_t));
  }
#endif
}

static bool RadioAudioBridgeStart(void)
{
  uint32_t observed;
  uint32_t start_tick;
  uint32_t measured_fs_hz;

  start_tick = HAL_GetTick();
  while ((radio_audio_rx_full_count == 0U) &&
         (radio_audio_error_flags == 0U) &&
         ((HAL_GetTick() - start_tick) < RADIO_AUDIO_START_TIMEOUT_MS))
  {
    HAL_Delay(1U);
  }
  if ((radio_audio_rx_full_count == 0U) || (radio_audio_error_flags != 0U))
  {
    printf("[bridge] FAIL: no complete radio PCM buffer (flags=0x%08lX)\r\n",
           (unsigned long)radio_audio_error_flags);
    return false;
  }

  /* At a full callback, RX is writing half 0 and half 1 is stable. */
  RadioAudioCopyHalf(RADIO_AUDIO_BUFFER_SAMPLES / 2U);
  __disable_irq();
  observed = radio_audio_rx_half_count;
  radio_audio_copy_enabled = true;
  __enable_irq();
  start_tick = HAL_GetTick();
  while ((radio_audio_rx_half_count == observed) &&
         (radio_audio_error_flags == 0U) &&
         ((HAL_GetTick() - start_tick) < RADIO_AUDIO_START_TIMEOUT_MS))
  {
    HAL_Delay(1U);
  }
  if ((radio_audio_rx_half_count == observed) ||
      (radio_audio_error_flags != 0U))
  {
    printf("[bridge] FAIL: DMA prefill synchronization\r\n");
    radio_audio_copy_enabled = false;
    return false;
  }

  if (HAL_SAI_Transmit_DMA(&hsai_BlockA1, (uint8_t *)radio_audio_tx_buffer,
                           RADIO_AUDIO_BUFFER_SAMPLES) != HAL_OK)
  {
    printf("[bridge] FAIL: SAI1 TX DMA error=0x%08lX\r\n",
           (unsigned long)HAL_SAI_GetError(&hsai_BlockA1));
    radio_audio_copy_enabled = false;
    return false;
  }
  measured_fs_hz = RadioAudioMeasureFs(GPIOE, GPIO_PIN_4);
  printf("[bridge] SAI1 PE4 measured FS=%lu Hz\r\n",
         (unsigned long)measured_fs_hz);
  if ((measured_fs_hz < 47500U) || (measured_fs_hz > 48500U))
  {
    printf("[bridge] FAIL: codec frame sync is not 48 kHz\r\n");
    radio_audio_copy_enabled = false;
    (void)HAL_SAI_DMAStop(&hsai_BlockA1);
    return false;
  }
  radio_audio_running = true;
  printf("[bridge] PASS: SAI2 RX DMA -> SAI1 TX DMA is running\r\n");
  return true;
}

static bool RadioAudioStart(void)
{
  radio_audio_running = false;
  radio_audio_headphones_unmuted = false;
  if (!VolumeControlInit() || !CodecStartDigitalHeadphones() ||
      !VolumeControlService(true) || !RadioSai2Start() ||
      !RadioTuneAndEnableDigital() || !RadioAudioBridgeStart())
  {
    (void)CodecSetHeadphoneMute(true);
    HAL_GPIO_WritePin(RADIO_RST_GPIO_Port, RADIO_RST_Pin, GPIO_PIN_RESET);
    return false;
  }
  return true;
}

static bool RadioParseFrequencyKHz(const char *text, uint32_t *frequency_khz)
{
  uint32_t value = 0U;
  bool have_digit = false;

  if ((text == NULL) || (frequency_khz == NULL))
  {
    return false;
  }
  while ((*text == ' ') || (*text == '\t'))
  {
    ++text;
  }
  while ((*text >= '0') && (*text <= '9'))
  {
    have_digit = true;
    if (value > 1000000U)
    {
      return false;
    }
    value = (value * 10U) + (uint32_t)(*text - '0');
    ++text;
  }
  while ((*text == ' ') || (*text == '\t'))
  {
    ++text;
  }
  if (!have_digit || (*text != '\0'))
  {
    return false;
  }
  *frequency_khz = value;
  return true;
}

static bool RadioParseBand(const char *text, RadioBand *band)
{
  if ((text == NULL) || (band == NULL))
  {
    return false;
  }
  while ((*text == ' ') || (*text == '\t'))
  {
    ++text;
  }
  for (uint32_t index = 0U; index < RADIO_BAND_COUNT; ++index)
  {
    if (strcmp(text, radio_band_configs[index].name) == 0)
    {
      *band = (RadioBand)index;
      return true;
    }
  }
  return false;
}

static void RadioUsbSendStatus(const RadioTuneStatus *tune_status)
{
  const RadioBandConfig *config;
  char response[160];

  if ((tune_status == NULL) || (tune_status->band >= RADIO_BAND_COUNT))
  {
    (void)UsbTest_SendText("ERR RADIO status unavailable\r\n");
    return;
  }
  config = &radio_band_configs[tune_status->band];
  (void)snprintf(response, sizeof(response),
                 "OK RADIO BAND=%s FREQ=%lu kHz (%lu.%03lu MHz) "
                 "RSSI=%u SNR=%u VALID=%u\r\n",
                 config->name,
                 (unsigned long)tune_status->frequency_khz,
                 (unsigned long)(tune_status->frequency_khz / 1000U),
                 (unsigned long)(tune_status->frequency_khz % 1000U),
                 tune_status->rssi_dbuv,
                 tune_status->snr_db,
                 tune_status->valid);
  (void)UsbTest_SendText(response);
}

static void RadioUsbSendBand(void)
{
  const RadioBandConfig *config = &radio_band_configs[radio_band];
  char response[128];

  (void)snprintf(response, sizeof(response),
                 "OK RADIO BAND=%s RANGE=%lu..%lu kHz STEP=%lu kHz\r\n",
                 config->name, (unsigned long)config->minimum_khz,
                 (unsigned long)config->maximum_khz,
                 (unsigned long)config->step_khz);
  (void)UsbTest_SendText(response);
}

static void VolumeUsbSendStatus(void)
{
  char response[112];
  uint32_t level_percent;

  if (!volume_ready)
  {
    (void)UsbTest_SendText("ERR VOLUME pot unavailable\r\n");
    return;
  }
  level_percent = (volume_filtered * 100U + 32767U) / 65535U;
  if (volume_last_muted)
  {
    (void)snprintf(response, sizeof(response),
                   "OK VOLUME ADC=%lu LEVEL=%lu%% MUTED=1\r\n",
                   (unsigned long)volume_filtered,
                   (unsigned long)level_percent);
  }
  else
  {
    const uint32_t attenuation_half_db =
      volume_last_code - HP_VOLUME_0DB_CODE;
    (void)snprintf(response, sizeof(response),
                   "OK VOLUME ADC=%lu LEVEL=%lu%% ATTEN=-%lu.%lu dB MUTED=0\r\n",
                   (unsigned long)volume_filtered,
                   (unsigned long)level_percent,
                   (unsigned long)(attenuation_half_db / 2U),
                   (unsigned long)((attenuation_half_db & 1U) ? 5U : 0U));
  }
  (void)UsbTest_SendText(response);
}

static void UsbCliCommand(const char *line)
{
  char command[64];
  const RadioBandConfig *config;
  RadioBand target_band;
  RadioTuneStatus tune_status;
  uint32_t frequency_khz;
  uint32_t target_frequency_khz;
  size_t length;

  if (line == NULL)
  {
    return;
  }
  while ((*line == ' ') || (*line == '\t'))
  {
    ++line;
  }
  length = strlen(line);
  while ((length > 0U) &&
         ((line[length - 1U] == ' ') || (line[length - 1U] == '\t')))
  {
    --length;
  }
  if ((length == 0U) || (length >= sizeof(command)))
  {
    (void)UsbTest_SendText("ERR empty or oversized command\r\n");
    return;
  }
  for (size_t index = 0U; index < length; ++index)
  {
    char character = line[index];
    command[index] = ((character >= 'a') && (character <= 'z'))
      ? (char)(character - ('a' - 'A')) : character;
  }
  command[length] = '\0';

  /* A WAV transfer is a framed binary exchange. Do not interleave replies from
   * other command handlers while its host is waiting for a frame. */
  if (WavTransfer_IsActive())
  {
    (void)WavTransfer_HandleCommand(command);
    return;
  }

  if (Diagnostics_HandleCommand(command))
  {
    return;
  }
  if (strcmp(command, "SLEEP START") == 0)
  {
#if defined(SPOOKY_IPC_SMOKE)
    (void)UsbTest_SendText("ERR SLEEP unavailable in IPC smoke build; use Debug\r\n");
#else
    PrototypePower_RequestSleep();
    (void)UsbTest_SendText(
      "OK SLEEP START; CDC will disconnect; updates continue on AUX UART7\r\n");
#endif
    return;
  }
  if (IpcSmokeCli_HandleCommand(command))
  {
    return;
  }
  if (MagnetometerTest_HandleCommand(command))
  {
    return;
  }
  if (UiBoardTest_HandleCommand(command))
  {
    return;
  }
  if ((strncmp(command, "WAV", 3U) == 0) &&
      ((command[3] == '\0') || (command[3] == ' ') ||
       (command[3] == '\t')))
  {
    if (RadioRecorder_IsActive())
    {
      (void)UsbTest_SendText("ERR WAV unavailable while recording\r\n");
    }
    else
    {
      (void)WavTransfer_HandleCommand(command);
    }
    return;
  }
  if (RadioRecorder_HandleCommand(command, radio_audio_running))
  {
    return;
  }
  if (RadioRecorder_IsActive() &&
      (command[0] == 'S') && (command[1] == 'D') &&
      ((command[2] == '\0') || (command[2] == ' ') ||
       (command[2] == '\t')))
  {
    (void)UsbTest_SendText("ERR SD unavailable while recording\r\n");
    return;
  }
  if (SdTest_HandleCommand(command))
  {
    return;
  }
  if ((strcmp(command, "BATTERY") == 0) ||
      (strcmp(command, "BATTERY READ") == 0) ||
      (strcmp(command, "BATTERY STATUS") == 0))
  {
    BoardDiagnostics_SendBatteryStatus(false);
    return;
  }
  if ((strcmp(command, "CHARGE") == 0) ||
      (strcmp(command, "CHARGE READ") == 0) ||
      (strcmp(command, "CHARGE STATUS") == 0))
  {
    BoardDiagnostics_SendBatteryStatus(true);
    return;
  }
  if (!radio_audio_running)
  {
    (void)UsbTest_SendText("ERR RADIO audio path is not running\r\n");
    return;
  }
  if (strcmp(command, "STATUS") == 0)
  {
    RadioUsbSendStatus(&radio_tune_status);
    return;
  }
  if ((strcmp(command, "VOLUME") == 0) ||
      (strcmp(command, "VOLUME READ") == 0) ||
      (strcmp(command, "VOLUME STATUS") == 0))
  {
    if (!VolumeControlService(true))
    {
      (void)UsbTest_SendText("ERR VOLUME ADC read failed\r\n");
      return;
    }
    VolumeUsbSendStatus();
    return;
  }
  if (RadioRecorder_IsActive() &&
      ((strncmp(command, "BAND", 4U) == 0) ||
       (strncmp(command, "TUNE", 4U) == 0) ||
       (strcmp(command, "UP") == 0) ||
       (strcmp(command, "DOWN") == 0)))
  {
    (void)UsbTest_SendText("ERR RADIO tuning disabled while recording\r\n");
    return;
  }
  if (strcmp(command, "BAND") == 0)
  {
    RadioUsbSendBand();
    return;
  }
  if ((strncmp(command, "BAND", 4U) == 0) &&
      ((command[4] == ' ') || (command[4] == '\t')))
  {
    if (!RadioParseBand(&command[5], &target_band))
    {
      (void)UsbTest_SendText("ERR usage: BAND FM|AM|SW|LW\r\n");
      return;
    }
    if (!RadioSwitchBand(target_band, &tune_status))
    {
      (void)UsbTest_SendText(
        "ERR RADIO band switch failed; reset required\r\n");
      return;
    }
    RadioUsbSendStatus(&tune_status);
    return;
  }

  config = &radio_band_configs[radio_band];

  if (strcmp(command, "UP") == 0)
  {
    target_frequency_khz =
      (radio_tune_status.frequency_khz <=
       (config->maximum_khz - config->step_khz))
        ? radio_tune_status.frequency_khz + config->step_khz
        : config->maximum_khz;
  }
  else if (strcmp(command, "DOWN") == 0)
  {
    target_frequency_khz =
      (radio_tune_status.frequency_khz >=
       (config->minimum_khz + config->step_khz))
        ? radio_tune_status.frequency_khz - config->step_khz
        : config->minimum_khz;
  }
  else if ((strncmp(command, "TUNE", 4U) == 0) &&
           ((command[4] == ' ') || (command[4] == '\t')))
  {
    if (!RadioParseFrequencyKHz(&command[5], &frequency_khz))
    {
      (void)UsbTest_SendText("ERR usage: TUNE <frequency-kHz>\r\n");
      return;
    }
    if ((frequency_khz < config->minimum_khz) ||
        (frequency_khz > config->maximum_khz))
    {
      char response[96];
      (void)snprintf(response, sizeof(response),
                     "ERR %s range: %lu..%lu kHz\r\n", config->name,
                     (unsigned long)config->minimum_khz,
                     (unsigned long)config->maximum_khz);
      (void)UsbTest_SendText(response);
      return;
    }
    target_frequency_khz = (radio_band == RADIO_BAND_FM)
      ? ((frequency_khz + 5U) / 10U) * 10U
      : frequency_khz;
  }
  else
  {
    (void)UsbTest_SendText("ERR unknown command; type HELP\r\n");
    return;
  }

  if (!RadioTuneFrequency(radio_band, target_frequency_khz, &tune_status))
  {
    (void)UsbTest_SendText("ERR RADIO tune failed\r\n");
    printf("[radio] USB %s tune failed at %lu kHz\r\n",
           config->name, (unsigned long)target_frequency_khz);
    return;
  }

  RadioLogTune("USB tuned", &tune_status);
  RadioUsbSendStatus(&tune_status);
}

static void RadioAudio_Service(void)
{
  bool inserted;

  if (!radio_audio_running)
  {
    return;
  }
  if (radio_audio_error_flags != 0U)
  {
    (void)CodecSetHeadphoneMute(true);
    radio_audio_running = false;
    BSP_LED_Off(LED_GREEN);
    BSP_LED_On(LED_RED);
    printf("[bridge] FAIL: runtime SAI/DMA flags=0x%08lX; output muted\r\n",
           (unsigned long)radio_audio_error_flags);
    return;
  }

  if (!VolumeControlService(false))
  {
    radio_audio_error_flags |= 8U;
    return;
  }

  inserted = HAL_GPIO_ReadPin(HEADPHONE_JACK_DETECT_GPIO_Port,
                              HEADPHONE_JACK_DETECT_Pin) == GPIO_PIN_RESET;
  if (inserted != radio_audio_headphones_unmuted)
  {
    if (!CodecSetHeadphoneMute(!inserted))
    {
      radio_audio_error_flags |= 4U;
      return;
    }
    radio_audio_headphones_unmuted = inserted;
    printf("[audio] headphones %s; output %s\r\n",
           inserted ? "inserted" : "removed",
           inserted ? (volume_last_muted ? "muted by volume pot" :
                         "enabled under volume-pot control") : "muted");
  }
}

static void Bringup_Run(void)
{
  BSP_LED_Off(LED_GREEN);
  BSP_LED_Off(LED_YELLOW);
  BSP_LED_Off(LED_RED);

  printf("\r\n========================================\r\n");
  printf("Spooky Box radio-to-headphone bring-up\r\n");
  printf("CM7 64 MHz HSI; FM/AM/SW/LW USB-tunable, 48 kHz stereo\r\n");
  printf("Jack detect: line-in PA5=%s, headphone PE0=%s\r\n",
         HAL_GPIO_ReadPin(LINE_IN_JACK_DETECT_GPIO_Port,
                          LINE_IN_JACK_DETECT_Pin) == GPIO_PIN_RESET
           ? "inserted" : "empty",
         HAL_GPIO_ReadPin(HEADPHONE_JACK_DETECT_GPIO_Port,
                          HEADPHONE_JACK_DETECT_Pin) == GPIO_PIN_RESET
           ? "inserted" : "empty");
  printf("========================================\r\n");

  if (RadioAudioStart())
  {
    BSP_LED_On(LED_GREEN);
    printf("\r\n[summary] PASS: radio audio is routed to the headphone codec\r\n");
    printf("[summary] Output follows PE0 jack detect and the PF10 volume pot\r\n");
  }
  else
  {
    BSP_LED_On(LED_RED);
    printf("\r\n[summary] FAIL: radio/audio stream did not start; hardware quiesced\r\n");
  }
}

static void SleepMakePeripheralPinsHighImpedance(void)
{
  GPIO_InitTypeDef gpio = {0};

  gpio.Mode = GPIO_MODE_ANALOG;
  gpio.Pull = GPIO_NOPULL;

  /* Stop all push-pull clocks/data that reach the switchable 3.3 V rail. */
  gpio.Pin = SAI_CODEC_MCLK_A_Pin | SAI_CODEC_SD_B_Pin |
             SAI_CODEC_FS_A_Pin | SAI_CODEC_SCK_A_Pin |
             SAI_CODEC_SD_A_Pin;
  HAL_GPIO_Init(GPIOE, &gpio);
  gpio.Pin = SAI_RADIO_SD_Pin | SAI_RADIO_FS_Pin | SAI_RADIO_SCK_Pin;
  HAL_GPIO_Init(GPIOD, &gpio);
  gpio.Pin = RADIO_RCLK_Pin;
  HAL_GPIO_Init(RADIO_RCLK_GPIO_Port, &gpio);
  gpio.Pin = MIC_CKOUT_Pin | MIC_DATIN_Pin;
  HAL_GPIO_Init(GPIOC, &gpio);
}

static void SleepStopRadioAudio(void)
{
  const uint8_t power_down = SI4735_CMD_POWER_DOWN;
  uint8_t status;

  if (radio_audio_running)
  {
    (void)CodecSetHeadphoneMute(true);
    (void)Bringup_RadioCommand(&power_down, sizeof(power_down), &status,
                               sizeof(status));
  }
  radio_audio_copy_enabled = false;
  radio_audio_running = false;
  radio_audio_headphones_unmuted = false;
  (void)HAL_SAI_DMAStop(&hsai_BlockA1);
  (void)HAL_SAI_DMAStop(&hsai_BlockA2);
  HAL_NVIC_DisableIRQ(DMA1_Stream0_IRQn);
  HAL_NVIC_DisableIRQ(DMA1_Stream4_IRQn);
  HAL_NVIC_DisableIRQ(DMA2_Stream0_IRQn);
  HAL_NVIC_DisableIRQ(SAI2_IRQn);
  HAL_GPIO_WritePin(RADIO_RST_GPIO_Port, RADIO_RST_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(AMP_SD_GPIO_Port, AMP_SD_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(LED_MATRIX_EN_GPIO_Port, LED_MATRIX_EN_Pin,
                    GPIO_PIN_RESET);
  SleepMakePeripheralPinsHighImpedance();

  __HAL_RCC_SAI1_CLK_DISABLE();
  __HAL_RCC_SAI2_CLK_DISABLE();
  __HAL_RCC_DMA1_CLK_DISABLE();
  __HAL_RCC_DMA2_CLK_DISABLE();
  __HAL_RCC_DFSDM1_CLK_DISABLE();
  __HAL_RCC_I2C1_CLK_DISABLE();
  __HAL_RCC_I2C4_CLK_DISABLE();
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */
/* USER CODE BEGIN Boot_Mode_Sequence_0 */
#if defined(DUAL_CORE_BOOT_SYNC_SEQUENCE)
  int32_t timeout;
#endif /* DUAL_CORE_BOOT_SYNC_SEQUENCE */
/* USER CODE END Boot_Mode_Sequence_0 */

/* USER CODE BEGIN Boot_Mode_Sequence_1 */
#if defined(DUAL_CORE_BOOT_SYNC_SEQUENCE)
  /* Wait until CPU2 boots and enters in stop mode or timeout*/
  timeout = 0xFFFF;
  while((__HAL_RCC_GET_FLAG(RCC_FLAG_D2CKRDY) != RESET) && (timeout-- > 0));
  if ( timeout < 0 )
  {
  Error_Handler();
  }
#endif /* DUAL_CORE_BOOT_SYNC_SEQUENCE */
/* USER CODE END Boot_Mode_Sequence_1 */
  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */
  Diagnostics_Init();

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* Configure the peripherals common clocks */
  PeriphCommonClock_Config();
/* USER CODE BEGIN Boot_Mode_Sequence_2 */
#if defined(SPOOKY_IPC_SMOKE)
  IpcSmoke_Init(); /* M4 is still held in its boot STOP wait. */
#endif
#if defined(DUAL_CORE_BOOT_SYNC_SEQUENCE)
/* When system initialization is finished, Cortex-M7 will release Cortex-M4 by means of
HSEM notification */
/*HW semaphore Clock enable*/
__HAL_RCC_HSEM_CLK_ENABLE();
/*Take HSEM */
HAL_HSEM_FastTake(HSEM_ID_0);
/*Release HSEM in order to notify the CPU2(CM4)*/
HAL_HSEM_Release(HSEM_ID_0,0);
/* wait until CPU2 wakes up from stop mode */
timeout = 0xFFFF;
while((__HAL_RCC_GET_FLAG(RCC_FLAG_D2CKRDY) == RESET) && (timeout-- > 0));
if ( timeout < 0 )
{
Error_Handler();
}
#endif /* DUAL_CORE_BOOT_SYNC_SEQUENCE */
/* USER CODE END Boot_Mode_Sequence_2 */

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_I2C1_Init();
  MX_I2C2_Init();
  MX_I2C4_Init();
  MX_SAI1_Init();
  MX_ADC3_Init();
  MX_DFSDM1_Init();
#if !defined(SPOOKY_MINIMAL_BRINGUP)
  MX_SDMMC1_SD_Init();
  MX_SAI2_Init();
  MX_SPI6_Init();
#endif
  /* USER CODE BEGIN 2 */

  /* USER CODE END 2 */

  /* Initialize leds */
  BSP_LED_Init(LED_GREEN);
  BSP_LED_Init(LED_YELLOW);
  BSP_LED_Init(LED_RED);

  /* Initialize USER push-button, will be used to trigger an interrupt each time it's pressed.*/
  BSP_PB_Init(BUTTON_USER, BUTTON_MODE_EXTI);

  /* Route the bounded printf logger through the backplane AUX UART instead of the
   * Nucleo ST-LINK VCP on USART3. */
  if (!BoardDiagnostics_StartConsole())
  {
    Error_Handler();
  }
  printf("\r\n[uart] Spooky Probe console on UART7: PE8 TX, PE7 RX, 115200 8N1\r\n");

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  Bringup_Run();
  SdTest_Start(&hsd1);
  RadioRecorder_Init(&hdfsdm1_filter0);
  WavTransfer_Init(&hsd1);
  if (!FuelGaugeTest_Start(&hi2c2))
  {
    BSP_LED_On(LED_RED);
  }
  if (!MagnetometerTest_Start(&hi2c2))
  {
    BSP_LED_On(LED_RED);
  }
  if (!UiBoardTest_Start(&hi2c2, &hspi6))
  {
    BSP_LED_On(LED_RED);
  }
  UsbTest_SetLineHandler(UsbCliCommand);
  if (!UsbTest_Start())
  {
    BSP_LED_On(LED_RED);
  }
  while (1)
  {

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
#if defined(SPOOKY_IPC_SMOKE)
    IpcSmoke_Service();
#endif
    RadioAudio_Service();
    RadioRecorder_Service();
    FuelGaugeTest_Service();
    UsbTest_Service();
    WavTransfer_Service();
    TargetLogger_Service();
    Diagnostics_Service();
    UiBoardTest_Service();
    SdTest_Service();
    PrototypePower_Service(SleepStopRadioAudio);
    MagnetometerTest_Service();
    HAL_Delay(5U);
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Supply configuration update enable
  */
  HAL_PWREx_ConfigSupply(PWR_DIRECT_SMPS_SUPPLY);

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Configure LSE Drive Capability
  */
  HAL_PWR_EnableBkUpAccess();
  __HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_LOW);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI|RCC_OSCILLATORTYPE_LSE;
  RCC_OscInitStruct.LSEState = RCC_LSE_ON;
  RCC_OscInitStruct.HSIState = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 9;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_3;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOMEDIUM;
  RCC_OscInitStruct.PLL.PLLFRACN = 3072;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV1;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
  {
    Error_Handler();
  }
  HAL_RCC_MCOConfig(RCC_MCO1, RCC_MCO1SOURCE_LSE, RCC_MCODIV_1);
}

/**
  * @brief Peripherals Common Clock Configuration
  * @retval None
  */
void PeriphCommonClock_Config(void)
{
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

  /** Initializes the peripherals clock
  */
  PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_SAI1;
  PeriphClkInitStruct.PLL3.PLL3M = 4;
  PeriphClkInitStruct.PLL3.PLL3N = 24;
  PeriphClkInitStruct.PLL3.PLL3P = 16;
  PeriphClkInitStruct.PLL3.PLL3Q = 2;
  PeriphClkInitStruct.PLL3.PLL3R = 8;
  PeriphClkInitStruct.PLL3.PLL3RGE = RCC_PLL3VCIRANGE_3;
  PeriphClkInitStruct.PLL3.PLL3VCOSEL = RCC_PLL3VCOWIDE;
  PeriphClkInitStruct.PLL3.PLL3FRACN = 4719;
  PeriphClkInitStruct.Sai1ClockSelection = RCC_SAI1CLKSOURCE_PLL3;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ADC3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC3_Init(void)
{

  /* USER CODE BEGIN ADC3_Init 0 */

  /* USER CODE END ADC3_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC3_Init 1 */

  /* USER CODE END ADC3_Init 1 */

  /** Common config
  */
  hadc3.Instance = ADC3;
  hadc3.Init.Resolution = ADC_RESOLUTION_16B;
  hadc3.Init.ScanConvMode = ADC_SCAN_DISABLE;
  hadc3.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  hadc3.Init.LowPowerAutoWait = DISABLE;
  hadc3.Init.ContinuousConvMode = DISABLE;
  hadc3.Init.NbrOfConversion = 1;
  hadc3.Init.DiscontinuousConvMode = DISABLE;
  hadc3.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc3.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc3.Init.ConversionDataManagement = ADC_CONVERSIONDATA_DR;
  hadc3.Init.Overrun = ADC_OVR_DATA_PRESERVED;
  hadc3.Init.LeftBitShift = ADC_LEFTBITSHIFT_NONE;
  hadc3.Init.OversamplingMode = DISABLE;
  hadc3.Init.Oversampling.Ratio = 1;
  if (HAL_ADC_Init(&hadc3) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_6;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_64CYCLES_5;
  sConfig.SingleDiff = ADC_SINGLE_ENDED;
  sConfig.OffsetNumber = ADC_OFFSET_NONE;
  sConfig.Offset = 0;
  sConfig.OffsetSignedSaturation = DISABLE;
  if (HAL_ADC_ConfigChannel(&hadc3, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC3_Init 2 */

  /* USER CODE END ADC3_Init 2 */

}

/**
  * @brief DFSDM1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_DFSDM1_Init(void)
{

  /* USER CODE BEGIN DFSDM1_Init 0 */

  /* USER CODE END DFSDM1_Init 0 */

  /* USER CODE BEGIN DFSDM1_Init 1 */

  /* USER CODE END DFSDM1_Init 1 */
  hdfsdm1_channel0.Instance = DFSDM1_Channel0;
  hdfsdm1_channel0.Init.OutputClock.Activation = ENABLE;
  /* This image uses a 24.576 MHz PLL3 audio kernel.  Divide by 8 for the
   * proven 3.072 MHz PDM clock; Sinc4/OSR64 gives native 48 kHz PCM. */
  hdfsdm1_channel0.Init.OutputClock.Selection = DFSDM_CHANNEL_OUTPUT_CLOCK_AUDIO;
  hdfsdm1_channel0.Init.OutputClock.Divider = 8;
  hdfsdm1_channel0.Init.Input.Multiplexer = DFSDM_CHANNEL_EXTERNAL_INPUTS;
  hdfsdm1_channel0.Init.Input.DataPacking = DFSDM_CHANNEL_STANDARD_MODE;
  hdfsdm1_channel0.Init.Input.Pins = DFSDM_CHANNEL_FOLLOWING_CHANNEL_PINS;
  /* SPK0641 SELECT is grounded, so valid Mic-Low data uses falling edges. */
  hdfsdm1_channel0.Init.SerialInterface.Type = DFSDM_CHANNEL_SPI_FALLING;
  hdfsdm1_channel0.Init.SerialInterface.SpiClock = DFSDM_CHANNEL_SPI_CLOCK_INTERNAL;
  hdfsdm1_channel0.Init.Awd.FilterOrder = DFSDM_CHANNEL_FASTSINC_ORDER;
  hdfsdm1_channel0.Init.Awd.Oversampling = 1;
  hdfsdm1_channel0.Init.Offset = 0;
  hdfsdm1_channel0.Init.RightBitShift = 9;
  if (HAL_DFSDM_ChannelInit(&hdfsdm1_channel0) != HAL_OK)
  {
    Error_Handler();
  }
  hdfsdm1_filter0.Instance = DFSDM1_Filter0;
  hdfsdm1_filter0.Init.RegularParam.Trigger = DFSDM_FILTER_SW_TRIGGER;
  hdfsdm1_filter0.Init.RegularParam.FastMode = ENABLE;
  hdfsdm1_filter0.Init.RegularParam.DmaMode = ENABLE;
  hdfsdm1_filter0.Init.InjectedParam.Trigger = DFSDM_FILTER_SW_TRIGGER;
  hdfsdm1_filter0.Init.InjectedParam.ScanMode = DISABLE;
  hdfsdm1_filter0.Init.InjectedParam.DmaMode = DISABLE;
  hdfsdm1_filter0.Init.InjectedParam.ExtTrigger =
    DFSDM_FILTER_EXT_TRIG_TIM1_TRGO;
  hdfsdm1_filter0.Init.InjectedParam.ExtTriggerEdge =
    DFSDM_FILTER_EXT_TRIG_RISING_EDGE;
  hdfsdm1_filter0.Init.FilterParam.SincOrder = DFSDM_FILTER_SINC4_ORDER;
  hdfsdm1_filter0.Init.FilterParam.Oversampling = 64;
  hdfsdm1_filter0.Init.FilterParam.IntOversampling = 1;
  if (HAL_DFSDM_FilterInit(&hdfsdm1_filter0) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_DFSDM_FilterConfigRegChannel(&hdfsdm1_filter0, DFSDM_CHANNEL_0,
                                       DFSDM_CONTINUOUS_CONV_ON) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN DFSDM1_Init 2 */

  /* USER CODE END DFSDM1_Init 2 */

}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.Timing = 0x00707CBB;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Analogue filter
  */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Digital filter
  */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief I2C2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C2_Init(void)
{

  /* USER CODE BEGIN I2C2_Init 0 */

  /* USER CODE END I2C2_Init 0 */

  /* USER CODE BEGIN I2C2_Init 1 */

  /* USER CODE END I2C2_Init 1 */
  hi2c2.Instance = I2C2;
  hi2c2.Init.Timing = 0x00707CBB;
  hi2c2.Init.OwnAddress1 = 0;
  hi2c2.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c2.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c2.Init.OwnAddress2 = 0;
  hi2c2.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c2.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c2.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c2) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Analogue filter
  */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c2, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Digital filter
  */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c2, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C2_Init 2 */

  /* USER CODE END I2C2_Init 2 */

}

/**
  * @brief I2C4 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C4_Init(void)
{

  /* USER CODE BEGIN I2C4_Init 0 */

  /* USER CODE END I2C4_Init 0 */

  /* USER CODE BEGIN I2C4_Init 1 */

  /* USER CODE END I2C4_Init 1 */
  hi2c4.Instance = I2C4;
  hi2c4.Init.Timing = 0x00707CBB;
  hi2c4.Init.OwnAddress1 = 0;
  hi2c4.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c4.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c4.Init.OwnAddress2 = 0;
  hi2c4.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c4.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c4.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c4) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Analogue filter
  */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c4, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Digital filter
  */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c4, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C4_Init 2 */

  /* USER CODE END I2C4_Init 2 */

}

/**
  * @brief SAI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SAI1_Init(void)
{

  /* USER CODE BEGIN SAI1_Init 0 */

  /* USER CODE END SAI1_Init 0 */

  /* USER CODE BEGIN SAI1_Init 1 */

  /* USER CODE END SAI1_Init 1 */
  hsai_BlockA1.Instance = SAI1_Block_A;
  hsai_BlockA1.Init.AudioMode = SAI_MODEMASTER_TX;
  hsai_BlockA1.Init.Synchro = SAI_ASYNCHRONOUS;
  hsai_BlockA1.Init.OutputDrive = SAI_OUTPUTDRIVE_ENABLE;
  hsai_BlockA1.Init.NoDivider = SAI_MCK_OVERSAMPLING_DISABLE;
  hsai_BlockA1.Init.MckOverSampling = SAI_MCK_OVERSAMPLING_DISABLE;
  hsai_BlockA1.Init.FIFOThreshold = SAI_FIFOTHRESHOLD_1QF;
  hsai_BlockA1.Init.AudioFrequency = SAI_AUDIO_FREQUENCY_48K;
  hsai_BlockA1.Init.SynchroExt = SAI_SYNCEXT_DISABLE;
  hsai_BlockA1.Init.MonoStereoMode = SAI_STEREOMODE;
  hsai_BlockA1.Init.CompandingMode = SAI_NOCOMPANDING;
  hsai_BlockA1.Init.TriState = SAI_OUTPUT_NOTRELEASED;
  if (HAL_SAI_InitProtocol(&hsai_BlockA1, SAI_I2S_STANDARD, SAI_PROTOCOL_DATASIZE_16BIT, 2) != HAL_OK)
  {
    Error_Handler();
  }
  hsai_BlockB1.Instance = SAI1_Block_B;
  hsai_BlockB1.Init.AudioMode = SAI_MODESLAVE_RX;
  hsai_BlockB1.Init.Synchro = SAI_SYNCHRONOUS;
  hsai_BlockB1.Init.OutputDrive = SAI_OUTPUTDRIVE_DISABLE;
  hsai_BlockB1.Init.MckOverSampling = SAI_MCK_OVERSAMPLING_DISABLE;
  hsai_BlockB1.Init.FIFOThreshold = SAI_FIFOTHRESHOLD_1QF;
  hsai_BlockB1.Init.SynchroExt = SAI_SYNCEXT_DISABLE;
  hsai_BlockB1.Init.MonoStereoMode = SAI_STEREOMODE;
  hsai_BlockB1.Init.CompandingMode = SAI_NOCOMPANDING;
  hsai_BlockB1.Init.TriState = SAI_OUTPUT_NOTRELEASED;
  if (HAL_SAI_InitProtocol(&hsai_BlockB1, SAI_I2S_STANDARD, SAI_PROTOCOL_DATASIZE_16BIT, 2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SAI1_Init 2 */

  /* USER CODE END SAI1_Init 2 */

}

/**
  * @brief SAI2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SAI2_Init(void)
{

  /* USER CODE BEGIN SAI2_Init 0 */

  /* USER CODE END SAI2_Init 0 */

  /* USER CODE BEGIN SAI2_Init 1 */

  /* USER CODE END SAI2_Init 1 */
  hsai_BlockA2.Instance = SAI2_Block_A;
  hsai_BlockA2.Init.Protocol = SAI_FREE_PROTOCOL;
  hsai_BlockA2.Init.AudioMode = SAI_MODEMASTER_TX;
  hsai_BlockA2.Init.DataSize = SAI_DATASIZE_8;
  hsai_BlockA2.Init.FirstBit = SAI_FIRSTBIT_MSB;
  hsai_BlockA2.Init.ClockStrobing = SAI_CLOCKSTROBING_FALLINGEDGE;
  hsai_BlockA2.Init.Synchro = SAI_ASYNCHRONOUS;
  hsai_BlockA2.Init.OutputDrive = SAI_OUTPUTDRIVE_DISABLE;
  hsai_BlockA2.Init.NoDivider = SAI_MCK_OVERSAMPLING_DISABLE;
  hsai_BlockA2.Init.MckOverSampling = SAI_MCK_OVERSAMPLING_DISABLE;
  hsai_BlockA2.Init.FIFOThreshold = SAI_FIFOTHRESHOLD_EMPTY;
  hsai_BlockA2.Init.AudioFrequency = SAI_AUDIO_FREQUENCY_192K;
  hsai_BlockA2.Init.SynchroExt = SAI_SYNCEXT_DISABLE;
  hsai_BlockA2.Init.MonoStereoMode = SAI_STEREOMODE;
  hsai_BlockA2.Init.CompandingMode = SAI_NOCOMPANDING;
  hsai_BlockA2.Init.TriState = SAI_OUTPUT_NOTRELEASED;
  hsai_BlockA2.Init.PdmInit.Activation = DISABLE;
  hsai_BlockA2.Init.PdmInit.MicPairsNbr = 1;
  hsai_BlockA2.Init.PdmInit.ClockEnable = SAI_PDM_CLOCK1_ENABLE;
  hsai_BlockA2.FrameInit.FrameLength = 8;
  hsai_BlockA2.FrameInit.ActiveFrameLength = 1;
  hsai_BlockA2.FrameInit.FSDefinition = SAI_FS_STARTFRAME;
  hsai_BlockA2.FrameInit.FSPolarity = SAI_FS_ACTIVE_LOW;
  hsai_BlockA2.FrameInit.FSOffset = SAI_FS_FIRSTBIT;
  hsai_BlockA2.SlotInit.FirstBitOffset = 0;
  hsai_BlockA2.SlotInit.SlotSize = SAI_SLOTSIZE_DATASIZE;
  hsai_BlockA2.SlotInit.SlotNumber = 1;
  hsai_BlockA2.SlotInit.SlotActive = 0x00000000;
  if (HAL_SAI_Init(&hsai_BlockA2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SAI2_Init 2 */

  /* USER CODE END SAI2_Init 2 */

}

/**
  * @brief SDMMC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SDMMC1_SD_Init(void)
{

  /* USER CODE BEGIN SDMMC1_Init 0 */

  /* USER CODE END SDMMC1_Init 0 */

  /* USER CODE BEGIN SDMMC1_Init 1 */

  /* USER CODE END SDMMC1_Init 1 */
  hsd1.Instance = SDMMC1;
  hsd1.Init.ClockEdge = SDMMC_CLOCK_EDGE_RISING;
  hsd1.Init.ClockPowerSave = SDMMC_CLOCK_POWER_SAVE_DISABLE;
  hsd1.Init.BusWide = SDMMC_BUS_WIDE_4B;
  hsd1.Init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_ENABLE;
  hsd1.Init.ClockDiv = 2;
  if (HAL_SD_Init(&hsd1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SDMMC1_Init 2 */

  /* USER CODE END SDMMC1_Init 2 */

}

/**
  * @brief SPI6 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI6_Init(void)
{

  /* USER CODE BEGIN SPI6_Init 0 */

  /* USER CODE END SPI6_Init 0 */

  /* USER CODE BEGIN SPI6_Init 1 */

  /* USER CODE END SPI6_Init 1 */
  /* SPI6 parameter configuration*/
  hspi6.Instance = SPI6;
  hspi6.Init.Mode = SPI_MODE_MASTER;
  hspi6.Init.Direction = SPI_DIRECTION_2LINES;
  hspi6.Init.DataSize = SPI_DATASIZE_4BIT;
  hspi6.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi6.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi6.Init.NSS = SPI_NSS_SOFT;
  hspi6.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
  hspi6.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi6.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi6.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi6.Init.CRCPolynomial = 0x0;
  hspi6.Init.NSSPMode = SPI_NSS_PULSE_ENABLE;
  hspi6.Init.NSSPolarity = SPI_NSS_POLARITY_LOW;
  hspi6.Init.FifoThreshold = SPI_FIFO_THRESHOLD_01DATA;
  hspi6.Init.TxCRCInitializationPattern = SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
  hspi6.Init.RxCRCInitializationPattern = SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
  hspi6.Init.MasterSSIdleness = SPI_MASTER_SS_IDLENESS_00CYCLE;
  hspi6.Init.MasterInterDataIdleness = SPI_MASTER_INTERDATA_IDLENESS_00CYCLE;
  hspi6.Init.MasterReceiverAutoSusp = SPI_MASTER_RX_AUTOSUSP_DISABLE;
  hspi6.Init.MasterKeepIOState = SPI_MASTER_KEEP_IO_STATE_DISABLE;
  hspi6.Init.IOSwap = SPI_IO_SWAP_DISABLE;
  if (HAL_SPI_Init(&hspi6) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI6_Init 2 */

  /* USER CODE END SPI6_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Stream0_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Stream0_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream0_IRQn);
  /* DMA1_Stream1_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Stream1_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream1_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();

  /*Configure GPIO pin : SD_CARD_DETECT_Pin */
  GPIO_InitStruct.Pin = SD_CARD_DETECT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(SD_CARD_DETECT_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : PA11 PA12 */
  GPIO_InitStruct.Pin = GPIO_PIN_11|GPIO_PIN_12;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF10_OTG1_FS;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* Keep the radio in reset until its reference clock and control bus have
   * been checked.  The prototype bodge routes reset to PA10. */
  HAL_GPIO_WritePin(RADIO_RST_GPIO_Port, RADIO_RST_Pin, GPIO_PIN_RESET);
  GPIO_InitStruct.Pin = RADIO_RST_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(RADIO_RST_GPIO_Port, &GPIO_InitStruct);

  /* The audio shield provides external pull-ups and filtering for both
   * active-low jack-detect signals.  The prototype bodges headphone detect
   * from PE14 to PE0 and restores line-in detect on PA5. */
  GPIO_InitStruct.Pin = LINE_IN_JACK_DETECT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(LINE_IN_JACK_DETECT_GPIO_Port, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = HEADPHONE_JACK_DETECT_Pin;
  HAL_GPIO_Init(HEADPHONE_JACK_DETECT_GPIO_Port, &GPIO_InitStruct);

  /* These pins are status/strap inputs.  Never drive them during reset. */
  GPIO_InitStruct.Pin = RADIO_GPIO_1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(RADIO_GPIO_1_GPIO_Port, &GPIO_InitStruct);

  /* The RF board's analog switch uses low for its AM/LW loop antenna and
   * high for its shortwave whip antenna.  FM has a separate RF input. */
  HAL_GPIO_WritePin(RADIO_SW_SWITCH_GPIO_Port, RADIO_SW_SWITCH_Pin,
                    GPIO_PIN_RESET);
  GPIO_InitStruct.Pin = RADIO_SW_SWITCH_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(RADIO_SW_SWITCH_GPIO_Port, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = RADIO_INT_Pin;
  HAL_GPIO_Init(RADIO_INT_GPIO_Port, &GPIO_InitStruct);

  /* The magnetometer interrupt is not needed for the first polling bring-up,
   * but leave its backplane net safely configured as an input. */
  GPIO_InitStruct.Pin = MAG_INT_Pin;
  HAL_GPIO_Init(MAG_INT_GPIO_Port, &GPIO_InitStruct);

  /* The 3.3 V breakout enables itself through an onboard pull-up.  Open-drain
   * PB12 can therefore release it for normal operation or pull it low during
   * the charging-monitor sleep test without driving against that pull-up. */
  HAL_GPIO_WritePin(REG_3V3_EN_GPIO_Port, REG_3V3_EN_Pin, GPIO_PIN_SET);
  GPIO_InitStruct.Pin = REG_3V3_EN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(REG_3V3_EN_GPIO_Port, &GPIO_InitStruct);

  /* Keep currently unused high-current loads shut down. */
  HAL_GPIO_WritePin(LED_MATRIX_EN_GPIO_Port, LED_MATRIX_EN_Pin,
                    GPIO_PIN_RESET);
  GPIO_InitStruct.Pin = LED_MATRIX_EN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  HAL_GPIO_Init(LED_MATRIX_EN_GPIO_Port, &GPIO_InitStruct);

  HAL_GPIO_WritePin(AMP_SD_GPIO_Port, AMP_SD_Pin, GPIO_PIN_RESET);
  GPIO_InitStruct.Pin = AMP_SD_Pin;
  HAL_GPIO_Init(AMP_SD_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

void RTC_WKUP_IRQHandler(void)
{
  PrototypePower_OnRtcWake();
}

void BSP_PB_Callback(Button_TypeDef Button)
{
  if (Button == BUTTON_USER)
  {
    PrototypePower_OnUserButton();
  }
}

void DMA1_Stream4_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&hdma_sai2_a);
}

void DMA2_Stream0_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&hdma_dfsdm1_flt0);
}

void SAI2_IRQHandler(void)
{
  HAL_SAI_IRQHandler(&hsai_BlockA2);
}

void HAL_SAI_RxHalfCpltCallback(SAI_HandleTypeDef *hsai)
{
  if ((hsai != NULL) && (hsai->Instance == SAI2_Block_A))
  {
    if (radio_audio_copy_enabled)
    {
      RadioAudioCopyHalf(0U);
    }
    ++radio_audio_rx_half_count;
  }
}

void HAL_SAI_RxCpltCallback(SAI_HandleTypeDef *hsai)
{
  if ((hsai != NULL) && (hsai->Instance == SAI2_Block_A))
  {
    if (radio_audio_copy_enabled)
    {
      RadioAudioCopyHalf(RADIO_AUDIO_BUFFER_SAMPLES / 2U);
    }
    ++radio_audio_rx_full_count;
  }
}

void HAL_SAI_ErrorCallback(SAI_HandleTypeDef *hsai)
{
  if ((hsai != NULL) && (hsai->Instance == SAI2_Block_A))
  {
    if ((radio_audio_error_flags & 1U) == 0U)
      Diagnostics_Record(DIAG_AUDIO_ERROR, 1U, hsai->ErrorCode);
    radio_audio_error_flags |= 1U;
    RadioRecorder_NotifyRadioError();
  }
  else if ((hsai != NULL) && (hsai->Instance == SAI1_Block_A))
  {
    if ((radio_audio_error_flags & 2U) == 0U)
      Diagnostics_Record(DIAG_AUDIO_ERROR, 2U, hsai->ErrorCode);
    radio_audio_error_flags |= 2U;
  }
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
