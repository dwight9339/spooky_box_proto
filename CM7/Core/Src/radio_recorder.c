#include "radio_recorder.h"

#include "diskio.h"
#include "ff.h"
#include "main.h"
#include "sd_test.h"
#include "usb_test.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define RECORDER_SAMPLE_RATE_HZ          48000U
#define RECORDER_CHANNELS                    3U
#define RECORDER_BYTES_PER_SAMPLE            2U
#define RECORDER_BLOCK_ALIGN \
  (RECORDER_CHANNELS * RECORDER_BYTES_PER_SAMPLE)
#define RECORDER_WAV_HEADER_BYTES            44U
#define RECORDER_BLOCK_FRAMES              4096U
#define RECORDER_RADIO_BLOCK_SAMPLES \
  (RECORDER_BLOCK_FRAMES * 2U)
#define RECORDER_PDM_BLOCK_SAMPLES \
  RECORDER_BLOCK_FRAMES
#define RECORDER_OUTPUT_SAMPLES \
  (RECORDER_BLOCK_FRAMES * RECORDER_CHANNELS)
#define RECORDER_OUTPUT_BYTES \
  (RECORDER_OUTPUT_SAMPLES * sizeof(int16_t))
#define RECORDER_QUEUE_DEPTH                  8U
#define RECORDER_DEFAULT_SECONDS             60U
#define RECORDER_MAX_SECONDS               3600U
#define RECORDER_PREALLOC_SECONDS            60U
#define RECORDER_PROGRESS_PERIOD_MS        5000U
#define RECORDER_MOUNT_CLOCK_DIV              2U
#define RECORDER_TRANSFER_CLOCK_DIV           0U
#define RECORDER_PDM_DC_POLE_Q15           32640
#define RECORDER_PDM_DC_SCALE              32768
#define RECORDER_WAV_MAX_FRAMES \
  ((UINT32_MAX - 36U) / RECORDER_BLOCK_ALIGN)

typedef enum
{
  RECORDER_IDLE = 0,
  RECORDER_ACTIVE
} RecorderState;

static DFSDM_Filter_HandleTypeDef *pdm_filter;
static FATFS recorder_filesystem;
static FIL recorder_file;
static RecorderState recorder_state;
static bool recorder_file_open;
static bool recorder_filesystem_mounted;
static bool pdm_dma_running;
static volatile bool capture_enabled;
static volatile bool stop_requested;
static volatile bool radio_error;
static volatile bool pdm_error;
static volatile bool radio_overrun;
static volatile bool pdm_overrun;
static char recorder_filename[13];

static int32_t pdm_dma_buffer[2U * RECORDER_PDM_BLOCK_SAMPLES]
  __attribute__((section(".dma_buffer"), aligned(32)));
static int16_t radio_queue[RECORDER_QUEUE_DEPTH][RECORDER_RADIO_BLOCK_SAMPLES]
  __attribute__((section(".dma_buffer"), aligned(32)));
static int32_t pdm_queue[RECORDER_QUEUE_DEPTH][RECORDER_PDM_BLOCK_SAMPLES]
  __attribute__((section(".dma_buffer"), aligned(32)));
static int16_t output_block[RECORDER_OUTPUT_SAMPLES]
  __attribute__((section(".dma_buffer"), aligned(32)));

static volatile uint8_t radio_queue_head;
static volatile uint8_t radio_queue_tail;
static volatile uint8_t radio_queue_count;
static volatile uint8_t radio_queue_high_water;
static volatile uint32_t radio_fill_samples;
static volatile uint8_t pdm_queue_head;
static volatile uint8_t pdm_queue_tail;
static volatile uint8_t pdm_queue_count;
static volatile uint8_t pdm_queue_high_water;

static uint32_t target_frames;
static uint32_t frames_written;
static uint32_t recording_start_ms;
static uint32_t progress_last_ms;
static uint32_t max_write_ms;
static uint32_t radio_left_peak;
static uint32_t radio_right_peak;
static uint32_t pdm_peak;
static int32_t pdm_dc_previous_input;
static int32_t pdm_dc_previous_output;

static void RecorderSend(const char *format, ...)
{
  char message[240];
  va_list args;
  int length;
  uint32_t start_ms;

  va_start(args, format);
  length = vsnprintf(message, sizeof(message), format, args);
  va_end(args);
  if (length <= 0)
  {
    return;
  }
  message[sizeof(message) - 1U] = '\0';

  start_ms = HAL_GetTick();
  while (!UsbTest_SendText(message) &&
         ((HAL_GetTick() - start_ms) < 250U))
  {
    HAL_Delay(1U);
  }
  printf("[record] %s", message);
}

static const char *RecorderFsResultName(FRESULT result)
{
  static const char *const names[] = {
    "FR_OK", "FR_DISK_ERR", "FR_INT_ERR", "FR_NOT_READY", "FR_NO_FILE",
    "FR_NO_PATH", "FR_INVALID_NAME", "FR_DENIED", "FR_EXIST",
    "FR_INVALID_OBJECT", "FR_WRITE_PROTECTED", "FR_INVALID_DRIVE",
    "FR_NOT_ENABLED", "FR_NO_FILESYSTEM", "FR_MKFS_ABORTED", "FR_TIMEOUT",
    "FR_LOCKED", "FR_NOT_ENOUGH_CORE", "FR_TOO_MANY_OPEN_FILES",
    "FR_INVALID_PARAMETER"
  };

  return ((unsigned int)result < (sizeof(names) / sizeof(names[0])))
    ? names[(unsigned int)result] : "FR_UNKNOWN";
}

static void RecorderSetSdClockDiv(uint32_t clock_div)
{
  hsd1.Init.ClockDiv = clock_div;
  if (HAL_SD_GetState(&hsd1) != HAL_SD_STATE_RESET)
  {
    MODIFY_REG(hsd1.Instance->CLKCR, SDMMC_CLKCR_CLKDIV, clock_div);
  }
}

static void RecorderPutLe16(uint8_t *destination, uint16_t value)
{
  destination[0] = (uint8_t)value;
  destination[1] = (uint8_t)(value >> 8);
}

static void RecorderPutLe32(uint8_t *destination, uint32_t value)
{
  destination[0] = (uint8_t)value;
  destination[1] = (uint8_t)(value >> 8);
  destination[2] = (uint8_t)(value >> 16);
  destination[3] = (uint8_t)(value >> 24);
}

static bool RecorderWriteHeader(uint32_t data_bytes)
{
  uint8_t header[RECORDER_WAV_HEADER_BYTES];
  UINT written = 0U;

  memcpy(&header[0], "RIFF", 4U);
  RecorderPutLe32(&header[4], 36U + data_bytes);
  memcpy(&header[8], "WAVE", 4U);
  memcpy(&header[12], "fmt ", 4U);
  RecorderPutLe32(&header[16], 16U);
  RecorderPutLe16(&header[20], 1U);
  RecorderPutLe16(&header[22], RECORDER_CHANNELS);
  RecorderPutLe32(&header[24], RECORDER_SAMPLE_RATE_HZ);
  RecorderPutLe32(&header[28],
                  RECORDER_SAMPLE_RATE_HZ * RECORDER_BLOCK_ALIGN);
  RecorderPutLe16(&header[32], RECORDER_BLOCK_ALIGN);
  RecorderPutLe16(&header[34], 16U);
  memcpy(&header[36], "data", 4U);
  RecorderPutLe32(&header[40], data_bytes);

  return (f_write(&recorder_file, header, sizeof(header), &written) == FR_OK) &&
         (written == sizeof(header));
}

static void RecorderUnmount(void)
{
  if (recorder_filesystem_mounted)
  {
    (void)f_mount(NULL, "", 0U);
    recorder_filesystem_mounted = false;
  }
}

static void RecorderDiscardFile(void)
{
  if (recorder_file_open)
  {
    (void)f_close(&recorder_file);
    recorder_file_open = false;
    (void)f_unlink(recorder_filename);
  }
  RecorderUnmount();
}

static bool RecorderOpenFile(uint32_t requested_seconds)
{
  FRESULT result;
  FILINFO info;
  uint32_t prealloc_seconds = requested_seconds;
  uint32_t prealloc_bytes;
  unsigned int index;

  if (!SD_CARD_IS_PRESENT())
  {
    RecorderSend("ERR RECORD no SD card detected\r\n");
    return false;
  }

  SdTest_Stop();
  RecorderSetSdClockDiv(RECORDER_MOUNT_CLOCK_DIV);
  result = f_mount(&recorder_filesystem, "", 1U);
  if (result != FR_OK)
  {
    RecorderSend("ERR RECORD mount failed result=%s(%u) hal=0x%08lX\r\n",
                 RecorderFsResultName(result), (unsigned int)result,
                 (unsigned long)HAL_SD_GetError(&hsd1));
    return false;
  }
  recorder_filesystem_mounted = true;
  RecorderSetSdClockDiv(RECORDER_TRANSFER_CLOCK_DIV);

  for (index = 0U; index < 1000U; ++index)
  {
    (void)snprintf(recorder_filename, sizeof(recorder_filename),
                   "REC%03u.WAV", index);
    result = f_stat(recorder_filename, &info);
    if (result == FR_NO_FILE)
    {
      break;
    }
    if (result != FR_OK)
    {
      RecorderSend("ERR RECORD filename scan failed result=%s(%u)\r\n",
                   RecorderFsResultName(result), (unsigned int)result);
      RecorderUnmount();
      return false;
    }
  }
  if (index >= 1000U)
  {
    RecorderSend("ERR RECORD no free REC###.WAV filename\r\n");
    RecorderUnmount();
    return false;
  }

  result = f_open(&recorder_file, recorder_filename,
                  FA_CREATE_NEW | FA_WRITE);
  if (result != FR_OK)
  {
    RecorderSend("ERR RECORD create failed result=%s(%u)\r\n",
                 RecorderFsResultName(result), (unsigned int)result);
    RecorderUnmount();
    return false;
  }
  recorder_file_open = true;

  if (prealloc_seconds > RECORDER_PREALLOC_SECONDS)
  {
    prealloc_seconds = RECORDER_PREALLOC_SECONDS;
  }
  prealloc_bytes = RECORDER_WAV_HEADER_BYTES +
    prealloc_seconds * RECORDER_SAMPLE_RATE_HZ * RECORDER_BLOCK_ALIGN;
  result = f_expand(&recorder_file, prealloc_bytes, 1U);
  if (result == FR_DENIED)
  {
    RecorderSend("WARN RECORD contiguous preallocation unavailable; "
                 "using dynamic growth\r\n");
  }
  else if (result != FR_OK)
  {
    RecorderSend("ERR RECORD preallocation failed result=%s(%u)\r\n",
                 RecorderFsResultName(result), (unsigned int)result);
    RecorderDiscardFile();
    return false;
  }

  if (!RecorderWriteHeader(0U))
  {
    RecorderSend("ERR RECORD initial WAV header write failed\r\n");
    RecorderDiscardFile();
    return false;
  }
  return true;
}

static bool RecorderFinalizeFile(void)
{
  uint32_t data_bytes = frames_written * RECORDER_BLOCK_ALIGN;
  bool ok = true;

  if (!recorder_file_open)
  {
    RecorderUnmount();
    return true;
  }
  if ((f_lseek(&recorder_file,
               RECORDER_WAV_HEADER_BYTES + data_bytes) != FR_OK) ||
      (f_truncate(&recorder_file) != FR_OK))
  {
    ok = false;
  }
  if (f_lseek(&recorder_file, 0U) != FR_OK)
  {
    ok = false;
  }
  else if (!RecorderWriteHeader(data_bytes))
  {
    ok = false;
  }
  if (f_sync(&recorder_file) != FR_OK)
  {
    ok = false;
  }
  if (f_close(&recorder_file) != FR_OK)
  {
    ok = false;
  }
  recorder_file_open = false;
  RecorderUnmount();
  return ok;
}

static void RecorderPrepareDmaBuffer(void *address, uint32_t bytes)
{
#if (__DCACHE_PRESENT == 1U)
  if ((SCB->CCR & SCB_CCR_DC_Msk) != 0U)
  {
    SCB_CleanInvalidateDCache_by_Addr((uint32_t *)address, (int32_t)bytes);
  }
#else
  (void)address;
  (void)bytes;
#endif
}

static void RecorderInvalidateDmaBuffer(void *address, uint32_t bytes)
{
#if (__DCACHE_PRESENT == 1U)
  if ((SCB->CCR & SCB_CCR_DC_Msk) != 0U)
  {
    uintptr_t start = (uintptr_t)address & ~(uintptr_t)31U;
    uintptr_t end = ((uintptr_t)address + bytes + 31U) & ~(uintptr_t)31U;
    SCB_InvalidateDCache_by_Addr((uint32_t *)start,
                                 (int32_t)(end - start));
  }
#else
  (void)address;
  (void)bytes;
#endif
}

static void RecorderEnqueuePdm(int32_t *source)
{
  uint8_t tail;

  if (!capture_enabled)
  {
    return;
  }
  if (pdm_queue_count >= RECORDER_QUEUE_DEPTH)
  {
    pdm_overrun = true;
    return;
  }

  tail = pdm_queue_tail;
  RecorderInvalidateDmaBuffer(source,
                              RECORDER_PDM_BLOCK_SAMPLES * sizeof(int32_t));
  memcpy(pdm_queue[tail], source,
         RECORDER_PDM_BLOCK_SAMPLES * sizeof(int32_t));
  __DMB();
  pdm_queue_tail = (uint8_t)((tail + 1U) % RECORDER_QUEUE_DEPTH);
  ++pdm_queue_count;
  if (pdm_queue_count > pdm_queue_high_water)
  {
    pdm_queue_high_water = pdm_queue_count;
  }
}

void RadioRecorder_OnRadioSamples(const int16_t *samples,
                                  uint32_t sample_count)
{
  uint32_t copied = 0U;

  if (!capture_enabled || (samples == NULL))
  {
    return;
  }

  while (copied < sample_count)
  {
    uint32_t available;
    uint32_t copy_count;
    uint8_t tail;

    if (radio_queue_count >= RECORDER_QUEUE_DEPTH)
    {
      radio_overrun = true;
      return;
    }
    tail = radio_queue_tail;
    available = RECORDER_RADIO_BLOCK_SAMPLES - radio_fill_samples;
    copy_count = sample_count - copied;
    if (copy_count > available)
    {
      copy_count = available;
    }
    memcpy(&radio_queue[tail][radio_fill_samples], &samples[copied],
           copy_count * sizeof(int16_t));
    copied += copy_count;
    radio_fill_samples += copy_count;

    if (radio_fill_samples == RECORDER_RADIO_BLOCK_SAMPLES)
    {
      __DMB();
      radio_fill_samples = 0U;
      radio_queue_tail = (uint8_t)((tail + 1U) % RECORDER_QUEUE_DEPTH);
      ++radio_queue_count;
      if (radio_queue_count > radio_queue_high_water)
      {
        radio_queue_high_water = radio_queue_count;
      }
    }
  }
}

static void RecorderReleaseQueues(void)
{
  uint32_t primask = __get_PRIMASK();

  __disable_irq();
  radio_queue_head =
    (uint8_t)((radio_queue_head + 1U) % RECORDER_QUEUE_DEPTH);
  pdm_queue_head =
    (uint8_t)((pdm_queue_head + 1U) % RECORDER_QUEUE_DEPTH);
  --radio_queue_count;
  --pdm_queue_count;
  if (primask == 0U)
  {
    __enable_irq();
  }
}

static bool RecorderWriteBlock(const int16_t *radio, const int32_t *pdm)
{
  UINT written = 0U;
  FRESULT result;
  uint32_t write_start;
  uint32_t write_ms;

  for (uint32_t frame = 0U; frame < RECORDER_BLOCK_FRAMES; ++frame)
  {
    int32_t left = radio[2U * frame];
    int32_t right = radio[(2U * frame) + 1U];
    int32_t mic_unscaled = pdm[frame] / 256;
    int32_t mic_dc_blocked = mic_unscaled - pdm_dc_previous_input +
      (int32_t)(((int64_t)RECORDER_PDM_DC_POLE_Q15 *
                 pdm_dc_previous_output) / RECORDER_PDM_DC_SCALE);
    int32_t mic = mic_dc_blocked;
    uint32_t magnitude;

    pdm_dc_previous_input = mic_unscaled;
    pdm_dc_previous_output = mic_dc_blocked;
    if (mic > 32767) mic = 32767;
    else if (mic < -32768) mic = -32768;

    magnitude = (uint32_t)((left < 0) ? -left : left);
    if (magnitude > radio_left_peak) radio_left_peak = magnitude;
    magnitude = (uint32_t)((right < 0) ? -right : right);
    if (magnitude > radio_right_peak) radio_right_peak = magnitude;
    magnitude = (uint32_t)((mic < 0) ? -mic : mic);
    if (magnitude > pdm_peak) pdm_peak = magnitude;

    output_block[3U * frame] = (int16_t)left;
    output_block[(3U * frame) + 1U] = (int16_t)right;
    output_block[(3U * frame) + 2U] = (int16_t)mic;
  }

  write_start = HAL_GetTick();
  result = f_write(&recorder_file, output_block,
                   RECORDER_OUTPUT_BYTES, &written);
  write_ms = HAL_GetTick() - write_start;
  if (write_ms > max_write_ms)
  {
    max_write_ms = write_ms;
  }
  if ((result != FR_OK) || (written != RECORDER_OUTPUT_BYTES))
  {
    RecorderSend("ERR RECORD write failed result=%s(%u) bytes=%u/%u\r\n",
                 RecorderFsResultName(result), (unsigned int)result,
                 (unsigned int)written,
                 (unsigned int)RECORDER_OUTPUT_BYTES);
    return false;
  }
  frames_written += RECORDER_BLOCK_FRAMES;
  return true;
}

static void RecorderStopCapture(void)
{
  capture_enabled = false;
  if (pdm_dma_running)
  {
    (void)HAL_DFSDM_FilterRegularStop_DMA(pdm_filter);
    pdm_dma_running = false;
  }
}

static void RecorderReportDiagnostics(void)
{
  RecorderSend("RECORD DIAG queues radio=%u/%u pdm=%u/%u "
               "max-write=%lums peaks=%lu,%lu,%lu\r\n",
               (unsigned int)radio_queue_high_water,
               RECORDER_QUEUE_DEPTH,
               (unsigned int)pdm_queue_high_water,
               RECORDER_QUEUE_DEPTH,
               (unsigned long)max_write_ms,
               (unsigned long)radio_left_peak,
               (unsigned long)radio_right_peak,
               (unsigned long)pdm_peak);
}

static void RecorderFinish(bool aborted, const char *reason)
{
  bool finalized;
  uint32_t elapsed_ms;
  uint32_t data_bytes = frames_written * RECORDER_BLOCK_ALIGN;

  RecorderStopCapture();
  finalized = RecorderFinalizeFile();
  recorder_state = RECORDER_IDLE;
  stop_requested = false;
  elapsed_ms = HAL_GetTick() - recording_start_ms;

  if (aborted || !finalized)
  {
    RecorderSend("ERR RECORD ABORT file=%s frames=%lu bytes=%lu "
                 "reason=%s finalized=%u\r\n",
                 recorder_filename, (unsigned long)frames_written,
                 (unsigned long)data_bytes, reason,
                 finalized ? 1U : 0U);
  }
  else
  {
    RecorderSend("OK RECORD PASS file=%s frames=%lu bytes=%lu "
                 "audio=%lu.%03lus elapsed=%lums\r\n",
                 recorder_filename, (unsigned long)frames_written,
                 (unsigned long)data_bytes,
                 (unsigned long)(frames_written / RECORDER_SAMPLE_RATE_HZ),
                 (unsigned long)(((frames_written % RECORDER_SAMPLE_RATE_HZ) *
                                  1000U) / RECORDER_SAMPLE_RATE_HZ),
                 (unsigned long)elapsed_ms);
  }
  RecorderReportDiagnostics();
}

static bool RecorderStart(uint32_t seconds, bool radio_ready)
{
  uint32_t primask;

  if (recorder_state == RECORDER_ACTIVE)
  {
    RecorderSend("ERR RECORD already active\r\n");
    return false;
  }
  if (!radio_ready)
  {
    RecorderSend("ERR RECORD radio audio path is not running\r\n");
    return false;
  }
  if ((pdm_filter == NULL) ||
      (HAL_DFSDM_FilterGetState(pdm_filter) == HAL_DFSDM_FILTER_STATE_ERROR))
  {
    RecorderSend("ERR RECORD PDM path is not ready\r\n");
    return false;
  }
  if (!RecorderOpenFile(seconds))
  {
    return false;
  }

  primask = __get_PRIMASK();
  __disable_irq();
  radio_queue_head = 0U;
  radio_queue_tail = 0U;
  radio_queue_count = 0U;
  radio_queue_high_water = 0U;
  radio_fill_samples = 0U;
  pdm_queue_head = 0U;
  pdm_queue_tail = 0U;
  pdm_queue_count = 0U;
  pdm_queue_high_water = 0U;
  capture_enabled = false;
  if (primask == 0U)
  {
    __enable_irq();
  }

  frames_written = 0U;
  target_frames = seconds * RECORDER_SAMPLE_RATE_HZ;
  max_write_ms = 0U;
  radio_left_peak = 0U;
  radio_right_peak = 0U;
  pdm_peak = 0U;
  pdm_dc_previous_input = 0;
  pdm_dc_previous_output = 0;
  radio_error = false;
  pdm_error = false;
  radio_overrun = false;
  pdm_overrun = false;
  stop_requested = false;

  RecorderPrepareDmaBuffer(pdm_dma_buffer, sizeof(pdm_dma_buffer));
  recorder_state = RECORDER_ACTIVE;
  capture_enabled = true;
  if (HAL_DFSDM_FilterRegularStart_DMA(
        pdm_filter, pdm_dma_buffer,
        2U * RECORDER_PDM_BLOCK_SAMPLES) != HAL_OK)
  {
    RecorderFinish(true, "PDM DMA did not start");
    return false;
  }
  pdm_dma_running = true;
  recording_start_ms = HAL_GetTick();
  progress_last_ms = recording_start_ms;
  RecorderSend("OK RECORD START file=%s duration=%lus "
               "format=48000Hz/16-bit/3ch [radio-L,radio-R,mic]\r\n",
               recorder_filename, (unsigned long)seconds);
  return true;
}

static bool RecorderParseSeconds(const char *text, uint32_t *seconds)
{
  uint32_t value = 0U;
  bool digit = false;

  while ((*text == ' ') || (*text == '\t')) ++text;
  while ((*text >= '0') && (*text <= '9'))
  {
    uint32_t next = (uint32_t)(*text - '0');
    if (value > ((UINT32_MAX - next) / 10U)) return false;
    value = (value * 10U) + next;
    digit = true;
    ++text;
  }
  while ((*text == ' ') || (*text == '\t')) ++text;
  if (!digit || (*text != '\0')) return false;
  *seconds = value;
  return true;
}

void RadioRecorder_Init(DFSDM_Filter_HandleTypeDef *filter)
{
  pdm_filter = filter;
  recorder_state = RECORDER_IDLE;
  recorder_file_open = false;
  recorder_filesystem_mounted = false;
  pdm_dma_running = false;
  capture_enabled = false;
  recorder_filename[0] = '\0';
  printf("[record] three-channel recorder ready; default=%us\r\n",
         RECORDER_DEFAULT_SECONDS);
}

bool RadioRecorder_HandleCommand(const char *command, bool radio_ready)
{
  uint32_t seconds = RECORDER_DEFAULT_SECONDS;

  if ((command == NULL) ||
      (strncmp(command, "RECORD", 6U) != 0) ||
      ((command[6] != '\0') && (command[6] != ' ') &&
       (command[6] != '\t')))
  {
    return false;
  }
  if ((strcmp(command, "RECORD") == 0) ||
      (strcmp(command, "RECORD STATUS") == 0))
  {
    if (recorder_state == RECORDER_ACTIVE)
    {
      RecorderSend("OK RECORD ACTIVE file=%s audio=%lu.%01lus "
                   "queues=%u/%u,%u/%u max-write=%lums\r\n",
                   recorder_filename,
                   (unsigned long)(frames_written / RECORDER_SAMPLE_RATE_HZ),
                   (unsigned long)(((frames_written % RECORDER_SAMPLE_RATE_HZ) *
                                    10U) / RECORDER_SAMPLE_RATE_HZ),
                   (unsigned int)radio_queue_count, RECORDER_QUEUE_DEPTH,
                   (unsigned int)pdm_queue_count, RECORDER_QUEUE_DEPTH,
                   (unsigned long)max_write_ms);
    }
    else
    {
      RecorderSend("OK RECORD IDLE last-file=%s frames=%lu "
                   "max-write=%lums\r\n",
                   (recorder_filename[0] != '\0') ? recorder_filename : "none",
                   (unsigned long)frames_written,
                   (unsigned long)max_write_ms);
    }
    return true;
  }
  if (strcmp(command, "RECORD STOP") == 0)
  {
    if (recorder_state != RECORDER_ACTIVE)
    {
      RecorderSend("OK RECORD already idle\r\n");
    }
    else
    {
      stop_requested = true;
      RecorderSend("OK RECORD STOP requested; finalizing next matched block\r\n");
    }
    return true;
  }
  if (strncmp(command, "RECORD START", 12U) == 0)
  {
    if (command[12] != '\0')
    {
      if (((command[12] != ' ') && (command[12] != '\t')) ||
          !RecorderParseSeconds(&command[12], &seconds))
      {
        RecorderSend("ERR usage: RECORD START [seconds]\r\n");
        return true;
      }
    }
    if ((seconds == 0U) || (seconds > RECORDER_MAX_SECONDS))
    {
      RecorderSend("ERR RECORD duration must be 1..3600 seconds\r\n");
      return true;
    }
    (void)RecorderStart(seconds, radio_ready);
    return true;
  }

  RecorderSend("ERR usage: RECORD STATUS|START [seconds]|STOP\r\n");
  return true;
}

void RadioRecorder_Service(void)
{
  int16_t *radio;
  int32_t *pdm;
  uint32_t now;

  if (recorder_state != RECORDER_ACTIVE)
  {
    return;
  }
  if (!SD_CARD_IS_PRESENT())
  {
    RecorderFinish(true, "SD card removed");
    return;
  }
  if (radio_error || pdm_error || radio_overrun || pdm_overrun)
  {
    const char *reason = radio_error ? "radio DMA error" :
      pdm_error ? "PDM DMA error" :
      radio_overrun ? "radio queue overrun" : "PDM queue overrun";
    RecorderFinish(true, reason);
    return;
  }
  if ((frames_written + RECORDER_BLOCK_FRAMES) > RECORDER_WAV_MAX_FRAMES)
  {
    RecorderFinish(false, "WAV size limit");
    return;
  }

  radio = (radio_queue_count != 0U) ? radio_queue[radio_queue_head] : NULL;
  pdm = (pdm_queue_count != 0U) ? pdm_queue[pdm_queue_head] : NULL;
  if ((radio != NULL) && (pdm != NULL))
  {
    if (!RecorderWriteBlock(radio, pdm))
    {
      RecorderFinish(true, "three-channel file write failed");
      return;
    }
    RecorderReleaseQueues();
    if (stop_requested || (frames_written >= target_frames))
    {
      RecorderFinish(false, "complete");
      return;
    }
  }

  now = HAL_GetTick();
  if ((now - progress_last_ms) >= RECORDER_PROGRESS_PERIOD_MS)
  {
    progress_last_ms = now;
    RecorderSend("RECORD progress=%lu.%01lus queues=%u/%u,%u/%u "
                 "max-write=%lums\r\n",
                 (unsigned long)(frames_written / RECORDER_SAMPLE_RATE_HZ),
                 (unsigned long)(((frames_written % RECORDER_SAMPLE_RATE_HZ) *
                                  10U) / RECORDER_SAMPLE_RATE_HZ),
                 (unsigned int)radio_queue_count, RECORDER_QUEUE_DEPTH,
                 (unsigned int)pdm_queue_count, RECORDER_QUEUE_DEPTH,
                 (unsigned long)max_write_ms);
  }
}

void RadioRecorder_NotifyRadioError(void)
{
  if (recorder_state == RECORDER_ACTIVE)
  {
    radio_error = true;
  }
}

bool RadioRecorder_IsActive(void)
{
  return recorder_state == RECORDER_ACTIVE;
}

void RadioRecorder_Stop(void)
{
  if (recorder_state == RECORDER_ACTIVE)
  {
    RecorderFinish(false, "stopped");
  }
}

void HAL_DFSDM_FilterRegConvHalfCpltCallback(
  DFSDM_Filter_HandleTypeDef *filter)
{
  if ((filter == pdm_filter) && pdm_dma_running)
  {
    RecorderEnqueuePdm(&pdm_dma_buffer[0]);
  }
}

void HAL_DFSDM_FilterRegConvCpltCallback(
  DFSDM_Filter_HandleTypeDef *filter)
{
  if ((filter == pdm_filter) && pdm_dma_running)
  {
    RecorderEnqueuePdm(&pdm_dma_buffer[RECORDER_PDM_BLOCK_SAMPLES]);
  }
}

void HAL_DFSDM_FilterErrorCallback(DFSDM_Filter_HandleTypeDef *filter)
{
  if ((filter == pdm_filter) && pdm_dma_running)
  {
    pdm_error = true;
  }
}
