#include "demo_rolling.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "demo_clip.h"
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

/* FAT search time per pass when a slot file needs its allocation (p04.28): in
 * the background while a segment is open, and on the stream's path when a
 * segment must open in a slot that has no allocation yet. */
#ifndef SPOOKY_ROLL_PREPARE_STEP_MS
#define SPOOKY_ROLL_PREPARE_STEP_MS 8U
#endif
#ifndef SPOOKY_ROLL_OPEN_STEP_MS
#define SPOOKY_ROLL_OPEN_STEP_MS 16U
#endif

_Static_assert(ROLL_BLOCK_BYTES == 24576U, "recorder blocks are 4,096 three-channel frames");

typedef enum
{
  SEGMENT_NONE = 0,
  SEGMENT_OPEN,
  SEGMENT_CLOSE_PENDING,  /* full, or a save boundary: close before the next block */
  SEGMENT_OPEN_PENDING
} SegmentState;

/* Giving a slot file its full contiguous length (p04.28). FatFs f_expand reads
 * the FAT in one call until it finds a free run, about 420 ms on the bench card;
 * the storage service's run search reads it in bounded steps instead and points
 * f_expand at the run, which then returns at once. */
typedef enum
{
  ALLOC_IDLE = 0,
  ALLOC_SEARCH,
  ALLOC_EXPAND
} AllocPhase;

typedef enum
{
  ALLOC_DONE = 0,
  ALLOC_CONTINUE,
  ALLOC_FAILED
} AllocStep;

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
/* Slots whose file has its full length, so opening one needs no FAT search.
 * Bit n is slot n. Read from the ROLL folder when rolling starts. */
static uint32_t ready_mask;
static AllocPhase open_phase;       /* the segment file, on the stream's path */
static FIL spare;                   /* an empty slot prepared in the background */
static bool spare_open;
static int prepare_slot = -1;
static AllocPhase prepare_phase;
static bool prepare_disabled;       /* a preparation failed: none until restart */

_Static_assert(ROLLING_SLOTS <= 32U, "ready_mask has a bit per slot");

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
  const uint32_t segments = catalog.pinned_count;
  const uint32_t capture = capture_number;

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
  /* The C-010 chord's clip loads from the capture just committed (p04.6). */
  if (DemoClip_Waiting())
  {
    if (outcome == DEMO_SAVE_SAVED)
    {
      DemoClip_Begin(capture, capture_name, saved_frames, segments);
    }
    else
    {
      DemoClip_Fail(DEMO_CLIP_FAULT_SAVE_FAILED);
    }
  }
}

static uint32_t SlotBit(uint32_t slot)
{
  return 1UL << slot;
}

/* Starts allocating an empty (truncated) file: the stepped run search on FAT32,
 * else f_expand alone, whose search a FAT12/16 table keeps short. */
static AllocStep AllocBegin(AllocPhase *phase)
{
  switch (StorageService_BeginRunSearch(STORAGE_OWNER_RECORDER, ROLL_SEGMENT_FILE_BYTES))
  {
    case STORAGE_RUN_SEARCHING:
      *phase = ALLOC_SEARCH;
      return ALLOC_CONTINUE;
    case STORAGE_RUN_UNSUPPORTED:
      *phase = ALLOC_EXPAND;
      return ALLOC_CONTINUE;
    case STORAGE_RUN_NOT_FOUND:
      printf("[roll] FAIL: no free run for a segment\r\n");
      return ALLOC_FAILED;
    default:
      return ALLOC_FAILED;
  }
}

/* One allocation step: a bounded slice of the search, or the f_expand that
 * takes the run once found. */
static AllocStep AllocContinue(FIL *file, AllocPhase *phase, uint32_t budget_ms)
{
  StorageRunReport report;
  FRESULT result;

  if (*phase == ALLOC_SEARCH)
  {
    switch (StorageService_StepRunSearch(STORAGE_OWNER_RECORDER, budget_ms, &report))
    {
      case STORAGE_RUN_SEARCHING:
        return ALLOC_CONTINUE;
      case STORAGE_RUN_FOUND:
        *phase = ALLOC_EXPAND;
        return ALLOC_CONTINUE;
      case STORAGE_RUN_NOT_FOUND:
        printf("[roll] FAIL: free space too fragmented for a segment "
               "largest-run-clusters=%lu\r\n", (unsigned long)report.longest_clusters);
        *phase = ALLOC_IDLE;
        return ALLOC_FAILED;
      default:
        *phase = ALLOC_IDLE;
        return ALLOC_FAILED;
    }
  }
  *phase = ALLOC_IDLE;
  result = f_expand(file, ROLL_SEGMENT_FILE_BYTES, 1U);
  if (result != FR_OK)
  {
    printf("[roll] FAIL: allocate result=%s\r\n", StorageService_ResultName(result));
    return ALLOC_FAILED;
  }
  return ALLOC_DONE;
}

/* Opens a slot file and, unless it already has its full length, empties it for
 * allocation. *full tells which. */
static bool OpenSlotFile(FIL *file, uint32_t slot, bool *full)
{
  char path[20];
  FRESULT result;

  SlotPath(slot, path, sizeof(path));
  result = f_open(file, path, FA_WRITE | FA_OPEN_ALWAYS);
  if (result != FR_OK)
  {
    printf("[roll] FAIL: open %s result=%s\r\n", path, StorageService_ResultName(result));
    return false;
  }
  *full = f_size(file) == ROLL_SEGMENT_FILE_BYTES;
  if (!*full && ((f_lseek(file, 0U) != FR_OK) || (f_truncate(file) != FR_OK)))
  {
    printf("[roll] FAIL: truncate %s\r\n", path);
    (void)f_close(file);
    return false;
  }
  return true;
}

/* The ROLL folder's slot files that already have their full length. Read once
 * when rolling starts; afterwards ready_mask follows every allocation, rename
 * and truncation. */
static void ScanReadySlots(void)
{
  DIR dir;
  FILINFO info;

  ready_mask = 0U;
  if (f_opendir(&dir, "ROLL") != FR_OK)
  {
    return;
  }
  while ((f_readdir(&dir, &info) == FR_OK) && (info.fname[0] != '\0'))
  {
    const char *name = info.fname;

    if ((strncmp(name, "SLOT", 4U) == 0) && (name[4] >= '0') && (name[4] <= '9') &&
        (name[5] >= '0') && (name[5] <= '9') && (strcmp(&name[6], ".WAV") == 0) &&
        (info.fsize == ROLL_SEGMENT_FILE_BYTES))
    {
      const uint32_t slot = ((uint32_t)(name[4] - '0') * 10U) + (uint32_t)(name[5] - '0');

      if (slot < ROLLING_SLOTS)
      {
        ready_mask |= SlotBit(slot);
      }
    }
  }
  (void)f_closedir(&dir);
}

/* Stops a background preparation. Called before anything else touches a slot
 * file or the FAT search: FatFs has no file locking here (_FS_LOCK 0), so the
 * spare must never hold the slot a segment opens. */
static void PrepareAbort(void)
{
  if (spare_open)
  {
    (void)f_close(&spare);
    spare_open = false;
  }
  prepare_slot = -1;
  prepare_phase = ALLOC_IDLE;
}

/* One background step while a segment is open and nothing else runs: give an
 * empty slot without an allocation its full length, so the next rotation into
 * it is a plain open. False when there is nothing to prepare (not a step). A
 * failure ends preparation until rolling restarts; the stream carries on, and
 * a rotation into an unprepared slot allocates on its own path. */
static bool PrepareStep(void)
{
  AllocStep step;

  if (prepare_disabled)
  {
    return false;
  }
  if (prepare_slot < 0)
  {
    uint32_t slot;
    bool full = false;

    for (slot = 0U; slot < ROLLING_SLOTS; ++slot)
    {
      if ((catalog.slots[slot].state == (uint8_t)ROLLING_SLOT_EMPTY) &&
          ((ready_mask & SlotBit(slot)) == 0U))
      {
        break;
      }
    }
    if (slot >= ROLLING_SLOTS)
    {
      return false;
    }
    if (!OpenSlotFile(&spare, slot, &full))
    {
      prepare_disabled = true;
      return true;
    }
    spare_open = true;
    prepare_slot = (int)slot;
    step = full ? ALLOC_DONE : AllocBegin(&prepare_phase);
  }
  else
  {
    step = AllocContinue(&spare, &prepare_phase, SPOOKY_ROLL_PREPARE_STEP_MS);
  }
  if (step == ALLOC_CONTINUE)
  {
    return true;
  }
  if ((step == ALLOC_DONE) && (f_close(&spare) == FR_OK))
  {
    spare_open = false;
    ready_mask |= SlotBit((uint32_t)prepare_slot);
    ++status.prepared;
    prepare_slot = -1;
    return true;
  }
  printf("[roll] prepare SLOT%02d failed; preparation off until restart\r\n", prepare_slot);
  PrepareAbort();
  prepare_disabled = true;
  return true;
}

/* Opens the next segment, preferring a slot whose file already has its full
 * length. A slot without one is allocated here in bounded steps across passes
 * while the stream waits (ALLOC_CONTINUE). The segment gets a header for a full
 * segment. */
static AllocStep OpenSegment(void)
{
  AllocStep step;

  if (open_phase == ALLOC_IDLE)
  {
    const int slot = RollingCatalog_BeginSegmentPreferring(&catalog, ready_mask);
    bool full = false;

    PrepareAbort();
    if (slot < 0)
    {
      printf("[roll] FAIL: no slot free for the next segment\r\n");
      return ALLOC_FAILED;
    }
    if (!OpenSlotFile(&segment, (uint32_t)slot, &full))
    {
      return ALLOC_FAILED;
    }
    segment_open = true;
    if (full)
    {
      step = ALLOC_DONE;
    }
    else
    {
      ready_mask &= ~SlotBit((uint32_t)slot);
      ++status.open_allocations;
      step = AllocBegin(&open_phase);
    }
  }
  else
  {
    step = AllocContinue(&segment, &open_phase, SPOOKY_ROLL_OPEN_STEP_MS);
  }
  if (step != ALLOC_DONE)
  {
    return step;
  }
  ready_mask |= SlotBit((uint32_t)catalog.writing);
  if (!WriteHeader(ROLL_SEGMENT_DATA_BYTES))
  {
    printf("[roll] FAIL: segment header\r\n");
    return ALLOC_FAILED;
  }
  ++status.segments;
  segment_state = SEGMENT_OPEN;
  return ALLOC_DONE;
}

/* Closes the current segment. A partial one gets its true length in the header.
 * Only a segment about to be pinned by a save is also cut to that length, since
 * it becomes the capture's file; any other keeps its allocation, so the slot
 * reopens without a FAT search (p04.28). */
static bool CloseSegment(bool cut_partial)
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
      ok = WriteHeader(blocks * ROLL_BLOCK_BYTES);
      if (cut_partial)
      {
        ready_mask &= ~SlotBit((uint32_t)slot);
        ok = ok &&
             (f_lseek(&segment, ROLL_HEADER_BYTES + (blocks * ROLL_BLOCK_BYTES)) == FR_OK) &&
             (f_truncate(&segment) == FR_OK);
      }
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
  PrepareAbort();
  if (!CloseSegment(save_state == SAVE_REQUESTED))
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
      if (DemoClip_Waiting())
      {
        DemoClip_Fail(DEMO_CLIP_FAULT_SAVE_UNAVAILABLE);
      }
    }
  }
  segment_state = SEGMENT_OPEN_PENDING;
  return true;
}

static bool OpenStep(void)
{
  const AllocStep step = OpenSegment();

  if (step != ALLOC_DONE)
  {
    return step == ALLOC_CONTINUE;
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
      ready_mask &= ~SlotBit(catalog.pinned[save_index]);
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
  open_phase = ALLOC_IDLE;
  PrepareAbort();
  prepare_disabled = false;
  ScanReadySlots();
  /* With a ready slot the first segment opens here at once; otherwise its
   * allocation continues in DemoRolling_Step while the first blocks queue. */
  segment_state = SEGMENT_OPEN_PENDING;
  if (OpenSegment() == ALLOC_FAILED)
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
      else if ((save_state == SAVE_IDLE) && DemoClip_Loading())
      {
        DemoClip_Step(); /* timed by demo_clip.c; a failure ends only the load */
        return true;
      }
      else if ((save_state == SAVE_IDLE) && PrepareStep())
      {
        break; /* a background preparation step */
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
  if (DemoRolling_SaveActive())
  {
    return false;
  }
  PrepareAbort();
  (void)CloseSegment(false);
  RollingCatalog_Discard(&catalog);
  rotating = false;
  (void)StorageService_Release(STORAGE_OWNER_RECORDER);
  printf("[roll] stopped; window discarded\r\n");
  return true;
}

void DemoRolling_Fault(const char *reason)
{
  PrepareAbort();
  open_phase = ALLOC_IDLE;
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
  DemoClip_Abort();
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
  return (save_state != SAVE_IDLE) || DemoClip_Loading();
}

void DemoRolling_SetState(DemoRollState state)
{
  status.state = (uint8_t)state;
}

/* --- Control-facing ----------------------------------------------------------- */

DemoSaveOutcome DemoRolling_RequestSave(void)
{
  if (DemoRolling_SaveActive())
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
  PrepareAbort();
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
