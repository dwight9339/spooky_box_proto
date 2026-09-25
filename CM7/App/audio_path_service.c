/* Extracted from the proven M7 radio-to-headphone bridge without changing SAI
 * or DMA configuration, buffer placement, callback cadence or ISR work. */
#include "audio_path_service.h"

#include "diagnostics.h"
#include "main.h"
#include "radio_recorder.h"

#include <stdio.h>
#include <string.h>

#define AUDIO_PATH_BUFFER_SAMPLES      2048U
#define AUDIO_PATH_HALF_SAMPLES        (AUDIO_PATH_BUFFER_SAMPLES / 2U)
#define AUDIO_PATH_START_TIMEOUT_MS    500U

static DMA_HandleTypeDef hdma_sai2_a;
static SAI_HandleTypeDef *radio_rx_sai;
static SAI_HandleTypeDef *monitor_tx_sai;

/* AXI SRAM: reachable by DMA1, unlike DTCM. */
static uint16_t radio_rx_buffer[AUDIO_PATH_BUFFER_SAMPLES]
  __attribute__((section(".dma_buffer"), aligned(32)));
static uint16_t monitor_tx_buffer[AUDIO_PATH_BUFFER_SAMPLES]
  __attribute__((section(".dma_buffer"), aligned(32)));
static volatile uint32_t rx_half_count;
static volatile uint32_t rx_full_count;
static volatile uint32_t fault_flags;
static volatile bool stream_enabled;
static bool running;

static uint32_t __attribute__((optimize("O3")))
MeasureFs(GPIO_TypeDef *port, uint32_t pin)
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

bool AudioPath_Init(SAI_HandleTypeDef *radio_rx, SAI_HandleTypeDef *monitor_tx)
{
  if ((radio_rx == NULL) || (monitor_tx == NULL))
  {
    return false;
  }
  radio_rx_sai = radio_rx;
  monitor_tx_sai = monitor_tx;
  running = false;
  return true;
}

bool AudioPath_StartCapture(void)
{
  SAI_HandleTypeDef * const sai = radio_rx_sai;
  uint32_t sai2_kernel_hz;
  uint32_t measured_fs_hz;

  running = false;
  if (sai == NULL)
  {
    return false;
  }

  memset(sai, 0, sizeof(*sai));
  sai->Instance = SAI2_Block_A;
  sai->Init.AudioMode = SAI_MODEMASTER_RX;
  sai->Init.Synchro = SAI_ASYNCHRONOUS;
  sai->Init.OutputDrive = SAI_OUTPUTDRIVE_ENABLE;
  sai->Init.NoDivider = SAI_MASTERDIVIDER_DISABLE;
  sai->Init.MckOverSampling = SAI_MCK_OVERSAMPLING_DISABLE;
  sai->Init.MckOutput = SAI_MCK_OUTPUT_DISABLE;
  sai->Init.FIFOThreshold = SAI_FIFOTHRESHOLD_1QF;
  sai->Init.AudioFrequency = SAI_AUDIO_FREQUENCY_MCKDIV;
  sai->Init.Mckdiv = 16U;
  sai->Init.SynchroExt = SAI_SYNCEXT_DISABLE;
  sai->Init.MonoStereoMode = SAI_STEREOMODE;
  sai->Init.CompandingMode = SAI_NOCOMPANDING;
  sai->Init.TriState = SAI_OUTPUT_NOTRELEASED;

  /* Use the wire-native 32-bit stereo frame. The sibling jumper experiment's
     64-bit compensation made this board capture at about 24 kHz, which the
     48 kHz codec then replayed one octave high. */
  if (HAL_SAI_InitProtocol(sai, SAI_I2S_STANDARD,
                           SAI_PROTOCOL_DATASIZE_16BIT, 2U) != HAL_OK)
  {
    printf("[sai2] FAIL: SAI2A init error=0x%08lX\r\n",
           (unsigned long)HAL_SAI_GetError(sai));
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
  __HAL_LINKDMA(sai, hdmarx, hdma_sai2_a);
  HAL_NVIC_SetPriority(DMA1_Stream4_IRQn, 0U, 0U);
  HAL_NVIC_EnableIRQ(DMA1_Stream4_IRQn);
  HAL_NVIC_SetPriority(SAI2_IRQn, 0U, 0U);
  HAL_NVIC_EnableIRQ(SAI2_IRQn);

  memset(radio_rx_buffer, 0, sizeof(radio_rx_buffer));
  memset(monitor_tx_buffer, 0, sizeof(monitor_tx_buffer));
#if (__DCACHE_PRESENT == 1U)
  if ((SCB->CCR & SCB_CCR_DC_Msk) != 0U)
  {
    SCB_CleanDCache_by_Addr((uint32_t *)radio_rx_buffer,
                            sizeof(radio_rx_buffer));
    SCB_CleanDCache_by_Addr((uint32_t *)monitor_tx_buffer,
                            sizeof(monitor_tx_buffer));
  }
#endif
  rx_half_count = 0U;
  rx_full_count = 0U;
  fault_flags = 0U;
  stream_enabled = false;
  if (HAL_SAI_Receive_DMA(sai, (uint8_t *)radio_rx_buffer,
                          AUDIO_PATH_BUFFER_SAMPLES) != HAL_OK)
  {
    printf("[sai2] FAIL: RX DMA start error=0x%08lX\r\n",
           (unsigned long)HAL_SAI_GetError(sai));
    return false;
  }
  measured_fs_hz = MeasureFs(GPIOD, GPIO_PIN_12);
  printf("[sai2] PD11 RX, PD12 FS, PD13 SCK; kernel=%lu Hz; "
         "measured FS=%lu Hz\r\n", (unsigned long)sai2_kernel_hz,
         (unsigned long)measured_fs_hz);
  printf("[sai2] frame=%lu bits active=%lu bits slots=%lu MCKDIV=%lu; "
         "expect 48 kHz/1.536 MHz\r\n",
         (unsigned long)sai->FrameInit.FrameLength,
         (unsigned long)sai->FrameInit.ActiveFrameLength,
         (unsigned long)sai->SlotInit.SlotNumber,
         (unsigned long)sai->Init.Mckdiv);
  if ((measured_fs_hz < 47500U) || (measured_fs_hz > 48500U))
  {
    printf("[sai2] FAIL: radio frame sync is not 48 kHz\r\n");
    (void)HAL_SAI_DMAStop(sai);
    return false;
  }
  return true;
}

/* Monitor stage: builds the headphone signal from raw radio samples. It is a
 * pass-through today; microphone mixing and PTT belong here. The input is
 * read-only and the recorder has already copied it. */
static void RenderMonitor(const uint16_t *raw, uint16_t *monitor,
                          uint32_t sample_count)
{
  memcpy(monitor, raw, sample_count * sizeof(uint16_t));
}

static void ProcessHalf(uint32_t offset)
{
  const uint16_t *raw = &radio_rx_buffer[offset];

#if (__DCACHE_PRESENT == 1U)
  if ((SCB->CCR & SCB_CCR_DC_Msk) != 0U)
  {
    SCB_InvalidateDCache_by_Addr((uint32_t *)&radio_rx_buffer[offset],
                                 AUDIO_PATH_HALF_SAMPLES * sizeof(uint16_t));
  }
#endif
  /* Raw capture first, so monitor processing can never affect recordings. */
  RadioRecorder_OnRadioSamples((const int16_t *)raw, AUDIO_PATH_HALF_SAMPLES);
  RenderMonitor(raw, &monitor_tx_buffer[offset], AUDIO_PATH_HALF_SAMPLES);
#if (__DCACHE_PRESENT == 1U)
  if ((SCB->CCR & SCB_CCR_DC_Msk) != 0U)
  {
    SCB_CleanDCache_by_Addr((uint32_t *)&monitor_tx_buffer[offset],
                            AUDIO_PATH_HALF_SAMPLES * sizeof(uint16_t));
  }
#endif
}

bool AudioPath_StartMonitor(void)
{
  SAI_HandleTypeDef * const sai = monitor_tx_sai;
  uint32_t observed;
  uint32_t start_tick;
  uint32_t measured_fs_hz;

  if (sai == NULL)
  {
    return false;
  }

  /* Count only buffers received after the radio's digital output started. */
  rx_half_count = 0U;
  rx_full_count = 0U;
  start_tick = HAL_GetTick();
  while ((rx_full_count == 0U) && (fault_flags == 0U) &&
         ((HAL_GetTick() - start_tick) < AUDIO_PATH_START_TIMEOUT_MS))
  {
    HAL_Delay(1U);
  }
  if ((rx_full_count == 0U) || (fault_flags != 0U))
  {
    printf("[bridge] FAIL: no complete radio PCM buffer (flags=0x%08lX)\r\n",
           (unsigned long)fault_flags);
    return false;
  }

  /* At a full callback, RX is writing half 0 and half 1 is stable. */
  ProcessHalf(AUDIO_PATH_HALF_SAMPLES);
  __disable_irq();
  observed = rx_half_count;
  stream_enabled = true;
  __enable_irq();
  start_tick = HAL_GetTick();
  while ((rx_half_count == observed) && (fault_flags == 0U) &&
         ((HAL_GetTick() - start_tick) < AUDIO_PATH_START_TIMEOUT_MS))
  {
    HAL_Delay(1U);
  }
  if ((rx_half_count == observed) || (fault_flags != 0U))
  {
    printf("[bridge] FAIL: DMA prefill synchronization\r\n");
    stream_enabled = false;
    return false;
  }

  if (HAL_SAI_Transmit_DMA(sai, (uint8_t *)monitor_tx_buffer,
                           AUDIO_PATH_BUFFER_SAMPLES) != HAL_OK)
  {
    printf("[bridge] FAIL: SAI1 TX DMA error=0x%08lX\r\n",
           (unsigned long)HAL_SAI_GetError(sai));
    stream_enabled = false;
    return false;
  }
  measured_fs_hz = MeasureFs(GPIOE, GPIO_PIN_4);
  printf("[bridge] SAI1 PE4 measured FS=%lu Hz\r\n",
         (unsigned long)measured_fs_hz);
  if ((measured_fs_hz < 47500U) || (measured_fs_hz > 48500U))
  {
    printf("[bridge] FAIL: codec frame sync is not 48 kHz\r\n");
    stream_enabled = false;
    (void)HAL_SAI_DMAStop(sai);
    return false;
  }
  running = true;
  printf("[bridge] PASS: SAI2 RX DMA -> SAI1 TX DMA is running\r\n");
  return true;
}

void AudioPath_SetStreamEnabled(bool enabled)
{
  stream_enabled = enabled;
}

void AudioPath_MarkStopped(void)
{
  running = false;
}

void AudioPath_ReportFault(uint32_t flags)
{
  fault_flags |= flags;
}

void AudioPath_Stop(void)
{
  stream_enabled = false;
  running = false;
  if (monitor_tx_sai != NULL)
  {
    (void)HAL_SAI_DMAStop(monitor_tx_sai);
  }
  if (radio_rx_sai != NULL)
  {
    (void)HAL_SAI_DMAStop(radio_rx_sai);
  }
  HAL_NVIC_DisableIRQ(DMA1_Stream0_IRQn);
  HAL_NVIC_DisableIRQ(DMA1_Stream4_IRQn);
  HAL_NVIC_DisableIRQ(SAI2_IRQn);
}

bool AudioPath_IsRunning(void)
{
  return running;
}

bool AudioPath_GetStatus(AudioPathStatus *status)
{
  if (status == NULL)
  {
    return false;
  }
  status->running = running;
  status->stream_enabled = stream_enabled;
  status->fault_flags = fault_flags;
  status->rx_half_count = rx_half_count;
  status->rx_full_count = rx_full_count;
  return true;
}

void DMA1_Stream4_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&hdma_sai2_a);
}

void SAI2_IRQHandler(void)
{
  if (radio_rx_sai != NULL)
  {
    HAL_SAI_IRQHandler(radio_rx_sai);
  }
}

void HAL_SAI_RxHalfCpltCallback(SAI_HandleTypeDef *hsai)
{
  if ((hsai != NULL) && (hsai->Instance == SAI2_Block_A))
  {
    if (stream_enabled)
    {
      ProcessHalf(0U);
    }
    ++rx_half_count;
  }
}

void HAL_SAI_RxCpltCallback(SAI_HandleTypeDef *hsai)
{
  if ((hsai != NULL) && (hsai->Instance == SAI2_Block_A))
  {
    if (stream_enabled)
    {
      ProcessHalf(AUDIO_PATH_HALF_SAMPLES);
    }
    ++rx_full_count;
  }
}

void HAL_SAI_ErrorCallback(SAI_HandleTypeDef *hsai)
{
  if ((hsai != NULL) && (hsai->Instance == SAI2_Block_A))
  {
    if ((fault_flags & AUDIO_PATH_FAULT_RADIO_RX) == 0U)
      Diagnostics_Record(DIAG_AUDIO_ERROR, 1U, hsai->ErrorCode);
    fault_flags |= AUDIO_PATH_FAULT_RADIO_RX;
    RadioRecorder_NotifyRadioError();
  }
  else if ((hsai != NULL) && (hsai->Instance == SAI1_Block_A))
  {
    if ((fault_flags & AUDIO_PATH_FAULT_MONITOR_TX) == 0U)
      Diagnostics_Record(DIAG_AUDIO_ERROR, 2U, hsai->ErrorCode);
    fault_flags |= AUDIO_PATH_FAULT_MONITOR_TX;
  }
}
