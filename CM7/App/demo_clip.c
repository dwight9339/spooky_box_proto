#include "demo_clip.h"

#include <stdio.h>
#include <string.h>

#include "clip_decimator.h"
#include "clip_player.h"
#include "demo_field.h"
#include "ff.h"
#include "granular.h"
#include "main.h"
#include "recording_limits.h"
#include "step_pattern.h"
#include "storage_service.h"
#include "transport.h"

#define CLIP_HEADER_BYTES RECORDING_LIMIT_WAV_HEADER_BYTES
#define CLIP_FRAME_BYTES (CLIP_SOURCE_CHANNELS * sizeof(int16_t))
/* Grains that may sound at once. At the 64 MHz, cache-off clock a grain costs
 * about 0.42 ms per radio half (a half is 512 frames, 10,667 us). On the p04.7
 * bench (2026-10-07, docs/evidence/2026-10-07-granular-voice-demo.md) 16 grains
 * took 6.8 ms and overran the radio queue; limits 3 to 6 held with rolling
 * capture running, the longest loop pass reaching 72 ms at 6. 4 keeps a step of
 * margin below the 75 ms loop budget. */
#define CLIP_GRAIN_LIMIT 4U
/* The worst render measured at CLIP_GRAIN_LIMIT was 1,945 us. A render over this
 * budget is counted, not cut short: a count above zero means the limit no longer
 * bounds the work. */
#define CLIP_RENDER_BUDGET_US 2000U

_Static_assert(CLIP_RATE_HZ == GRANULAR_SOURCE_RATE_HZ, "the voice reads the 24 kHz clip");

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
static GranularEngine engine;
static volatile uint8_t voice = (uint8_t)DEMO_VOICE_GRAIN;
static volatile uint32_t render_us_max;
static volatile uint32_t render_over_budget;
static volatile uint32_t renders;

/* The transport and Granular's pattern (p04.14). The radio interrupt owns the
 * transport; the foreground posts targets it applies at its next render: the run
 * state and tempo at once, a fit on load at the next step while running (decision
 * 0026 item 4). Step values and flags are written one at a time by the
 * foreground and read by the interrupt. */
#define SEQ_KNOB_CENTRE 500U /* the position knob's no-offset point, permille */
#define SEQ_NO_STEP 0xFFFFFFFFU
static Transport transport;
static StepPattern grain_pattern;
static volatile bool run_target;
static volatile uint32_t tempo_target = TRANSPORT_BPM_DEFAULT_X100;
static volatile bool fit_pending;
static volatile uint32_t fit_tempo;
static volatile uint8_t fit_steps_per_beat;
static volatile uint16_t knob_permille = SEQ_KNOB_CENTRE;
static volatile uint32_t playhead = SEQ_NO_STEP;  /* the step the transport is on */
static volatile uint32_t held_step = SEQ_NO_STEP; /* the latest step that fired on */
static volatile uint32_t steps_fired;
static volatile uint32_t fits;
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

static void StopVoices(void)
{
  ClipPlayer_Stop(&player);
  Granular_Stop(&engine);
}

static bool Playing(void)
{
  return ClipPlayer_Active(&player) || Granular_Active(&engine);
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
  StopVoices();
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
  /* Fit on load (decision 0026 item 2): the interrupt applies it. */
  {
    uint32_t bpm_x100;
    uint8_t steps_per_beat;

    if (Transport_Fit(written, CLIP_RATE_HZ, grain_pattern.length, &bpm_x100, &steps_per_beat))
    {
      fit_tempo = bpm_x100;
      fit_steps_per_beat = steps_per_beat;
      fit_pending = true;
    }
  }
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

void DemoClip_Init(void)
{
  ClipPlayer_Init(&player);
  Transport_Init(&transport);
  StepPattern_InitSweep(&grain_pattern, 1000U); /* decision 0027 item 5 */
  Granular_Init(&engine, HAL_GetTick() ^ DWT->CYCCNT);
  Granular_SetMaxGrains(&engine, CLIP_GRAIN_LIMIT);
}

void DemoClip_Expect(void)
{
  StopVoices();
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

  if (play && ready && !Playing())
  {
    if (voice == (uint8_t)DEMO_VOICE_LOOP)
    {
      (void)ClipPlayer_Start(&player, clip, status.samples);
    }
    else
    {
      (void)Granular_Start(&engine, clip, status.samples);
    }
  }
  else if ((!play || !ready) && Playing())
  {
    StopVoices();
  }
}

void DemoClip_SetVoice(DemoVoice next)
{
  if ((uint8_t)next != voice)
  {
    StopVoices(); /* the next SetPlaying starts the new voice */
    voice = (uint8_t)next;
  }
}

DemoVoice DemoClip_Voice(void)
{
  return (DemoVoice)voice;
}

void DemoClip_SetGrainParams(const GranularParams *params)
{
  Granular_SetParams(&engine, params);
  if (params != NULL)
  {
    knob_permille = params->position_permille;
  }
}

void DemoClip_SetRunning(bool run)
{
  run_target = run;
}

bool DemoClip_RunTarget(void)
{
  return run_target;
}

void DemoClip_SetTempo(uint32_t bpm_x100)
{
  tempo_target = (bpm_x100 < TRANSPORT_BPM_MIN_X100) ? TRANSPORT_BPM_MIN_X100
                 : (bpm_x100 > TRANSPORT_BPM_MAX_X100) ? TRANSPORT_BPM_MAX_X100 : bpm_x100;
}

uint32_t DemoClip_TempoTarget(void)
{
  return tempo_target;
}

StepPattern *DemoClip_GrainPattern(void)
{
  return &grain_pattern;
}

void DemoClip_GetTransportStatus(DemoTransportStatus *out)
{
  if (out == NULL)
  {
    return;
  }
  out->running = transport.running;
  out->bpm_x100 = transport.bpm_x100;
  out->steps_per_beat = grain_pattern.steps_per_beat;
  out->playhead = playhead;
  out->held_step = held_step;
  out->steps_fired = steps_fired;
  out->fits = fits;
  out->fit_pending = fit_pending;
}

uint32_t DemoClip_GetGrains(uint16_t *position_permille, uint8_t *envelope, uint32_t capacity)
{
  return Granular_GetGrains(&engine, position_permille, envelope, capacity);
}

void DemoClip_GetVoiceStatus(DemoVoiceStatus *out)
{
  GranularStatus grains;

  if (out == NULL)
  {
    return;
  }
  Granular_GetStatus(&engine, &grains);
  out->voice = voice;
  out->renders = renders;
  out->render_us_max = render_us_max;
  out->render_over_budget = render_over_budget;
  out->render_budget_us = CLIP_RENDER_BUDGET_US;
  out->grains_started = grains.grains_started;
  out->grains_dropped = grains.grains_dropped;
  out->grains_active = grains.active;
  out->grains_high_water = grains.active_high_water;
  out->max_grains = Granular_MaxGrains(&engine);
}

void DemoClip_ResetVoiceStats(void)
{
  render_us_max = 0U;
  render_over_budget = 0U;
  engine.status.active_high_water = 0U;
}

void DemoClip_SetMaxGrains(uint32_t max_grains)
{
  Granular_SetMaxGrains(&engine, max_grains);
}

void DemoClip_GetStatus(DemoClipStatus *out)
{
  if (out == NULL)
  {
    return;
  }
  status.playing = Playing();
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

/* --- Sequencer, radio DMA interrupt ------------------------------------------ */

static void ApplyFit(void)
{
  Transport_SetTempo(&transport, fit_tempo);
  tempo_target = transport.bpm_x100;
  grain_pattern.steps_per_beat = fit_steps_per_beat; /* every pattern (0026 item 2) */
  fit_pending = false;
  ++fits;
}

/* The held step's value, offset by the knob, is where new grains start. Read
 * every pass, so a knob turn or an edit of the held step takes effect at once. */
static void PinPosition(void)
{
  if (held_step < STEP_PATTERN_MAX_STEPS)
  {
    Granular_OverridePosition(&engine,
                              StepPattern_Offset(grain_pattern.steps[held_step].value,
                                                 knob_permille, SEQ_KNOB_CENTRE, 1000U));
  }
}

/* A new step: an on step moves the grains there (decision 0027 item 6); an off
 * step keeps the previous position (0020 item 8). */
static void Step(void)
{
  uint32_t step;

  if (fit_pending)
  {
    ApplyFit(); /* at the step boundary while running */
  }
  step = StepPattern_Playhead(&grain_pattern, &transport);
  playhead = step;
  ++steps_fired;
  if (grain_pattern.steps[step].on)
  {
    held_step = step;
    PinPosition();
  }
}

static void ApplyTargets(void)
{
  if (run_target && !transport.running)
  {
    Transport_Start(&transport);
    playhead = SEQ_NO_STEP;
  }
  else if (!run_target && transport.running)
  {
    Transport_Stop(&transport);
    Granular_ReleasePosition(&engine);
    playhead = SEQ_NO_STEP;
    held_step = SEQ_NO_STEP;
  }
  if (fit_pending && !transport.running)
  {
    ApplyFit();
  }
  if (!fit_pending && (tempo_target != transport.bpm_x100))
  {
    Transport_SetTempo(&transport, tempo_target);
  }
}

/* Renders frame_count frames, split where a step starts so the step takes effect
 * on its own frame. The clock runs whichever voice plays, or none. */
static bool RenderSequenced(uint16_t *stereo, uint32_t frame_count)
{
  bool rendered = false;
  uint32_t done = 0U;

  ApplyTargets();
  if (transport.running && (playhead == SEQ_NO_STEP))
  {
    Step(); /* the first step of a start */
  }
  PinPosition();
  while (done < frame_count)
  {
    uint32_t chunk = frame_count - done;
    const uint32_t next = Transport_FramesToNextStep(&transport, grain_pattern.steps_per_beat);

    if ((next != 0U) && (next < chunk))
    {
      chunk = next;
    }
    if ((voice == (uint8_t)DEMO_VOICE_LOOP) ? ClipPlayer_Render(&player, &stereo[2U * done], chunk)
                                            : Granular_Render(&engine, &stereo[2U * done], chunk))
    {
      rendered = true;
    }
    Transport_Advance(&transport, chunk);
    done += chunk;
    if (transport.running && (next == chunk))
    {
      Step();
    }
  }
  return rendered;
}

bool DemoClip_RenderMonitor(uint16_t *stereo, uint32_t frame_count)
{
  const uint32_t start = DWT->CYCCNT;
  const bool rendered = RenderSequenced(stereo, frame_count);

  if (rendered)
  {
    const uint32_t elapsed_us = (DWT->CYCCNT - start) / (SystemCoreClock / 1000000U);

    ++renders;
    if (elapsed_us > render_us_max)
    {
      render_us_max = elapsed_us;
    }
    if (elapsed_us > CLIP_RENDER_BUDGET_US)
    {
      ++render_over_budget;
    }
  }
  return rendered;
}
