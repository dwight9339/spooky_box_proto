#include "radio_recorder.h"
#include "audio_path_service.h"
#include "diagnostics.h"
#include "recording_limits.h"
#include "session_control.h"

#include "ff.h"
#include "recording_result.h"
#include "reply_queue.h"
#include "sd_diskio.h"
#include "sd_media.h"
#include "storage_margin.h"
#include "storage_service.h"
#include "usb_test.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define RECORDER_SAMPLE_RATE_HZ RECORDING_LIMIT_SAMPLE_RATE_HZ
#define RECORDER_CHANNELS RECORDING_LIMIT_CHANNELS
#define RECORDER_BLOCK_ALIGN RECORDING_LIMIT_BLOCK_ALIGN
#define RECORDER_WAV_HEADER_BYTES RECORDING_LIMIT_WAV_HEADER_BYTES
#define RECORDER_BLOCK_FRAMES RECORDING_LIMIT_BLOCK_FRAMES
#define RECORDER_RADIO_BLOCK_SAMPLES \
  (RECORDER_BLOCK_FRAMES * 2U)
#define RECORDER_PDM_BLOCK_SAMPLES \
  RECORDER_BLOCK_FRAMES
#define RECORDER_OUTPUT_SAMPLES \
  (RECORDER_BLOCK_FRAMES * RECORDER_CHANNELS)
#define RECORDER_OUTPUT_BYTES \
  (RECORDER_OUTPUT_SAMPLES * sizeof(int16_t))
#define RECORDER_QUEUE_DEPTH                  8U
#define RECORDER_MAX_SECONDS               3600U
#define RECORDER_RUNWAY_LOOKAHEAD_SECONDS    30U
#define RECORDER_FILENAME_LIMIT            1000U
#define RECORDER_PROGRESS_PERIOD_MS        5000U
#define RECORDER_WRITE_HIST_BIN_MS           10U
#define RECORDER_WRITE_HIST_BINS              8U
#define RECORDER_PDM_DC_POLE_Q15           32640
#define RECORDER_PDM_DC_SCALE              32768

/* Decision 0008: retain one minute of three-channel audio. hpq.2 will set a
 * non-zero rolling-capture contribution when it chooses the window. Finalizing
 * rewrites the already allocated header and truncates, so it needs no data bytes. */
#ifndef SPOOKY_RECORDING_CARD_RESERVE_SECONDS
#define SPOOKY_RECORDING_CARD_RESERVE_SECONDS 60U
#endif
#ifndef SPOOKY_ROLLING_CAPTURE_RESERVE_BYTES
#define SPOOKY_ROLLING_CAPTURE_RESERVE_BYTES 0U
#endif
#ifndef SPOOKY_RECORDING_FINALIZE_RESERVE_BYTES
#define SPOOKY_RECORDING_FINALIZE_RESERVE_BYTES 0U
#endif
/* jjy.9: audio preallocated contiguously before capture; a longer recording
 * grows past it only into free space verified ahead of time (jjy.17). */
#ifndef SPOOKY_RECORDING_PREALLOC_SECONDS
#define SPOOKY_RECORDING_PREALLOC_SECONDS 60U
#endif
/* jjy.17: longest used FAT stretch a growing recording may cross. FatFs reads it
 * inside one f_write, about 1.5 ms per FAT sector, so 16 sectors (2048 clusters,
 * 64 MiB at 32 KiB) add about 25 ms to that write. Configurable until the bench
 * fixes it. */
#ifndef SPOOKY_RECORDING_MAX_GAP_FAT_SECTORS
#define SPOOKY_RECORDING_MAX_GAP_FAT_SECTORS 16U
#endif
/* jjy.9: foreground time one preparation step may spend on filename probes or
 * FAT reads (at least one each pass). Below the 70 ms recorder budget and the
 * 50 ms loop-stall threshold; configurable until bench trials fix it. */
#ifndef SPOOKY_RECORD_PREPARE_STEP_MS
#define SPOOKY_RECORD_PREPARE_STEP_MS 32U
#endif
/* Decision 0012 item 4: frames from the microphone DMA start to its first
 * sample (C). Measured by the item 9 loopback qualification; 0 until then. */
#ifndef SPOOKY_RECORD_MIC_LATENCY_FRAMES
#define SPOOKY_RECORD_MIC_LATENCY_FRAMES 0U
#endif
/* Decision 0014 item 5: half the eight-block (683 ms) queue headroom, until the
 * media survey fixes the thresholds. */
#ifndef SPOOKY_STORAGE_MARGIN_QUEUE_BLOCKS
#define SPOOKY_STORAGE_MARGIN_QUEUE_BLOCKS 4U
#endif
#ifndef SPOOKY_STORAGE_MARGIN_WRITE_MS
#define SPOOKY_STORAGE_MARGIN_WRITE_MS 341U
#endif

typedef enum
{
  RECORDER_IDLE = 0,
  RECORDER_PREPARING, /* Card mounted; file being named, created, allocated. */
  RECORDER_ACTIVE
} RecorderState;

/* Preparation runs in bounded foreground steps before capture (jjy.9). */
typedef enum
{
  PREPARE_NAME = 0,
  PREPARE_CREATE,
  PREPARE_SEARCH,
  PREPARE_ALLOCATE,
  PREPARE_REPORT /* Outcome known; waiting for the event queue to take it. */
} PreparePhase;

typedef enum
{
  PREPARE_CONTINUE = 0,
  PREPARE_DONE,
  PREPARE_FAILED
} PrepareStep;

static DFSDM_Filter_HandleTypeDef *pdm_filter;
static FIL recorder_file;
static RecorderState recorder_state;
static bool recorder_file_open;
static bool pdm_dma_running;
static volatile bool capture_enabled;
/* Radio samples still to drop before the first kept frame (decision 0012). */
static volatile uint32_t radio_skip_samples;
static AudioPathCaptureStart capture_start;
static bool capture_start_valid;
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

static uint32_t frames_written;
static uint32_t recording_start_ms;
static uint32_t progress_last_ms;
static uint32_t max_write_ms;
static uint32_t max_convert_ms;
static bool output_pending; /* output_block holds a converted, unwritten block. */
static uint32_t write_count;
/* f_write durations in 10 ms bins; the last bin holds 70 ms and above. */
static uint32_t write_hist[RECORDER_WRITE_HIST_BINS];
static uint32_t radio_left_peak;
static uint32_t radio_right_peak;
static uint32_t pdm_peak;
static int32_t pdm_dc_previous_input;
static int32_t pdm_dc_previous_output;
static uint32_t pending_seconds;
static RecordingLimits recording_limits;
static const char *finish_reason;
static bool finish_aborted;
static bool session_event_pending;
/* Replies the CDC port did not accept; see reply_queue.h for the policy. */
static ReplyQueue recorder_usb_queue;
/* 8lw.20: lines not sent because no host had the port configured. They are
 * not faults; the outcome stays available through RECORD RESULT. */
static uint32_t recorder_usb_detached;
static RecordingResult last_result;
static StorageMargin storage_margin;
static PreparePhase prepare_phase;
static unsigned int prepare_name_index;
static uint32_t prepare_bytes;
static uint32_t prepare_started_ms;
static uint32_t prepare_open_ms;
static uint32_t prepare_phase_ms[PREPARE_REPORT];
static bool prepare_ok;
static uint32_t prepare_step_max_ms;
static uint32_t prepare_steps;
static StorageRunReport prepare_run;
/* jjy.17: free space verified ahead of the file's allocation. */
static bool runway_active;
static bool runway_failed;
static StorageRunwayReport runway;
static const StorageMarginLimits storage_margin_limits = {
  SPOOKY_STORAGE_MARGIN_QUEUE_BLOCKS, SPOOKY_STORAGE_MARGIN_WRITE_MS
};

static void RecorderFlushUsb(void)
{
  const char *line;

  if (!UsbTest_HostAttached())
  {
    /* Lines queued for a host that has gone would arrive stale. */
    recorder_usb_detached += ReplyQueue_Clear(&recorder_usb_queue);
    return;
  }
  line = ReplyQueue_Peek(&recorder_usb_queue);
  if ((line != NULL) && UsbTest_SendText(line))
  {
    ReplyQueue_Pop(&recorder_usb_queue);
  }
}

static void RecorderSendKind(ReplyKind kind, const char *format, va_list args)
{
  char message[REPLY_QUEUE_LINE_BYTES];
  int length = vsnprintf(message, sizeof(message), format, args);

  if (length <= 0)
  {
    return;
  }
  message[sizeof(message) - 1U] = '\0';

  if (!UsbTest_HostAttached())
  {
    ++recorder_usb_detached;
  }
  else if ((ReplyQueue_Peek(&recorder_usb_queue) != NULL) || !UsbTest_SendText(message))
  {
    if (ReplyQueue_Push(&recorder_usb_queue, kind, message) ==
        REPLY_PUSH_EVICTED_REPLY)
    {
      Diagnostics_Record(DIAG_USB_BACKPRESSURE,
                         recorder_usb_queue.last_lost_length, REPLY_QUEUE_DEPTH);
    }
  }
  printf("[record] %s", message);
}

static void RecorderSend(const char *format, ...)
{
  va_list args;

  va_start(args, format);
  RecorderSendKind(REPLY_KIND_KEEP, format, args);
  va_end(args);
}

/* Periodic progress: a newer line supersedes one the port has not taken. */
static void RecorderSendProgress(const char *format, ...)
{
  va_list args;

  va_start(args, format);
  RecorderSendKind(REPLY_KIND_PROGRESS, format, args);
  va_end(args);
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
  if (StorageService_Owner() == STORAGE_OWNER_RECORDER)
  {
    (void)StorageService_Release(STORAGE_OWNER_RECORDER);
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

/* Mounts and checks the card and begins preparation; the rest runs in
 * RecorderPrepareStep. */
bool RadioRecorder_OpenFile(uint32_t requested_seconds)
{
  FRESULT result;
  HAL_SD_CardInfoTypeDef card;
  uint64_t free_bytes;
  uint64_t recordable;
  uint32_t prealloc_seconds = requested_seconds;

  prepare_started_ms = HAL_GetTick();
  if (!StorageService_CardPresent())
  {
    RecorderSend("ERR RECORD no SD card detected\r\n");
    return false;
  }

  result = StorageService_Acquire(STORAGE_OWNER_RECORDER);
  if (result != FR_OK)
  {
    char detail[96];

    RecorderSend("ERR RECORD mount failed result=%s(%u) %s\r\n",
                 StorageService_ResultName(result), (unsigned int)result,
                 SdDiskIo_InitDetail(detail, sizeof(detail)));
    return false;
  }
  /* Decision 0014: only SDHC/SDXC cards record. */
  if (!StorageService_GetCardInfo(STORAGE_OWNER_RECORDER, &card))
  {
    RecorderSend("ERR RECORD card information query failed hal=0x%08lX\r\n",
                 (unsigned long)StorageService_HalError());
    RecorderUnmount();
    return false;
  }
  if (!SdMedia_RecordingSupported(card.CardType))
  {
    RecorderSend("ERR RECORD unsupported card TYPE=%s; SDHC/SDXC required\r\n",
                 SdMedia_CardTypeName(card.CardType));
    RecorderUnmount();
    return false;
  }
  if (!StorageService_GetFreeBytes(STORAGE_OWNER_RECORDER, &free_bytes))
  {
    RecorderSend("ERR RECORD free-space query failed\r\n");
    RecorderUnmount();
    return false;
  }
  if (!RecordingLimits_Init(&recording_limits, requested_seconds, free_bytes,
      SPOOKY_RECORDING_CARD_RESERVE_SECONDS,
      SPOOKY_ROLLING_CAPTURE_RESERVE_BYTES,
      SPOOKY_RECORDING_FINALIZE_RESERVE_BYTES))
  {
    RecorderSend("ERR RECORD card full; recording reserve unavailable\r\n");
    RecorderUnmount();
    return false;
  }

  /* Preallocate the first minute, or the whole of a shorter timed recording,
   * but never into the card reserve: the recording stops at the reserve. */
  if ((prealloc_seconds == 0U) ||
      (prealloc_seconds > SPOOKY_RECORDING_PREALLOC_SECONDS))
  {
    prealloc_seconds = SPOOKY_RECORDING_PREALLOC_SECONDS;
  }
  /* A timed recording ends after the block that reaches its target. */
  prepare_bytes = (uint32_t)RecordingLimits_FileBytes(
    ((prealloc_seconds * RECORDER_SAMPLE_RATE_HZ + RECORDER_BLOCK_FRAMES - 1U) /
     RECORDER_BLOCK_FRAMES) * RECORDER_BLOCK_FRAMES);
  recordable = recording_limits.free_bytes_at_open - recording_limits.reserve_bytes;
  if (recordable < prepare_bytes)
  {
    prepare_bytes = (uint32_t)recordable;
  }

  recorder_filename[0] = '\0';
  prepare_phase = PREPARE_NAME;
  prepare_name_index = 0U;
  (void)memset(prepare_phase_ms, 0, sizeof(prepare_phase_ms));
  (void)memset(&prepare_run, 0, sizeof(prepare_run));
  prepare_step_max_ms = 0U;
  prepare_steps = 0U;
  prepare_open_ms = HAL_GetTick() - prepare_started_ms;
  session_event_pending = false;
  recorder_state = RECORDER_PREPARING;
  return true;
}

/* Bytes the whole recording needs, or UINT64_MAX when open-ended. */
static uint64_t RecorderPlannedBytes(void)
{
  if (recording_limits.target_frames == 0U)
  {
    return UINT64_MAX;
  }
  return RecordingLimits_FileBytes(
    ((recording_limits.target_frames + RECORDER_BLOCK_FRAMES - 1U) /
     RECORDER_BLOCK_FRAMES) * RECORDER_BLOCK_FRAMES);
}

/* A recording longer than its preallocation may grow only into free space the
 * runway has verified, so FatFs never searches far inside f_write (jjy.17). */
static void RecorderBeginRunway(void)
{
  StorageRunState state;

  runway_active = false;
  runway_failed = false;
  (void)memset(&runway, 0, sizeof(runway));
  RecordingLimits_SetAllocation(&recording_limits, 0U);
  if (RecorderPlannedBytes() <= prepare_bytes)
  {
    return; /* Fully preallocated. */
  }
  state = StorageService_BeginRunway(STORAGE_OWNER_RECORDER,
                                     (uint32_t)recorder_file.obj.sclust,
                                     prepare_bytes,
                                     SPOOKY_RECORDING_MAX_GAP_FAT_SECTORS, &runway);
  if (state == STORAGE_RUN_UNSUPPORTED)
  {
    return; /* FAT12/16: FatFs's search is at most 256 FAT sectors. */
  }
  if (state != STORAGE_RUN_SEARCHING)
  {
    runway_failed = true;
    RecordingLimits_SetAllocation(&recording_limits, prepare_bytes);
    return;
  }
  runway_active = true;
  RecordingLimits_SetAllocation(&recording_limits,
    (uint64_t)runway.file_clusters * runway.cluster_bytes);
}

/* Reads one FAT sector ahead when the verified allocation is less than the
 * lookahead past the write position. One sector covers 128 clusters, 14.5 s of
 * audio at 32 KiB, so this runs rarely and takes about 1 ms. */
static void RecorderRunwayStep(void)
{
  uint64_t wanted;
  const uint64_t planned = RecorderPlannedBytes();

  if (!runway_active || runway.barrier)
  {
    return;
  }
  wanted = RecordingLimits_FileBytes(frames_written) +
    ((uint64_t)RECORDER_RUNWAY_LOOKAHEAD_SECONDS * RECORDER_SAMPLE_RATE_HZ *
     RECORDER_BLOCK_ALIGN);
  if (wanted > planned)
  {
    wanted = planned;
  }
  if (recording_limits.allocation_bytes >= wanted)
  {
    return;
  }
  if (!StorageService_StepRunway(STORAGE_OWNER_RECORDER, &runway))
  {
    runway_active = false;
    runway_failed = true; /* The allowance stops growing; the recording ends at it. */
    return;
  }
  RecordingLimits_SetAllocation(&recording_limits,
    (uint64_t)(runway.file_clusters + runway.free_clusters) * runway.cluster_bytes);
}

static const char *RecorderRunwayName(void)
{
  if (runway_failed) return "failed";
  if (!runway_active) return "none";
  return runway.barrier ? "end" : "open";
}

static uint32_t RecorderClustersKib(uint32_t clusters)
{
  return (uint32_t)(((uint64_t)clusters * prepare_run.cluster_bytes) / 1024U);
}

/* Probes REC###.WAV names until one is free. Each probe scans the directory. */
static PrepareStep RecorderPrepareName(uint32_t step_start)
{
  FILINFO info;
  FRESULT result;

  while (prepare_name_index < RECORDER_FILENAME_LIMIT)
  {
    (void)snprintf(recorder_filename, sizeof(recorder_filename),
                   "REC%03u.WAV", prepare_name_index);
    result = f_stat(recorder_filename, &info);
    if (result == FR_NO_FILE)
    {
      prepare_phase = PREPARE_CREATE;
      return PREPARE_CONTINUE;
    }
    if (result != FR_OK)
    {
      RecorderSend("ERR RECORD filename scan failed result=%s(%u)\r\n",
                   StorageService_ResultName(result), (unsigned int)result);
      return PREPARE_FAILED;
    }
    ++prepare_name_index;
    if ((HAL_GetTick() - step_start) >= SPOOKY_RECORD_PREPARE_STEP_MS)
    {
      return PREPARE_CONTINUE;
    }
  }
  RecorderSend("ERR RECORD no free REC###.WAV filename\r\n");
  return PREPARE_FAILED;
}

static PrepareStep RecorderPrepareCreate(void)
{
  FRESULT result = f_open(&recorder_file, recorder_filename,
                          FA_CREATE_NEW | FA_WRITE);

  if (result != FR_OK)
  {
    RecorderSend("ERR RECORD create failed result=%s(%u)\r\n",
                 StorageService_ResultName(result), (unsigned int)result);
    return PREPARE_FAILED;
  }
  recorder_file_open = true;
  switch (StorageService_BeginRunSearch(STORAGE_OWNER_RECORDER, prepare_bytes))
  {
    case STORAGE_RUN_SEARCHING:
      prepare_phase = PREPARE_SEARCH;
      return PREPARE_CONTINUE;
    case STORAGE_RUN_UNSUPPORTED:
      /* FAT12/16: the FAT is at most 256 sectors, so f_expand's own search
       * stays short. */
      prepare_phase = PREPARE_ALLOCATE;
      return PREPARE_CONTINUE;
    case STORAGE_RUN_NOT_FOUND:
      RecorderSend("ERR RECORD free space below the preallocation need-kib=%lu\r\n",
                   (unsigned long)(prepare_bytes / 1024U));
      return PREPARE_FAILED;
    default:
      RecorderSend("ERR RECORD preallocation search failed\r\n");
      return PREPARE_FAILED;
  }
}

/* A file that grows cluster by cluster makes FatFs search the FAT inside
 * f_write; on fragmented free space that stalled a write for 1.4 s and overran
 * the queues. Without a contiguous run the recording is refused (jjy.9). */
static PrepareStep RecorderPrepareSearch(void)
{
  switch (StorageService_StepRunSearch(STORAGE_OWNER_RECORDER,
                                       SPOOKY_RECORD_PREPARE_STEP_MS,
                                       &prepare_run))
  {
    case STORAGE_RUN_SEARCHING:
      return PREPARE_CONTINUE;
    case STORAGE_RUN_FOUND:
      prepare_phase = PREPARE_ALLOCATE;
      return PREPARE_CONTINUE;
    case STORAGE_RUN_NOT_FOUND:
      RecorderSend("ERR RECORD free space too fragmented largest-run-kib=%lu "
                   "need-kib=%lu fat-sectors=%lu\r\n",
                   (unsigned long)RecorderClustersKib(prepare_run.longest_clusters),
                   (unsigned long)RecorderClustersKib(prepare_run.needed_clusters),
                   (unsigned long)prepare_run.fat_sectors);
      return PREPARE_FAILED;
    default:
      RecorderSend("ERR RECORD FAT read failed during preallocation search\r\n");
      return PREPARE_FAILED;
  }
}

static PrepareStep RecorderPrepareAllocate(void)
{
  FRESULT result = f_expand(&recorder_file, prepare_bytes, 1U);

  if (result == FR_DENIED)
  {
    RecorderSend("ERR RECORD no contiguous free space need-kib=%lu\r\n",
                 (unsigned long)(prepare_bytes / 1024U));
    return PREPARE_FAILED;
  }
  if (result != FR_OK)
  {
    RecorderSend("ERR RECORD preallocation failed result=%s(%u)\r\n",
                 StorageService_ResultName(result), (unsigned int)result);
    return PREPARE_FAILED;
  }
  if (!RecorderWriteHeader(0U))
  {
    RecorderSend("ERR RECORD initial WAV header write failed\r\n");
    return PREPARE_FAILED;
  }
  RecorderBeginRunway();
  return PREPARE_DONE;
}

/* One bounded preparation step per foreground pass. The outcome goes to the
 * Session machine, which starts capture or discards the file. */
static void RecorderPrepareStep(void)
{
  const uint32_t step_start = HAL_GetTick();
  const PreparePhase phase = prepare_phase;
  PrepareStep step;
  uint32_t step_ms;

  if (session_event_pending)
  {
    return;
  }
  if (phase == PREPARE_REPORT)
  {
    session_event_pending = SessionControl_ReportPrepared(prepare_ok);
    return;
  }
  if (!StorageService_CardPresent())
  {
    RecorderSend("ERR RECORD SD card removed while preparing\r\n");
    step = PREPARE_FAILED;
  }
  else
  {
    switch (phase)
    {
      case PREPARE_NAME:
        step = RecorderPrepareName(step_start);
        break;
      case PREPARE_CREATE:
        step = RecorderPrepareCreate();
        break;
      case PREPARE_SEARCH:
        step = RecorderPrepareSearch();
        break;
      case PREPARE_ALLOCATE:
      default:
        step = RecorderPrepareAllocate();
        break;
    }
  }
  step_ms = HAL_GetTick() - step_start;
  ++prepare_steps;
  prepare_phase_ms[phase] += step_ms;
  if (step_ms > prepare_step_max_ms)
  {
    prepare_step_max_ms = step_ms;
  }
  if (step == PREPARE_CONTINUE)
  {
    return;
  }
  if (step == PREPARE_DONE)
  {
    RecorderSend("OK RECORD PREPARED file=%s prealloc-kib=%lu open=%lums "
                 "name=%lums create=%lums search=%lums fat-sectors=%lu "
                 "allocate=%lums steps=%lu step-max=%lums elapsed=%lums\r\n",
                 recorder_filename, (unsigned long)(prepare_bytes / 1024U),
                 (unsigned long)prepare_open_ms,
                 (unsigned long)prepare_phase_ms[PREPARE_NAME],
                 (unsigned long)prepare_phase_ms[PREPARE_CREATE],
                 (unsigned long)prepare_phase_ms[PREPARE_SEARCH],
                 (unsigned long)prepare_run.fat_sectors,
                 (unsigned long)prepare_phase_ms[PREPARE_ALLOCATE],
                 (unsigned long)prepare_steps,
                 (unsigned long)prepare_step_max_ms,
                 (unsigned long)(HAL_GetTick() - prepare_started_ms));
  }
  prepare_ok = step == PREPARE_DONE;
  prepare_phase = PREPARE_REPORT;
  session_event_pending = SessionControl_ReportPrepared(prepare_ok);
}

void RadioRecorder_DiscardFile(void)
{
  RecorderDiscardFile();
  recorder_filename[0] = '\0'; /* The file no longer exists. */
  recorder_state = RECORDER_IDLE;
  session_event_pending = false;
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
    if (!pdm_overrun)
      Diagnostics_Record(DIAG_PDM_OVERRUN, pdm_queue_count, radio_queue_count);
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
  if (radio_skip_samples != 0U)
  {
    /* Radio frames before the capture origin are not part of the recording. */
    copied = (radio_skip_samples < sample_count) ? radio_skip_samples
                                                 : sample_count;
    radio_skip_samples -= copied;
  }

  while (copied < sample_count)
  {
    uint32_t available;
    uint32_t copy_count;
    uint8_t tail;

    if (radio_queue_count >= RECORDER_QUEUE_DEPTH)
    {
      if (!radio_overrun)
        Diagnostics_Record(DIAG_RADIO_OVERRUN, radio_queue_count, pdm_queue_count);
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

/* Conversion and the FatFs write run in separate foreground passes, so a slow
 * card write does not also carry the conversion time against the budget. */
static void RecorderConvertBlock(const int16_t *radio, const int32_t *pdm)
{
  uint32_t convert_start = HAL_GetTick();
  uint32_t convert_ms;

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

  convert_ms = HAL_GetTick() - convert_start;
  if (convert_ms > max_convert_ms)
  {
    max_convert_ms = convert_ms;
  }
}

/* Decision 0014 item 5: latch a storage-margin warning once a queue or a write
 * passes its threshold. The queues fill in interrupts during a slow write, so
 * the high-water mark is read after each write. */
static void RecorderCheckMargin(uint32_t write_ms)
{
  uint32_t high_water = radio_queue_high_water;

  if (pdm_queue_high_water > high_water)
  {
    high_water = pdm_queue_high_water;
  }
  if (StorageMargin_Update(&storage_margin, &storage_margin_limits, high_water,
                           write_ms))
  {
    Diagnostics_Record(DIAG_STORAGE_MARGIN, high_water, write_ms);
  }
}

static bool RecorderWriteBlock(void)
{
  UINT written = 0U;
  FRESULT result;
  uint32_t write_start = HAL_GetTick();
  uint32_t write_ms;
  uint32_t bin;

  result = f_write(&recorder_file, output_block,
                   RECORDER_OUTPUT_BYTES, &written);
  write_ms = HAL_GetTick() - write_start;
  Diagnostics_Record(DIAG_SD_WRITE, write_ms,
    (uint32_t)radio_queue_count | ((uint32_t)pdm_queue_count << 16));
  if (write_ms > max_write_ms)
  {
    max_write_ms = write_ms;
  }
  bin = write_ms / RECORDER_WRITE_HIST_BIN_MS;
  if (bin >= RECORDER_WRITE_HIST_BINS)
  {
    bin = RECORDER_WRITE_HIST_BINS - 1U;
  }
  ++write_hist[bin];
  ++write_count;
  RecorderCheckMargin(write_ms);
  if ((result != FR_OK) || (written != RECORDER_OUTPUT_BYTES))
  {
    Diagnostics_Record(DIAG_SD_ERROR, (uint32_t)result, written);
    RecorderSend("ERR RECORD write failed result=%s(%u) bytes=%u/%u\r\n",
                 StorageService_ResultName(result), (unsigned int)result,
                 (unsigned int)written,
                 (unsigned int)RECORDER_OUTPUT_BYTES);
    return false;
  }
  frames_written += RECORDER_BLOCK_FRAMES;
  return true;
}

void RadioRecorder_StopCapture(void)
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
               "max-write=%lums peaks=%lu,%lu,%lu margin=%s\r\n",
               (unsigned int)radio_queue_high_water,
               RECORDER_QUEUE_DEPTH,
               (unsigned int)pdm_queue_high_water,
               RECORDER_QUEUE_DEPTH,
               (unsigned long)max_write_ms,
               (unsigned long)radio_left_peak,
               (unsigned long)radio_right_peak,
               (unsigned long)pdm_peak,
               storage_margin.low ? "LOW" : "OK");
}

/* Returns whether the file was finalized. */
static bool RecorderFinish(bool aborted, const char *reason)
{
  bool finalized;
  uint32_t elapsed_ms;
  uint32_t data_bytes = frames_written * RECORDER_BLOCK_ALIGN;

  RadioRecorder_StopCapture();
  finalized = RecorderFinalizeFile();
  recorder_state = RECORDER_IDLE;
  stop_requested = false;
  elapsed_ms = HAL_GetTick() - recording_start_ms;
  Diagnostics_Record(DIAG_RECORD_END, frames_written,
    ((aborted || !finalized) ? 1U : 0U) | (finalized ? 2U : 0U));
  /* Kept before the reply is pushed, so RECORD RESULT never trails it. */
  RecordingResult_Record(&last_result, aborted, finalized, recorder_filename,
                         frames_written, data_bytes, elapsed_ms, reason);

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
                 "audio=%lu.%03lus elapsed=%lums reason=%s\r\n",
                 recorder_filename, (unsigned long)frames_written,
                 (unsigned long)data_bytes,
                 (unsigned long)(frames_written / RECORDER_SAMPLE_RATE_HZ),
                 (unsigned long)(((frames_written % RECORDER_SAMPLE_RATE_HZ) *
                                  1000U) / RECORDER_SAMPLE_RATE_HZ),
                 (unsigned long)elapsed_ms, reason);
  }
  RecorderReportDiagnostics();
  return finalized;
}

bool RadioRecorder_CanStart(uint32_t seconds, bool radio_ready)
{
  if (recorder_state != RECORDER_IDLE)
  {
    RecorderSend("ERR RECORD already active\r\n");
    return false;
  }
  if (seconds > RECORDER_MAX_SECONDS)
  {
    RecorderSend("ERR RECORD duration must be 1..3600 seconds when supplied\r\n");
    return false;
  }
  if (StorageService_Owner() == STORAGE_OWNER_SD_STRESS)
  {
    RecorderSend("ERR RECORD unavailable while SD test active\r\n");
    return false;
  }
  if (StorageService_Owner() != STORAGE_OWNER_NONE)
  {
    RecorderSend("ERR RECORD storage busy owner=%s\r\n",
                 StorageOwner_Name(StorageService_Owner()));
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
  if (!StorageService_CardPresent())
  {
    RecorderSend("ERR RECORD no SD card detected\r\n");
    return false;
  }
  pending_seconds = seconds;
  return true;
}

bool RadioRecorder_StartCapture(void)
{
  uint32_t primask;

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
  max_write_ms = 0U;
  StorageMargin_Reset(&storage_margin);
  max_convert_ms = 0U;
  output_pending = false;
  write_count = 0U;
  (void)memset(write_hist, 0, sizeof(write_hist));
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
  finish_reason = "complete";
  finish_aborted = false;
  session_event_pending = false;

  RecorderPrepareDmaBuffer(pdm_dma_buffer, sizeof(pdm_dma_buffer));
  recorder_state = RECORDER_ACTIVE;
  /* Decision 0012 item 4: the radio position snapshot, the radio capture
   * enable and the microphone DMA start happen together with interrupts
   * masked, so the delay between them is constant and folded into C. The
   * section lasts a few microseconds, far below one 10.7 ms half period. */
  capture_start_valid = false;
  primask = __get_PRIMASK();
  __disable_irq();
  if (!AudioPath_AlignCaptureLocked(SPOOKY_RECORD_MIC_LATENCY_FRAMES,
                                    &capture_start))
  {
    __set_PRIMASK(primask);
    finish_aborted = true;
    finish_reason = "radio timeline unavailable";
    return false;
  }
  radio_skip_samples = capture_start.skip_frames * 2U; /* stereo */
  capture_enabled = true;
  if (HAL_DFSDM_FilterRegularStart_DMA(
        pdm_filter, pdm_dma_buffer,
        2U * RECORDER_PDM_BLOCK_SAMPLES) != HAL_OK)
  {
    __set_PRIMASK(primask);
    finish_aborted = true;
    finish_reason = "PDM DMA did not start";
    return false;
  }
  __set_PRIMASK(primask);
  capture_start_valid = true;
  pdm_dma_running = true;
  recording_start_ms = HAL_GetTick();
  progress_last_ms = recording_start_ms;
  Diagnostics_Record(DIAG_RECORD_START, pending_seconds, RECORDER_SAMPLE_RATE_HZ);
  if (pending_seconds == 0U)
  {
    RecorderSend("OK RECORD START file=%s duration=open "
                 "format=48000Hz/16-bit/3ch [radio-L,radio-R,mic]\r\n",
                 recorder_filename);
  }
  else
  {
    RecorderSend("OK RECORD START file=%s duration=%lus "
                 "format=48000Hz/16-bit/3ch [radio-L,radio-R,mic]\r\n",
                 recorder_filename, (unsigned long)pending_seconds);
  }
  return true;
}

void RadioRecorder_RequestStop(void)
{
  stop_requested = true;
}

bool RadioRecorder_TargetReached(void)
{
  if (stop_requested)
  {
    finish_reason = "stopped";
    return true;
  }
  if (RecordingLimits_DurationReached(&recording_limits, frames_written))
  {
    finish_reason = "duration complete";
    return true;
  }
  session_event_pending = false;
  return false;
}

bool RadioRecorder_FinalizeFile(void)
{
  const bool finalized = RecorderFinish(finish_aborted, finish_reason);
  finish_aborted = false;
  finish_reason = "complete";
  session_event_pending = false;
  return finalized;
}

void RadioRecorder_PublishSessionEvent(SesPublished event)
{
  switch (event)
  {
    case SES_PUB_RECORDING_STOPPING:
      RecorderSend("OK RECORD STOP requested; finalizing next matched block\r\n");
      break;
    case SES_PUB_STOP_IGNORED:
      RecorderSend("OK RECORD already idle\r\n");
      break;
    case SES_PUB_RECORDING_PREPARING:
      RecorderSend("OK RECORD PREPARING prealloc-kib=%lu\r\n",
                   (unsigned long)(prepare_bytes / 1024U));
      break;
    case SES_PUB_RECORDING_CANCELLED:
      RecorderSend("OK RECORD STOP cancelled before capture; no file kept\r\n");
      break;
    case SES_PUB_RECORDING_STARTED:
    case SES_PUB_RECORDING_REJECTED:
    case SES_PUB_RECORDING_COMPLETED:
    case SES_PUB_RECORDING_ABORTED:
    default:
      break;
  }
}

void RadioRecorder_SendStorageStatus(void)
{
  const uint64_t free_bytes = RecordingLimits_FreeBytes(&recording_limits,
                                                        frames_written);
  RecorderSend("OK SD STATUS CARD=%s OWNER=%s FILE=%s FRAMES=%lu "
               "FREE_MIB=%lu RESERVE_MIB=%lu\r\n",
               StorageService_CardPresent() ? "PRESENT" : "ABSENT",
               StorageOwner_Name(StorageService_Owner()),
               (recorder_filename[0] != '\0') ? recorder_filename : "none",
               (unsigned long)frames_written,
               (unsigned long)(free_bytes / (1024U * 1024U)),
               (unsigned long)(recording_limits.reserve_bytes / (1024U * 1024U)));
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
  pdm_dma_running = false;
  capture_enabled = false;
  pending_seconds = 0U;
  (void)memset(&recording_limits, 0, sizeof(recording_limits));
  finish_reason = "complete";
  finish_aborted = false;
  session_event_pending = false;
  recorder_filename[0] = '\0';
  ReplyQueue_Init(&recorder_usb_queue);
  RecordingResult_Init(&last_result);
  printf("[record] three-channel recorder ready; default=open\r\n");
}

bool RadioRecorder_HandleCommand(const char *command, bool radio_ready)
{
  uint32_t seconds = 0U;

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
    if (recorder_state == RECORDER_PREPARING)
    {
      RecorderSend("OK RECORD PREPARING file=%s fat-sectors=%lu elapsed=%lums\r\n",
                   (recorder_filename[0] != '\0') ? recorder_filename : "none",
                   (unsigned long)prepare_run.fat_sectors,
                   (unsigned long)(HAL_GetTick() - prepare_started_ms));
    }
    else if (recorder_state == RECORDER_ACTIVE)
    {
      RecorderSend("OK RECORD ACTIVE file=%s audio=%lu.%01lus "
                   "queues=%u/%u,%u/%u max-write=%lums margin=%s\r\n",
                   recorder_filename,
                   (unsigned long)(frames_written / RECORDER_SAMPLE_RATE_HZ),
                   (unsigned long)(((frames_written % RECORDER_SAMPLE_RATE_HZ) *
                                    10U) / RECORDER_SAMPLE_RATE_HZ),
                   (unsigned int)radio_queue_count, RECORDER_QUEUE_DEPTH,
                   (unsigned int)pdm_queue_count, RECORDER_QUEUE_DEPTH,
                   (unsigned long)max_write_ms,
                   storage_margin.low ? "LOW" : "OK");
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
  if (strcmp(command, "RECORD RESULT") == 0)
  {
    char reply[REPLY_QUEUE_LINE_BYTES];

    if (RecordingResult_Format(&last_result, reply, sizeof(reply)) != 0U)
    {
      RecorderSend("%s", reply);
    }
    return true;
  }
  if (strcmp(command, "RECORD TIMELINE") == 0)
  {
    AudioTimelinePosition now;
    char now_text[21];
    char origin_text[21];
    char mic_text[21];
    char phase_text[12];
    const bool have_now = AudioPath_GetPosition(&now);

    FormatFrame(have_now ? now.frame : 0U, now_text);
    if (!capture_start_valid)
    {
      RecorderSend("OK RECORD TIMELINE NONE epoch=%lu now=%s\r\n",
                   have_now ? (unsigned long)now.epoch : 0UL, now_text);
      return true;
    }
    FormatFrame(capture_start.alignment.origin.frame, origin_text);
    FormatFrame(capture_start.mic_start.frame, mic_text);
    if (capture_start.monitor_phase_valid)
    {
      (void)snprintf(phase_text, sizeof(phase_text), "%lu",
                     (unsigned long)capture_start.monitor_phase_frames);
    }
    else
    {
      (void)snprintf(phase_text, sizeof(phase_text), "none");
    }
    RecorderSend("OK RECORD TIMELINE file=%s epoch=%lu origin=%s "
                 "mic-start=%s p=%lu skip=%lu c=%lu tx-rx-phase=%s "
                 "now=%s\r\n",
                 recorder_filename,
                 (unsigned long)capture_start.alignment.origin.epoch,
                 origin_text, mic_text,
                 (unsigned long)(capture_start.mic_start.frame -
                                 capture_start.alignment.first_half_frame),
                 (unsigned long)capture_start.skip_frames,
                 (unsigned long)SPOOKY_RECORD_MIC_LATENCY_FRAMES,
                 phase_text, now_text);
    return true;
  }
  if (strcmp(command, "RECORD LATENCY") == 0)
  {
    RecorderSend("OK RECORD LATENCY writes=%lu write-max=%lums "
                 "convert-max=%lums "
                 "write-hist-10ms=%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu "
                 "usb-superseded=%lu usb-lost=%lu usb-detached=%lu "
                 "runway=%s fat-ahead=%lu gap-max=%lu\r\n",
                 (unsigned long)write_count, (unsigned long)max_write_ms,
                 (unsigned long)max_convert_ms,
                 (unsigned long)write_hist[0], (unsigned long)write_hist[1],
                 (unsigned long)write_hist[2], (unsigned long)write_hist[3],
                 (unsigned long)write_hist[4], (unsigned long)write_hist[5],
                 (unsigned long)write_hist[6], (unsigned long)write_hist[7],
                 (unsigned long)recorder_usb_queue.progress_superseded,
                 (unsigned long)recorder_usb_queue.replies_lost,
                 (unsigned long)recorder_usb_detached,
                 RecorderRunwayName(), (unsigned long)runway.fat_sectors,
                 (unsigned long)runway.longest_gap);
    return true;
  }
  if (strcmp(command, "RECORD STOP") == 0)
  {
    if (!SessionControl_RequestStop())
    {
      RecorderSend("ERR BUSY\r\n");
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
    if ((command[12] != '\0') &&
        ((seconds == 0U) || (seconds > RECORDER_MAX_SECONDS)))
    {
      RecorderSend("ERR RECORD duration must be 1..3600 seconds\r\n");
      return true;
    }
    if (!SessionControl_RequestStart(seconds, radio_ready))
    {
      RecorderSend("ERR BUSY\r\n");
    }
    return true;
  }

  RecorderSend("ERR usage: RECORD STATUS|RESULT|LATENCY|START [seconds]|STOP\r\n");
  return true;
}

void RadioRecorder_Service(void)
{
  int16_t *radio;
  int32_t *pdm;
  uint32_t now;

  RecorderFlushUsb();
  if (recorder_state == RECORDER_PREPARING)
  {
    RecorderPrepareStep();
    return;
  }
  if (recorder_state != RECORDER_ACTIVE)
  {
    return;
  }
  if (session_event_pending)
  {
    return;
  }
  if (!StorageService_CardPresent())
  {
    finish_aborted = true;
    finish_reason = "SD card removed";
    session_event_pending = true;
    SessionControl_ReportCaptureFault();
    return;
  }
  if (radio_error || pdm_error || radio_overrun || pdm_overrun)
  {
    const char *reason = radio_error ? "radio DMA error" :
      pdm_error ? "PDM DMA error" :
      radio_overrun ? "radio queue overrun" : "PDM queue overrun";
    finish_aborted = true;
    finish_reason = reason;
    session_event_pending = true;
    SessionControl_ReportCaptureFault();
    return;
  }
  radio = (radio_queue_count != 0U) ? radio_queue[radio_queue_head] : NULL;
  pdm = (pdm_queue_count != 0U) ? pdm_queue[pdm_queue_head] : NULL;
  if (!output_pending && (radio != NULL) && (pdm != NULL))
  {
    RecorderConvertBlock(radio, pdm);
    RecorderReleaseQueues();
    output_pending = true;
  }
  else if (output_pending)
  {
    RecordingLimitReason limit;

    output_pending = false;
    if (!RecorderWriteBlock())
    {
      finish_aborted = true;
      finish_reason = "three-channel file write failed";
      session_event_pending = true;
      SessionControl_ReportCaptureFault();
      return;
    }
    limit =RecordingLimits_BeforeBlock(&recording_limits, frames_written);
    if ((limit == RECORDING_LIMIT_CARD_FULL) && !stop_requested &&
        !RecordingLimits_DurationReached(&recording_limits, frames_written))
    {
      /* A full card is a fault even when the valid partial file can be finalized. */
      finish_aborted = true;
      finish_reason = "card full";
      session_event_pending = true;
      SessionControl_ReportCardFull();
      return;
    }
    if ((limit == RECORDING_LIMIT_ALLOCATION) && !stop_requested &&
        !RecordingLimits_DurationReached(&recording_limits, frames_written))
    {
      /* jjy.17: growing further would make FatFs search used space inside a
       * block write. A fault, with the valid partial file finalized. */
      finish_aborted = true;
      finish_reason = runway.barrier ? "free space fragmented" :
        runway_failed ? "FAT lookahead failed" : "FAT lookahead behind";
      session_event_pending = true;
      SessionControl_ReportCaptureFault();
      return;
    }
    if ((limit == RECORDING_LIMIT_FILE_SIZE) && !stop_requested &&
        !RecordingLimits_DurationReached(&recording_limits, frames_written))
    {
      finish_aborted = false;
      finish_reason = "WAV size limit";
      session_event_pending = true;
      SessionControl_ReportFileLimit();
      return;
    }
    finish_aborted = false;
    finish_reason = "complete";
    session_event_pending = true;
    SessionControl_ReportBlockWritten();
  }
  else
  {
    RecorderRunwayStep();
  }

  now = HAL_GetTick();
  if ((now - progress_last_ms) >= RECORDER_PROGRESS_PERIOD_MS)
  {
    progress_last_ms = now;
    RecorderSendProgress("RECORD progress=%lu.%01lus queues=%u/%u,%u/%u "
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

/* Preparing counts as active: the card is held for the session. */
bool RadioRecorder_IsActive(void)
{
  return recorder_state != RECORDER_IDLE;
}

bool RadioRecorder_IsCapturing(void)
{
  return capture_enabled;
}

void RadioRecorder_Stop(void)
{
  if (recorder_state == RECORDER_PREPARING)
  {
    RadioRecorder_DiscardFile();
  }
  else if (recorder_state == RECORDER_ACTIVE)
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
    if (!pdm_error)
      Diagnostics_Record(DIAG_AUDIO_ERROR, 3U, filter->ErrorCode);
    pdm_error = true;
  }
}
