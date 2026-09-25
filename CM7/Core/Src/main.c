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
#include "audio_path_service.h"
#include "board_diagnostics.h"
#include "codec_volume_service.h"
#include "target_logger.h"
#include "diagnostics.h"
#include "ipc_smoke_cli.h"
#if defined(SPOOKY_IPC_SMOKE)
#include "ipc_smoke.h"
#endif
#include "prototype_power.h"
#include "fuel_gauge_test.h"
#include "magnetometer_test.h"
#include "radio_control_service.h"
#include "radio_recorder.h"
#include "sd_test.h"
#include "ui_board_test.h"
#include "usb_test.h"
#include "wav_transfer.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

#define SPOOKY_MINIMAL_BRINGUP

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

static void Bringup_Run(void);
static void UsbCliCommand(const char *line);
static void RadioAudio_Service(void);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* Sequences the monitored output around a receiver function change. The radio
 * service only changes the Si4735; this caller owns muting and the DMA copy. */
static bool RadioAudioSwitchBand(RadioBand band, RadioTuneStatus *tune_status)
{
  if ((band >= RADIO_BAND_COUNT) || (tune_status == NULL))
  {
    return false;
  }
  if (!CodecVolume_SetTransitionMuted(true))
  {
    goto failed;
  }
  AudioPath_SetStreamEnabled(false);

  if (!RadioControl_SwitchBand(band, tune_status))
  {
    goto failed;
  }

  AudioPath_SetStreamEnabled(true);
  if (!CodecVolume_SetTransitionMuted(false))
  {
    goto failed;
  }
  return true;

failed:
  AudioPath_SetStreamEnabled(false);
  AudioPath_MarkStopped();
  (void)CodecVolume_SetTransitionMuted(true);
  RadioControl_HoldReset();
  BSP_LED_Off(LED_GREEN);
  BSP_LED_On(LED_RED);
  printf("[radio] FAIL: band switch; receiver reset and output muted\r\n");
  return false;
}

static bool RadioAudioStart(void)
{
  if (!AudioPath_Init(&hsai_BlockA2, &hsai_BlockA1) ||
      !CodecVolume_Init(&hi2c4, &hadc3, &hsai_BlockA1) ||
      !AudioPath_StartCapture() ||
      !RadioControl_Start(&hi2c1) ||
      !AudioPath_StartMonitor())
  {
    goto failed;
  }
  return true;

failed:
  (void)CodecVolume_SetTransitionMuted(true);
  RadioControl_HoldReset();
  return false;
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
    if (strcmp(text, RadioControl_GetBandInfo((RadioBand)index)->name) == 0)
    {
      *band = (RadioBand)index;
      return true;
    }
  }
  return false;
}

static void RadioUsbSendStatus(const RadioTuneStatus *tune_status)
{
  const RadioBandInfo *config;
  char response[160];

  if ((tune_status == NULL) || (tune_status->band >= RADIO_BAND_COUNT))
  {
    (void)UsbTest_SendText("ERR RADIO status unavailable\r\n");
    return;
  }
  config = RadioControl_GetBandInfo(tune_status->band);
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
  RadioControlStatus status;
  const RadioBandInfo *config;
  char response[128];

  (void)RadioControl_GetStatus(&status);
  config = RadioControl_GetBandInfo(status.band);
  (void)snprintf(response, sizeof(response),
                 "OK RADIO BAND=%s RANGE=%lu..%lu kHz STEP=%lu kHz\r\n",
                 config->name, (unsigned long)config->minimum_khz,
                 (unsigned long)config->maximum_khz,
                 (unsigned long)config->step_khz);
  (void)UsbTest_SendText(response);
}

static void VolumeUsbSendStatus(void)
{
  CodecVolumeStatus status;
  char response[112];
  uint32_t level_percent;

  if (!CodecVolume_GetStatus(&status) || !status.ready)
  {
    (void)UsbTest_SendText("ERR VOLUME pot unavailable\r\n");
    return;
  }
  level_percent = (status.volume_adc * 100U + 32767U) / 65535U;
  if (status.volume_muted)
  {
    (void)snprintf(response, sizeof(response),
                   "OK VOLUME ADC=%lu LEVEL=%lu%% MUTED=1\r\n",
                   (unsigned long)status.volume_adc,
                   (unsigned long)level_percent);
  }
  else
  {
    (void)snprintf(response, sizeof(response),
                   "OK VOLUME ADC=%lu LEVEL=%lu%% ATTEN=-%lu.%lu dB MUTED=0\r\n",
                   (unsigned long)status.volume_adc,
                   (unsigned long)level_percent,
                   (unsigned long)(status.attenuation_half_db / 2U),
                   (unsigned long)((status.attenuation_half_db & 1U) ?
                                   5U : 0U));
  }
  (void)UsbTest_SendText(response);
}

static void UsbCliCommand(const char *line)
{
  char command[64];
  const RadioBandInfo *config;
  RadioControlStatus radio_status;
  RadioBand target_band;
  RadioTuneStatus tune_status;
  uint32_t frequency_khz;
  bool tuned;
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
    if (SdTest_IsActive())
    {
      (void)UsbTest_SendText("ERR WAV unavailable while SD test active\r\n");
    }
    else if (RadioRecorder_IsActive())
    {
      (void)UsbTest_SendText("ERR WAV unavailable while recording\r\n");
    }
    else
    {
      (void)WavTransfer_HandleCommand(command);
    }
    return;
  }
  if (SdTest_IsActive() && (strncmp(command, "RECORD START", 12U) == 0) &&
      ((command[12] == '\0') || (command[12] == ' ') ||
       (command[12] == '\t')))
  {
    (void)UsbTest_SendText("ERR RECORD unavailable while SD test active\r\n");
    return;
  }
  if (RadioRecorder_HandleCommand(command, AudioPath_IsRunning()))
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
  if (!AudioPath_IsRunning())
  {
    (void)UsbTest_SendText("ERR RADIO audio path is not running\r\n");
    return;
  }
  if (strcmp(command, "STATUS") == 0)
  {
    (void)RadioControl_GetStatus(&radio_status);
    RadioUsbSendStatus(&radio_status.tune);
    return;
  }
  if ((strcmp(command, "VOLUME") == 0) ||
      (strcmp(command, "VOLUME READ") == 0) ||
      (strcmp(command, "VOLUME STATUS") == 0))
  {
    if (!CodecVolume_RefreshVolume())
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
    if (!RadioAudioSwitchBand(target_band, &tune_status))
    {
      (void)UsbTest_SendText(
        "ERR RADIO band switch failed; reset required\r\n");
      return;
    }
    RadioUsbSendStatus(&tune_status);
    return;
  }

  (void)RadioControl_GetStatus(&radio_status);
  config = RadioControl_GetBandInfo(radio_status.band);

  if (strcmp(command, "UP") == 0)
  {
    tuned = RadioControl_TuneStep(true, &tune_status);
  }
  else if (strcmp(command, "DOWN") == 0)
  {
    tuned = RadioControl_TuneStep(false, &tune_status);
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
    tuned = RadioControl_Tune(frequency_khz, &tune_status);
  }
  else
  {
    (void)UsbTest_SendText("ERR unknown command; type HELP\r\n");
    return;
  }

  if (!tuned)
  {
    (void)RadioControl_GetStatus(&radio_status);
    (void)UsbTest_SendText("ERR RADIO tune failed\r\n");
    printf("[radio] USB %s tune failed at %lu kHz\r\n",
           config->name, (unsigned long)radio_status.target_khz);
    return;
  }

  RadioControl_LogTune("USB tuned", &tune_status);
  RadioUsbSendStatus(&tune_status);
}

static void RadioAudio_Service(void)
{
  CodecVolumeStatus codec_status;
  AudioPathStatus audio_status;

  if (!AudioPath_GetStatus(&audio_status) || !audio_status.running)
  {
    return;
  }
  if (audio_status.fault_flags != 0U)
  {
    (void)CodecVolume_SetTransitionMuted(true);
    AudioPath_MarkStopped();
    BSP_LED_Off(LED_GREEN);
    BSP_LED_On(LED_RED);
    printf("[bridge] FAIL: runtime SAI/DMA flags=0x%08lX; output muted\r\n",
           (unsigned long)audio_status.fault_flags);
    return;
  }

  if (!CodecVolume_Service())
  {
    (void)CodecVolume_GetStatus(&codec_status);
    AudioPath_ReportFault(
      (codec_status.last_error == CODEC_VOLUME_ERROR_OUTPUT)
        ? AUDIO_PATH_FAULT_CODEC_OUTPUT : AUDIO_PATH_FAULT_VOLUME_ADC);
  }
}

static void Bringup_Run(void)
{
  CodecVolumeStatus codec_status;

  BSP_LED_Off(LED_GREEN);
  BSP_LED_Off(LED_YELLOW);
  BSP_LED_Off(LED_RED);

  printf("\r\n========================================\r\n");
  printf("Spooky Box radio-to-headphone bring-up\r\n");
  printf("CM7 64 MHz HSI; FM/AM/SW/LW USB-tunable, 48 kHz stereo\r\n");
  (void)CodecVolume_GetStatus(&codec_status);
  printf("Jack detect: line-in PA5=%s, headphone PE0=%s\r\n",
         codec_status.line_in_inserted ? "inserted" : "empty",
         codec_status.headphone_inserted ? "inserted" : "empty");
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
  if (AudioPath_IsRunning())
  {
    (void)CodecVolume_SetTransitionMuted(true);
    RadioControl_PowerDown();
  }
  AudioPath_Stop();
  HAL_NVIC_DisableIRQ(DMA2_Stream0_IRQn); /* DFSDM microphone capture */
  RadioControl_HoldReset();
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

void DMA2_Stream0_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&hdma_dfsdm1_flt0);
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
