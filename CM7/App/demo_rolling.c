#include "demo_rolling.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "demo_field.h"
#include "ff.h"
#include "main.h"
#include "recording_limits.h"
#include "rolling_catalog.h"
#include "storage_service.h"

#define ROLL_BLOCK_BYTES RECORDING_LIMIT_BLOCK_BYTES
#define ROLL_HEADER_BYTES RECORDING_LIMIT_WAV_HEADER_BYTES
#define ROLL_SEGMENT_DATA_BYTES (ROLLING_SEGMENT_BLOCKS * ROLL_BLOCK_BYTES)
#define ROLL_SEGMENT_FILE_BYTES (ROLL_HEADER_BYTES + ROLL_SEGMENT_DATA_BYTES)
#define ROLL_CAPTURE_LIMIT 999U

_Static_assert(ROLL_BLOCK_BYTES == 24576U, "recorder blocks are 4,096 three-channel frames");

typedef enum
{
  SEGMENT_NONE = 0,
  SEGMENT_OPEN,
  SEGMENT_CLOSE_PENDING,  /* full, or a save boundary: close before the next block */
  SEGMENT_OPEN_PENDING
} SegmentState;

typedef enum
{
  SAVE_IDLE = 0,
  SAVE_REQUESTED,  /* waiting for the next block boundary */
  SAVE_NAME,       /* pick an unused capture number */
  SAVE_RENAME,
  SAVE_DESCRIPTOR
} SaveState;

static RollingCatalog catalog;
static FIL segment;
static bool segment_open;
static SegmentState segment_state;
static uint32_t rotate_started_ms;
static bool rotating;
static SaveState save_state;
static uint8_t save_index;
static uint32_t save_started_ms;
static uint32_t capture_number = 1U;
static char capture_name[8]; /* Cnnn */
static uint32_t saved_frames[ROLLING_SLOTS];
static uint32_t saved_sequence[ROLLING_SLOTS];
static DemoRollStatus status;

static uint32_t Now(void)
{
  return HAL_GetTick();
}

static void SlotPath(uint32_t slot, char *path, size_t size)
{
  (void)snprintf(path, size, "ROLL/SLOT%02lu.WAV", (unsigned long)slot);
}

static void PutLe16(uint8_t *destination, uint16_t value)
{
  destination[0] = (uint8_t)value;
  destination[1] = (uint8_t)(value >> 8);
}

static void PutLe32(uint8_t *destination, uint32_t value)
{
  destination[0] = (uint8_t)value;
  destination[1] = (uint8_t)(value >> 8);
  destination[2] = (uint8_t)(value >> 16);
  destination[3] = (uint8_t)(value >> 24);
}

/* The same WAV header the session recorder writes, at the file's start. */
static bool WriteHeader(uint32_t data_bytes)
{
  uint8_t header[ROLL_HEADER_BYTES];
  UINT written = 0U;

  memcpy(&header[0], "RIFF", 4U);
  PutLe32(&header[4], 36U + data_bytes);
  memcpy(&header[8], "WAVE", 4U);
  memcpy(&header[12], "fmt ", 4U);
  PutLe32(&header[16], 16U);
  PutLe16(&header[20], 1U);
  PutLe16(&header[22], RECORDING_LIMIT_CHANNELS);
  PutLe32(&header[24], RECORDING_LIMIT_SAMPLE_RATE_HZ);
  PutLe32(&header[28], RECORDING_LIMIT_SAMPLE_RATE_HZ * RECORDING_LIMIT_BLOCK_ALIGN);
  PutLe16(&header[32], RECORDING_LIMIT_BLOCK_ALIGN);
  PutLe16(&header[34], 16U);
  memcpy(&header[36], "data", 4U);
  PutLe32(&header[40], data_bytes);
  return (f_lseek(&segment, 0U) == FR_OK) &&
         (f_write(&segment, header, sizeof(header), &written) == FR_OK) &&
         (written == sizeof(header));
}

static void NoteStep(uint32_t started_ms)
{
  const uint32_t elapsed = Now() - started_ms;

  if (elapsed > status.step_ms_max)
  {
    status.step_ms_max = elapsed;
  }
}

static void Outcome(DemoSaveOutcome outcome)
{
  status.save = (uint8_t)outcome;
  DemoField_OnSaveOutcome((uint8_t)outcome);
}

static void EndSave(DemoSaveOutcome outcome)
{
  const uint32_t elapsed = Now() - save_started_ms;

  RollingCatalog_SaveDone(&catalog);
  save_state = SAVE_IDLE;
  status.save_active = false;
  if (outcome == DEMO_SAVE_SAVED)
  {
    ++status.saves;
    status.last_capture = capture_number;
    if (elapsed > status.save_ms_max)
    {
      status.save_ms_max = elapsed;
    }
    ++capture_number;
  }
  else
  {
    ++status.saves_failed;
  }
  printf("[roll] save %s %s in %lu ms\r\n",
         (outcome == DEMO_SAVE_SAVED) ? "committed" : "failed", capture_name,
         (unsigned long)elapsed);
  Outcome(outcome);
}

/* Opens the next segment: a free slot, preallocated contiguously when its file is
 * new or was truncated, with a header for a full segment. */
static bool OpenSegment(void)
{
  char path[20];
  const int slot = RollingCatalog_BeginSegment(&catalog);
  FRESULT result;

  if (slot < 0)
  {
    printf("[roll] FAIL: no slot free for the next segment\r\n");
    return false;
  }
  SlotPath((uint32_t)slot, path, sizeof(path));
  result = f_open(&segment, path, FA_WRITE | FA_OPEN_ALWAYS);
  if (result != FR_OK)
  {
    printf("[roll] FAIL: open %s result=%s\r\n", path, StorageService_ResultName(result));
    return false;
  }
  segment_open = true;
  if (f_size(&segment) != ROLL_SEGMENT_FILE_BYTES)
  {
    if ((f_lseek(&segment, 0U) != FR_OK) || (f_truncate(&segment) != FR_OK))
    {
      printf("[roll] FAIL: truncate %s\r\n", path);
      return false;
    }
    result = f_expand(&segment, ROLL_SEGMENT_FILE_BYTES, 1U);
    if (result != FR_OK)
    {
      printf("[roll] FAIL: allocate %s result=%s\r\n", path,
             StorageService_ResultName(result));
      return false;
    }
  }
  if (!WriteHeader(ROLL_SEGMENT_DATA_BYTES))
  {
    printf("[roll] FAIL: header %s\r\n", path);
    return false;
  }
  ++status.segments;
  segment_state = SEGMENT_OPEN;
  return true;
}

/* Closes the current segment. A partial one gets its true length and loses the
 * stale tail of an earlier use. */
static bool CloseSegment(void)
{
  const int slot = catalog.writing;
  uint32_t blocks = 0U;
  bool ok = true;

  if (slot >= 0)
  {
    blocks = catalog.slots[slot].blocks;
  }
  if (segment_open)
  {
    if ((slot >= 0) && (blocks < ROLLING_SEGMENT_BLOCKS))
    {
      ok = WriteHeader(blocks * ROLL_BLOCK_BYTES) &&
           (f_lseek(&segment, ROLL_HEADER_BYTES + (blocks * ROLL_BLOCK_BYTES)) == FR_OK) &&
           (f_truncate(&segment) == FR_OK);
    }
    ok = (f_close(&segment) == FR_OK) && ok;
    segment_open = false;
  }
  RollingCatalog_EndSegment(&catalog);
  segment_state = SEGMENT_NONE;
  return ok;
}

static bool CloseStep(void)
{
  if (!CloseSegment())
  {
    printf("[roll] FAIL: segment close\r\n");
    return false;
  }
  if (save_state == SAVE_REQUESTED)
  {
    const RollingSaveAdmission admission = RollingCatalog_PinWindow(&catalog);

    if (admission == ROLLING_SAVE_ACCEPTED)
    {
      uint32_t index;

      for (index = 0U; index < catalog.pinned_count; ++index)
      {
        const RollingSlot *pinned = &catalog.slots[catalog.pinned[index]];

        saved_frames[index] = (uint32_t)pinned->blocks * RECORDING_LIMIT_BLOCK_FRAMES;
        saved_sequence[index] = pinned->sequence;
      }
      save_index = 0U;
      save_state = SAVE_NAME;
    }
    else
    {
      save_state = SAVE_IDLE;
      status.save_active = false;
      ++status.saves_unavailable;
      Outcome(DEMO_SAVE_UNAVAILABLE);
    }
  }
  segment_state = SEGMENT_OPEN_PENDING;
  return true;
}

static bool OpenStep(void)
{
  if (!OpenSegment())
  {
    return false;
  }
  if (rotating)
  {
    const uint32_t elapsed = Now() - rotate_started_ms;

    rotating = false;
    ++status.rotations;
    if (elapsed > status.rotate_ms_max)
    {
      status.rotate_ms_max = elapsed;
    }
  }
  return true;
}

/* One formatted line; a line that does not fit is an error, never truncated or
 * written past the end of the buffer (p04.5 bench: the first header was). */
static bool WriteLine(FIL *file, const char *format, ...)
{
  char text[128];
  UINT written = 0U;
  va_list args;
  int length;

  va_start(args, format);
  length = vsnprintf(text, sizeof(text), format, args);
  va_end(args);
  if ((length <= 0) || ((size_t)length >= sizeof(text)))
  {
    return false;
  }
  return (f_write(file, text, (UINT)length, &written) == FR_OK) && (written == (UINT)length);
}

static bool WriteDescriptor(void)
{
  char path[24];
  FIL descriptor;
  uint32_t total = 0U;
  uint32_t index;
  bool ok;

  for (index = 0U; index < catalog.pinned_count; ++index)
  {
    total += saved_frames[index];
  }
  (void)snprintf(path, sizeof(path), "CAPS/%s.TXT", capture_name);
  if (f_open(&descriptor, path, FA_WRITE | FA_CREATE_NEW) != FR_OK)
  {
    return false;
  }
  ok = WriteLine(&descriptor, "SPOOKY DEMO CAPTURE 1\r\n") &&
       WriteLine(&descriptor, "format=48000Hz/16-bit/3ch [radio-L,radio-R,mic]\r\n") &&
       WriteLine(&descriptor, "segments=%u frames=%lu\r\n", (unsigned)catalog.pinned_count,
                 (unsigned long)total);
  for (index = 0U; ok && (index < catalog.pinned_count); ++index)
  {
    ok = WriteLine(&descriptor, "%sS%02lu.WAV frames=%lu sequence=%lu\r\n", capture_name,
                   (unsigned long)index, (unsigned long)saved_frames[index],
                   (unsigned long)saved_sequence[index]);
  }
  /* Closing commits the descriptor; only then is the capture saved. */
  ok = (f_close(&descriptor) == FR_OK) && ok;
  if (ok)
  {
    status.last_capture_frames = total;
  }
  return ok;
}

static void SaveStep(void)
{
  char from[20];
  char to[24];
  FRESULT result;

  switch (save_state)
  {
    case SAVE_NAME:
    {
      FILINFO info;

      /* Captures share the CAPS folder made when rolling started: creating a
       * folder clears a whole cluster, too long for one pass (bench, p04.5). A
       * number whose first segment exists belongs to an earlier capture or an
       * earlier boot: try the next one on the next pass. */
      (void)snprintf(capture_name, sizeof(capture_name), "C%03lu",
                     (unsigned long)capture_number);
      (void)snprintf(to, sizeof(to), "CAPS/%sS00.WAV", capture_name);
      result = f_stat(to, &info);
      if (result == FR_OK)
      {
        if (++capture_number > ROLL_CAPTURE_LIMIT)
        {
          EndSave(DEMO_SAVE_FAILED);
        }
      }
      else if (result != FR_NO_FILE)
      {
        printf("[roll] FAIL: stat %s result=%s\r\n", to, StorageService_ResultName(result));
        EndSave(DEMO_SAVE_FAILED);
      }
      else
      {
        save_state = SAVE_RENAME;
      }
      break;
    }
    case SAVE_RENAME:
      SlotPath(catalog.pinned[save_index], from, sizeof(from));
      (void)snprintf(to, sizeof(to), "CAPS/%sS%02u.WAV", capture_name, (unsigned)save_index);
      result = f_rename(from, to);
      if (result != FR_OK)
      {
        printf("[roll] FAIL: rename %s result=%s\r\n", from,
               StorageService_ResultName(result));
        EndSave(DEMO_SAVE_FAILED);
        break;
      }
      RollingCatalog_SlotReleased(&catalog, catalog.pinned[save_index]);
      if (++save_index >= catalog.pinned_count)
      {
        save_state = SAVE_DESCRIPTOR;
      }
      break;
    case SAVE_DESCRIPTOR:
      EndSave(WriteDescriptor() ? DEMO_SAVE_SAVED : DEMO_SAVE_FAILED);
      break;
    case SAVE_IDLE:
    case SAVE_REQUESTED:
    default:
      break;
  }
}

/* --- Recorder-facing ---------------------------------------------------------- */

bool DemoRolling_Begin(void)
{
  FRESULT result = StorageService_Acquire(STORAGE_OWNER_RECORDER);

  if (result != FR_OK)
  {
    printf("[roll] cannot start: mount result=%s\r\n", StorageService_ResultName(result));
    return false;
  }
  /* Both folders exist before capture starts, so no save or rotation ever
   * creates one. */
  result = f_mkdir("ROLL");
  if ((result == FR_OK) || (result == FR_EXIST))
  {
    result = f_mkdir("CAPS");
  }
  if ((result != FR_OK) && (result != FR_EXIST))
  {
    printf("[roll] cannot start: mkdir result=%s\r\n", StorageService_ResultName(result));
    (void)StorageService_Release(STORAGE_OWNER_RECORDER);
    return false;
  }
  if (save_state == SAVE_IDLE)
  {
    RollingCatalog_Init(&catalog);
  }
  segment_open = false;
  rotating = false;
  if (!OpenSegment())
  {
    DemoRolling_Fault("first segment");
    return false;
  }
  status.state = (uint8_t)DEMO_ROLL_RUNNING;
  printf("[roll] running: %u segments of %lu ms in ROLL/\r\n", (unsigned)ROLLING_SLOTS,
         (unsigned long)(((uint64_t)ROLLING_SEGMENT_BLOCKS * RECORDING_LIMIT_BLOCK_FRAMES *
                          1000U) / RECORDING_LIMIT_SAMPLE_RATE_HZ));
  return true;
}

bool DemoRolling_ReadyForBlock(void)
{
  return segment_open && (segment_state == SEGMENT_OPEN);
}

bool DemoRolling_WriteBlock(const void *block, uint32_t bytes, uint32_t *write_ms)
{
  const uint32_t start = Now();
  UINT written = 0U;
  const FRESULT result = f_write(&segment, block, (UINT)bytes, &written);

  if (write_ms != NULL)
  {
    *write_ms = Now() - start;
  }
  if ((result != FR_OK) || (written != bytes))
  {
    printf("[roll] FAIL: write result=%s bytes=%u/%lu\r\n",
           StorageService_ResultName(result), (unsigned)written, (unsigned long)bytes);
    return false;
  }
  if (RollingCatalog_BlockWritten(&catalog) || (save_state == SAVE_REQUESTED))
  {
    segment_state = SEGMENT_CLOSE_PENDING;
    rotating = true;
    rotate_started_ms = Now();
  }
  return true;
}

bool DemoRolling_Step(void)
{
  const uint32_t start = Now();
  bool ok = true;

  switch (segment_state)
  {
    case SEGMENT_CLOSE_PENDING:
      ok = CloseStep();
      break;
    case SEGMENT_OPEN_PENDING:
      ok = OpenStep();
      break;
    case SEGMENT_OPEN:
      if ((save_state != SAVE_IDLE) && (save_state != SAVE_REQUESTED))
      {
        SaveStep();
      }
      else
      {
        return true; /* nothing to do: not a step */
      }
      break;
    case SEGMENT_NONE:
    default:
      return true;
  }
  NoteStep(start);
  return ok;
}

bool DemoRolling_Stop(void)
{
  if (save_state != SAVE_IDLE)
  {
    return false;
  }
  (void)CloseSegment();
  RollingCatalog_Discard(&catalog);
  rotating = false;
  (void)StorageService_Release(STORAGE_OWNER_RECORDER);
  printf("[roll] stopped; window discarded\r\n");
  return true;
}

void DemoRolling_Fault(const char *reason)
{
  if (segment_open)
  {
    (void)f_close(&segment);
    segment_open = false;
  }
  RollingCatalog_EndSegment(&catalog);
  segment_state = SEGMENT_NONE;
  rotating = false;
  if (save_state != SAVE_IDLE)
  {
    EndSave(DEMO_SAVE_FAILED);
  }
  RollingCatalog_Discard(&catalog);
  if (StorageService_Owner() == STORAGE_OWNER_RECORDER)
  {
    (void)StorageService_Release(STORAGE_OWNER_RECORDER);
  }
  ++status.faults;
  status.state = (uint8_t)DEMO_ROLL_FAULT;
  printf("[roll] FAULT: %s; window discarded, retrying later\r\n",
         (reason != NULL) ? reason : "?");
}

bool DemoRolling_SaveActive(void)
{
  return save_state != SAVE_IDLE;
}

void DemoRolling_SetState(DemoRollState state)
{
  status.state = (uint8_t)state;
}

/* --- Control-facing ----------------------------------------------------------- */

DemoSaveOutcome DemoRolling_RequestSave(void)
{
  if (save_state != SAVE_IDLE)
  {
    ++status.saves_busy;
    status.save = (uint8_t)DEMO_SAVE_BUSY;
    return DEMO_SAVE_BUSY;
  }
  if ((status.state != (uint8_t)DEMO_ROLL_RUNNING) ||
      (RollingCatalog_CheckSave(&catalog) != ROLLING_SAVE_ACCEPTED))
  {
    ++status.saves_unavailable;
    status.save = (uint8_t)DEMO_SAVE_UNAVAILABLE;
    return DEMO_SAVE_UNAVAILABLE;
  }
  save_state = SAVE_REQUESTED;
  save_started_ms = Now();
  status.save_active = true;
  status.save = (uint8_t)DEMO_SAVE_WRITING;
  printf("[roll] save requested; window resolves at the next block\r\n");
  return DEMO_SAVE_WRITING;
}

void DemoRolling_GetStatus(DemoRollStatus *out)
{
  if (out == NULL)
  {
    return;
  }
  status.retained_ms = (uint32_t)(((uint64_t)RollingCatalog_RetainedBlocks(&catalog) *
                                   RECORDING_LIMIT_BLOCK_FRAMES * 1000U) /
                                  RECORDING_LIMIT_SAMPLE_RATE_HZ);
  status.reclaimed = catalog.segments_reclaimed;
  status.allocation_failures = catalog.allocation_failures;
  *out = status;
}

const char *DemoRolling_SaveName(uint8_t outcome)
{
  static const char *const names[] = {
    "NONE", "WRITING", "SAVED", "BUSY", "UNAVAILABLE", "FAILED"
  };

  return (outcome < (sizeof(names) / sizeof(names[0]))) ? names[outcome] : "?";
}

const char *DemoRolling_StateName(uint8_t state)
{
  static const char *const names[] = {"OFF", "WAITING", "RUNNING", "FAULT"};

  return (state < (sizeof(names) / sizeof(names[0]))) ? names[state] : "?";
}
