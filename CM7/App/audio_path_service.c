/* Extracted from the proven M7 radio-to-headphone bridge without changing SAI
 * or DMA configuration, buffer placement, callback cadence or ISR work. */
#include "audio_path_service.h"

#include "diagnostics.h"
#include "main.h"
#include "monitor_ptt.h"
#include "radio_activity_feed.h"
#include "radio_recorder.h"
#include "usb_test.h"
#if defined(SPOOKY_DEMO)
#include "demo_clip.h"
#endif

#include <stdio.h>
#include <string.h>

#define AUDIO_PATH_BUFFER_SAMPLES      2048U
#define AUDIO_PATH_HALF_SAMPLES        (AUDIO_PATH_BUFFER_SAMPLES / 2U)
#define AUDIO_PATH_START_TIMEOUT_MS    500U
/* Decision 0028: PTT fades the monitored radio over 5 ms (240 frames at 48 kHz),
 * like an engine switch (decision 0027). Configurable until a bench sets it. */
#ifndef SPOOKY_PTT_RAMP_FRAMES
#define SPOOKY_PTT_RAMP_FRAMES         240U
#endif
/* Decision 0012 item 6: a foreground observation, at most one 75 ms pass. */
#define AUDIO_PATH_PTT_UNCERTAINTY_FRAMES 3600U

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
static AudioTimelineStream radio_timeline;
static MonitorPtt monitor_ptt;
static AudioPathPttStatus ptt_status; /* foreground only */

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
  MonitorPtt_Init(&monitor_ptt, SPOOKY_PTT_RAMP_FRAMES);
  (void)memset(&ptt_status, 0, sizeof(ptt_status));
  ptt_status.ramp_frames = SPOOKY_PTT_RAMP_FRAMES;
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
  /* Just below SysTick (priority 0): a radio half takes about 1.2 ms in the
   * Debug image while recording, and at SysTick's priority it lost ticks
   * (full_spooky_proto-akw). It still preempts the microphone copy (4). */
  HAL_NVIC_SetPriority(DMA1_Stream4_IRQn, 1U, 0U);
  HAL_NVIC_EnableIRQ(DMA1_Stream4_IRQn);
  HAL_NVIC_SetPriority(SAI2_IRQn, 1U, 0U);
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
  /* Block indices restart with the counters. */
  (void)RadioActivityFeed_Init();
  /* The DMA starts at index 0 with no halves completed: a new epoch. */
  (void)AudioTimeline_StreamStart(&radio_timeline,
                                  AUDIO_PATH_HALF_SAMPLES / 2U, 2U, 0U);
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

/* Monitor stage: builds the headphone signal from raw radio samples, then PTT
 * fades the radio out of it (decision 0028). The microphone is not monitored.
 * The input is read-only and the recorder has already copied it. */
static void RenderMonitor(const uint16_t *raw, uint16_t *monitor,
                          uint32_t sample_count)
{
#if defined(SPOOKY_DEMO)
  /* Demo only (decision 0011 item 15, p04.6): Instrument's loaded clip replaces
   * the radio in the monitor. The raw capture above is unchanged. */
  if (DemoClip_RenderMonitor(monitor, sample_count / 2U))
  {
    return;
  }
#endif
  memcpy(monitor, raw, sample_count * sizeof(uint16_t));
  MonitorPtt_Apply(&monitor_ptt, (int16_t *)monitor, sample_count / 2U);
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
  /* Last, after the headphone copy: one level per block for the radio onset
   * detector, indexed by the completed-half count before this block. */
  RadioActivityFeed_OnBlock(rx_half_count + rx_full_count, (const int16_t *)raw,
                            AUDIO_PATH_HALF_SAMPLES);
}

bool AudioPath_StartMonitor(void)
{
  SAI_HandleTypeDef * const sai = monitor_tx_sai;
  uint32_t observed;
  uint32_t full_base;
  uint32_t start_tick;
  uint32_t measured_fs_hz;

  if (sai == NULL)
  {
    return false;
  }

  /* Wait for a buffer received after the radio's digital output started. The
   * counters keep running: they are the timeline's completed-half count, whose
   * parity must match the half the DMA is filling (decision 0012 item 2). */
  full_base = rx_full_count;
  start_tick = HAL_GetTick();
  while ((rx_full_count == full_base) && (fault_flags == 0U) &&
         ((HAL_GetTick() - start_tick) < AUDIO_PATH_START_TIMEOUT_MS))
  {
    HAL_Delay(1U);
  }
  if ((rx_full_count == full_base) || (fault_flags != 0U))
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

/* Reads the completed-half count and the DMA down-counter together; the
 * caller masks interrupts so no completion is counted between the reads. */
static bool ObserveLocked(AudioTimelinePosition *position)
{
  const uint32_t completed = rx_half_count + rx_full_count;
  const uint32_t remaining = __HAL_DMA_GET_COUNTER(&hdma_sai2_a);

  return AudioTimeline_StreamObserve(&radio_timeline, completed, remaining,
                                     position) == AUDIO_TIMELINE_OK;
}

bool AudioPath_GetBlockInProgress(uint32_t *block)
{
  AudioTimelinePosition position;

  if ((block == NULL) || !AudioPath_GetPosition(&position))
  {
    return false;
  }
  *block = (uint32_t)(position.frame / (AUDIO_PATH_HALF_SAMPLES / 2U));
  return true;
}

bool AudioPath_GetPosition(AudioTimelinePosition *position)
{
  const uint32_t primask = __get_PRIMASK();
  bool ok;

  if ((position == NULL) || !running)
  {
    return false;
  }
  __disable_irq();
  ok = ObserveLocked(position);
  __set_PRIMASK(primask);
  return ok;
}

bool AudioPath_AlignCaptureLocked(uint32_t mic_latency_frames,
                                  AudioPathCaptureStart *start)
{
  uint32_t tx_remaining;
  uint32_t rx_index;
  uint32_t tx_index;

  if ((start == NULL) || !running || !stream_enabled)
  {
    return false;
  }
  (void)memset(start, 0, sizeof(*start));
  if (!ObserveLocked(&start->mic_start) ||
      (AudioTimeline_AlignStart(&radio_timeline, start->mic_start,
                                mic_latency_frames, &start->alignment) !=
       AUDIO_TIMELINE_OK) ||
      (AudioTimeline_DeliverySkip(&start->alignment,
                                  AudioTimeline_NextHalfFrame(&radio_timeline),
                                  &start->skip_frames) != AUDIO_TIMELINE_OK))
  {
    return false;
  }
  /* Monitor path phase for the loopback qualification (decision 0012 item 9).
   * Both buffers are AUDIO_PATH_BUFFER_SAMPLES halfwords of stereo frames. */
  if ((monitor_tx_sai != NULL) && (monitor_tx_sai->hdmatx != NULL))
  {
    tx_remaining = __HAL_DMA_GET_COUNTER(monitor_tx_sai->hdmatx);
    rx_index = (AUDIO_PATH_BUFFER_SAMPLES -
                __HAL_DMA_GET_COUNTER(&hdma_sai2_a)) % AUDIO_PATH_BUFFER_SAMPLES;
    tx_index = (AUDIO_PATH_BUFFER_SAMPLES - tx_remaining) %
               AUDIO_PATH_BUFFER_SAMPLES;
    start->monitor_phase_frames =
      ((rx_index + AUDIO_PATH_BUFFER_SAMPLES - tx_index) %
       AUDIO_PATH_BUFFER_SAMPLES) / 2U;
    start->monitor_phase_valid = true;
  }
  return true;
}

void AudioPath_SetPtt(bool on)
{
  AudioTimelineStamp stamp;

  if (on == MonitorPtt_IsOn(&monitor_ptt))
  {
    return;
  }
  MonitorPtt_Set(&monitor_ptt, on);
  (void)memset(&stamp, 0, sizeof(stamp));
  stamp.uncertainty_frames = AUDIO_PATH_PTT_UNCERTAINTY_FRAMES;
  if (!AudioPath_GetPosition(&stamp.position))
  {
    ++ptt_status.unstamped; /* the radio stream is not running */
  }
  if (on)
  {
    ++ptt_status.presses;
    ptt_status.last_on = stamp;
  }
  else
  {
    ++ptt_status.releases;
    ptt_status.last_off = stamp;
  }
}

bool AudioPath_GetPttStatus(AudioPathPttStatus *status)
{
  if (status == NULL)
  {
    return false;
  }
  *status = ptt_status;
  status->on = MonitorPtt_IsOn(&monitor_ptt);
  status->gain_q15 = MonitorPtt_GainQ15(&monitor_ptt);
  return true;
}

/* Decimal text of a 64-bit frame count; newlib-nano printf has no %llu. */
static void FormatFrame(uint64_t frame, char text[21])
{
  char digits[21];
  uint32_t count = 0U;
  uint32_t index;

  do
  {
    digits[count++] = (char)('0' + (frame % 10U));
    frame /= 10U;
  } while ((frame != 0U) && (count < 20U));
  for (index = 0U; index < count; ++index)
  {
    text[index] = digits[count - 1U - index];
  }
  text[count] = '\0';
}

/* A stamp as epoch:frame, or NONE before the first. */
static void FormatStamp(char *text, size_t size, const AudioTimelineStamp *stamp)
{
  char frame[21];

  if (stamp->position.epoch == 0U)
  {
    (void)snprintf(text, size, "NONE");
  }
  else
  {
    FormatFrame(stamp->position.frame, frame);
    (void)snprintf(text, size, "%lu:%s", (unsigned long)stamp->position.epoch, frame);
  }
}

static void SendMonitorStatus(void)
{
  AudioPathPttStatus status;
  char on_text[32];
  char off_text[32];
  char line[200];

  (void)AudioPath_GetPttStatus(&status);
  FormatStamp(on_text, sizeof(on_text), &status.last_on);
  FormatStamp(off_text, sizeof(off_text), &status.last_off);
  (void)snprintf(line, sizeof(line),
                 "OK MONITOR PTT=%u GAIN_Q15=%lu RAMP_FRAMES=%lu MIC=OFF PRESSES=%lu "
                 "RELEASES=%lu UNSTAMPED=%lu LAST_ON=%s LAST_OFF=%s UNCERTAINTY=%lu\r\n",
                 status.on ? 1U : 0U, (unsigned long)status.gain_q15,
                 (unsigned long)status.ramp_frames, (unsigned long)status.presses,
                 (unsigned long)status.releases, (unsigned long)status.unstamped,
                 on_text, off_text, (unsigned long)AUDIO_PATH_PTT_UNCERTAINTY_FRAMES);
  (void)UsbTest_SendText(line);
}

bool AudioPath_HandleCommand(const char *command)
{
  if (command == NULL)
  {
    return false;
  }
  if ((strcmp(command, "MONITOR") == 0) || (strcmp(command, "MONITOR STATUS") == 0))
  {
    SendMonitorStatus();
    return true;
  }
  if (strcmp(command, "MONITOR PTT ON") == 0)
  {
    AudioPath_SetPtt(true);
    SendMonitorStatus();
    return true;
  }
  if (strcmp(command, "MONITOR PTT OFF") == 0)
  {
    AudioPath_SetPtt(false);
    SendMonitorStatus();
    return true;
  }
  if ((strncmp(command, "MONITOR", 7U) == 0) &&
      ((command[7] == ' ') || (command[7] == '\t')))
  {
    (void)UsbTest_SendText("ERR usage: MONITOR [STATUS] | MONITOR PTT ON|OFF\r\n");
    return true;
  }
  return false;
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
