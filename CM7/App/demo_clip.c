#include "demo_clip.h"

#include <stdio.h>
#include <string.h>

#include "clip_decimator.h"
#include "clip_player.h"
#include "demo_field.h"
#include "ff.h"
#include "main.h"
#include "recording_limits.h"
#include "storage_service.h"

#define CLIP_HEADER_BYTES RECORDING_LIMIT_WAV_HEADER_BYTES
#define CLIP_FRAME_BYTES (CLIP_SOURCE_CHANNELS * sizeof(int16_t))

_Static_assert(RECORDING_LIMIT_CHANNELS == CLIP_SOURCE_CHANNELS,
               "captures hold the recorder's three channels");
_Static_assert(RECORDING_LIMIT_SAMPLE_RATE_HZ == CLIP_SOURCE_RATE_HZ,
               "captures are at the recorder's rate");

typedef enum
{
  LOAD_IDLE = 0,
  LOAD_OPEN,   /* open the next part's segment and seek to its first frame */
  LOAD_READ    /* read and decimate one chunk of the open part */
} LoadStep;

/* AXI SRAM (decision 0011 item 15). Only the CPU touches it: the foreground
 * writes it while the player is stopped, the radio interrupt reads it. */
static int16_t clip[CLIP_MAX_SAMPLES] __attribute__((section(".dma_buffer"), aligned(32)));
/* Read chunk. The SD driver reads the FIFO by CPU (sd_diskio.c), so DTCM will do. */
static int16_t chunk[CLIP_LOAD_CHUNK_FRAMES * CLIP_SOURCE_CHANNELS];

static ClipPlayer player;
static ClipDecimator decimator;
static ClipRange range;
static LoadStep step;
static FIL file;
static bool file_open;
static uint8_t part;
static uint32_t part_read;   /* frames of the current part already read */
static uint32_t written;     /* clip samples so far */
static uint32_t load_started_ms;
static char capture_name[8]; /* Cnnn */
static DemoClipStatus status;

static uint32_t Now(void)
{
  return HAL_GetTick();
}

static void CloseFile(void)
{
  if (file_open)
  {
    (void)f_close(&file);
    file_open = false;
  }
}

static void Fail(DemoClipFault fault)
{
  ClipPlayer_Stop(&player);
  CloseFile();
  if (status.state != (uint8_t)DEMO_CLIP_LOADING)
  {
    status.capture = 0U; /* no capture was loading: the save never committed */
  }
  step = LOAD_IDLE;
  status.state = (uint8_t)DEMO_CLIP_FAILED;
  status.fault = (uint8_t)fault;
  status.samples = 0U;
  ++status.failures;
  printf("[clip] FAIL: %s (C%03lu)\r\n", DemoClip_FaultName((uint8_t)fault),
         (unsigned long)status.capture);
  DemoField_OnClipOutcome(status.state, status.fault);
}

static void Ready(void)
{
  const uint32_t elapsed = Now() - load_started_ms;

  step = LOAD_IDLE;
  status.state = (uint8_t)DEMO_CLIP_READY;
  status.fault = (uint8_t)DEMO_CLIP_FAULT_NONE;
  status.samples = written;
  status.load_ms = elapsed;
  if (elapsed > status.load_ms_max)
  {
    status.load_ms_max = elapsed;
  }
  ++status.loads;
  printf("[clip] loaded C%03lu: %lu samples at 24 kHz (%lu ms) in %lu ms\r\n",
         (unsigned long)status.capture, (unsigned long)written,
         (unsigned long)((written * 1000U) / CLIP_RATE_HZ), (unsigned long)elapsed);
  DemoField_OnClipOutcome(status.state, status.fault);
}

static bool OpenPart(void)
{
  const ClipRangePart *current = &range.parts[part];
  char path[24];
  FRESULT result;

  (void)snprintf(path, sizeof(path), "CAPS/%sS%02u.WAV", capture_name,
                 (unsigned)current->segment);
  result = f_open(&file, path, FA_READ);
  if (result != FR_OK)
  {
    printf("[clip] open %s result=%s\r\n", path, StorageService_ResultName(result));
    return false;
  }
  file_open = true;
  /* The descriptor's frame count must be in the file. */
  if (f_size(&file) < (CLIP_HEADER_BYTES +
                       ((current->first_frame + current->frames) * CLIP_FRAME_BYTES)))
  {
    printf("[clip] %s holds %lu bytes, short of the descriptor\r\n", path,
           (unsigned long)f_size(&file));
    return false;
  }
  result = f_lseek(&file, CLIP_HEADER_BYTES + (current->first_frame * CLIP_FRAME_BYTES));
  if (result != FR_OK)
  {
    printf("[clip] seek %s result=%s\r\n", path, StorageService_ResultName(result));
    return false;
  }
  part_read = 0U;
  return true;
}

static bool ReadChunk(void)
{
  const ClipRangePart *current = &range.parts[part];
  const uint32_t left = current->frames - part_read;
  const uint32_t frames = (left < CLIP_LOAD_CHUNK_FRAMES) ? left : CLIP_LOAD_CHUNK_FRAMES;
  const UINT bytes = (UINT)(frames * CLIP_FRAME_BYTES);
  UINT read = 0U;
  const FRESULT result = f_read(&file, chunk, bytes, &read);

  if ((result != FR_OK) || (read != bytes))
  {
    printf("[clip] read result=%s bytes=%u/%u\r\n", StorageService_ResultName(result),
           (unsigned)read, (unsigned)bytes);
    return false;
  }
  written += ClipDecimator_Process(&decimator, chunk, frames, &clip[written],
                                   CLIP_MAX_SAMPLES - written);
  part_read += frames;
  return true;
}

/* --- Control-facing ---------------------------------------------------------- */

void DemoClip_Expect(void)
{
  ClipPlayer_Stop(&player);
  CloseFile();
  step = LOAD_IDLE;
  status.state = (uint8_t)DEMO_CLIP_SAVING;
  status.fault = (uint8_t)DEMO_CLIP_FAULT_NONE;
  status.capture = 0U;
  status.samples = 0U;
}

void DemoClip_Fail(DemoClipFault fault)
{
  Fail(fault);
}

void DemoClip_SetPlaying(bool play)
{
  const bool ready = status.state == (uint8_t)DEMO_CLIP_READY;

  if (play && ready && !ClipPlayer_Active(&player))
  {
    (void)ClipPlayer_Start(&player, clip, status.samples);
  }
  else if ((!play || !ready) && ClipPlayer_Active(&player))
  {
    ClipPlayer_Stop(&player);
  }
}

void DemoClip_GetStatus(DemoClipStatus *out)
{
  if (out == NULL)
  {
    return;
  }
  status.playing = ClipPlayer_Active(&player);
  status.loops = player.loops;
  *out = status;
}

const char *DemoClip_StateName(uint8_t state)
{
  static const char *const names[] = {"NONE", "SAVING", "LOADING", "READY", "FAILED"};

  return (state < (sizeof(names) / sizeof(names[0]))) ? names[state] : "?";
}

const char *DemoClip_FaultName(uint8_t fault)
{
  static const char *const names[DEMO_CLIP_FAULT_COUNT] = {
    "NONE", "SAVE_BUSY", "SAVE_UNAVAILABLE", "SAVE_FAILED", "LOAD_FAILED"
  };

  return (fault < DEMO_CLIP_FAULT_COUNT) ? names[fault] : "?";
}

/* --- Rolling-facing ---------------------------------------------------------- */

bool DemoClip_Waiting(void)
{
  return status.state == (uint8_t)DEMO_CLIP_SAVING;
}

void DemoClip_Begin(uint32_t capture, const char *name, const uint32_t *segment_frames,
                    uint32_t count)
{
  if (status.state != (uint8_t)DEMO_CLIP_SAVING)
  {
    return;
  }
  status.capture = capture;
  (void)snprintf(capture_name, sizeof(capture_name), "%s", (name != NULL) ? name : "");
  load_started_ms = Now();
  if (!ClipRange_Select(segment_frames, count, CLIP_SOURCE_FRAMES, &range))
  {
    Fail(DEMO_CLIP_FAULT_LOAD_FAILED);
    return;
  }
  ClipDecimator_Init(&decimator);
  written = 0U;
  part = 0U;
  step = LOAD_OPEN;
  status.state = (uint8_t)DEMO_CLIP_LOADING;
  printf("[clip] loading the last %lu frames of %s from %u segment(s)\r\n",
         (unsigned long)range.frames, capture_name, (unsigned)range.count);
}

bool DemoClip_Loading(void)
{
  return status.state == (uint8_t)DEMO_CLIP_LOADING;
}

void DemoClip_Step(void)
{
  const uint32_t start = Now();
  bool ok = true;

  switch (step)
  {
    case LOAD_OPEN:
      ok = OpenPart();
      step = LOAD_READ;
      break;
    case LOAD_READ:
      ok = ReadChunk();
      if (ok && (part_read >= range.parts[part].frames))
      {
        CloseFile();
        step = (++part < range.count) ? LOAD_OPEN : LOAD_IDLE;
      }
      break;
    case LOAD_IDLE:
    default:
      return;
  }
  if ((Now() - start) > status.step_ms_max)
  {
    status.step_ms_max = Now() - start;
  }
  if (!ok)
  {
    Fail(DEMO_CLIP_FAULT_LOAD_FAILED);
  }
  else if (step == LOAD_IDLE)
  {
    if (written == (range.frames / 2U))
    {
      Ready();
    }
    else
    {
      printf("[clip] decimated %lu samples, expected %lu\r\n", (unsigned long)written,
             (unsigned long)(range.frames / 2U));
      Fail(DEMO_CLIP_FAULT_LOAD_FAILED);
    }
  }
}

void DemoClip_Abort(void)
{
  if (status.state == (uint8_t)DEMO_CLIP_LOADING)
  {
    Fail(DEMO_CLIP_FAULT_LOAD_FAILED);
  }
  else if (status.state == (uint8_t)DEMO_CLIP_SAVING)
  {
    Fail(DEMO_CLIP_FAULT_SAVE_FAILED);
  }
}

/* --- Monitor ------------------------------------------------------------------ */

bool DemoClip_RenderMonitor(uint16_t *stereo, uint32_t frame_count)
{
  return ClipPlayer_Render(&player, stereo, frame_count);
}
