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
#include "slicer.h"
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
/* The worst render measured at CLIP_GRAIN_LIMIT, with the render aligned to the
 * flash line and the transport running, was 2,068 us at every setting (p04.14
 * bench, 2026-10-07); unaligned builds measured 1,945 to 2,459 us. This budget is
 * about 10 % above it. A render over it is counted, not cut short: a count above
 * zero means the limit no longer bounds the work. */
#define CLIP_RENDER_BUDGET_US 2300U

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
static SlicerEngine slicer;
/* The foreground chooses voice_target; the radio interrupt owns voice, the one
 * rendering, and moves it to the target, through a fade between the two
 * engines (0027 items 1 and 2). */
static volatile uint8_t voice_target = (uint8_t)DEMO_VOICE_GRAIN;
static volatile uint8_t voice = (uint8_t)DEMO_VOICE_GRAIN;
typedef enum
{
  SWITCH_NONE = 0,
  SWITCH_OUT,   /* the old engine fades out */
  SWITCH_IN     /* the new engine fades in */
} SwitchPhase;
static uint8_t switch_phase = (uint8_t)SWITCH_NONE;
static uint32_t switch_done;   /* frames of the phase rendered */
static volatile uint32_t switches;
static volatile uint32_t render_us_max;
static volatile uint32_t slice_render_us_max;
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
static StepPattern slice_pattern;
static volatile bool audition_pending;
static volatile uint8_t audition_slice;
static volatile uint16_t bar_phase;
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
  Slicer_Stop(&slicer);
}

static bool Playing(void)
{
  return ClipPlayer_Active(&player) || Granular_Active(&engine) || Slicer_Active(&slicer);
}

static bool IsEngine(uint8_t which)
{
  return (which == (uint8_t)DEMO_VOICE_GRAIN) || (which == (uint8_t)DEMO_VOICE_SLICE);
}

/* The pattern a voice plays; the plain loop keeps Granular's clock. */
static StepPattern *PatternOf(uint8_t which)
{
  return (which == (uint8_t)DEMO_VOICE_SLICE) ? &slice_pattern : &grain_pattern;
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
  /* The identity pattern: step n plays slice n (0022 item 10). */
  StepPattern_InitSweep(&slice_pattern, (uint16_t)SLICE_MAP_MAX_SLICES);
  Granular_Init(&engine, HAL_GetTick() ^ DWT->CYCCNT);
  Slicer_Init(&slicer);
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
    if (voice_target == (uint8_t)DEMO_VOICE_LOOP)
    {
      (void)ClipPlayer_Start(&player, clip, status.samples);
    }
    else
    {
      /* Both engines start, so a switch between them needs no foreground step. */
      (void)Granular_Start(&engine, clip, status.samples);
      (void)Slicer_Start(&slicer, clip, status.samples);
    }
  }
  else if ((!play || !ready) && Playing())
  {
    StopVoices();
  }
}

void DemoClip_SetVoice(DemoVoice next)
{
  if ((uint8_t)next == voice_target)
  {
    return;
  }
  if (!IsEngine((uint8_t)next) || !IsEngine(voice_target))
  {
    StopVoices(); /* the next SetPlaying starts the new voice */
  }
  voice_target = (uint8_t)next; /* between engines, the interrupt fades */
}

DemoVoice DemoClip_Voice(void)
{
  return (DemoVoice)voice_target;
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

StepPattern *DemoClip_SlicePattern(void)
{
  return &slice_pattern;
}

StepPattern *DemoClip_ActivePattern(void)
{
  return PatternOf(voice_target);
}

void DemoClip_SetSlicerSetup(const SlicerSetup *setup)
{
  Slicer_SetSetup(&slicer, setup);
}

void DemoClip_Audition(uint8_t slice)
{
  audition_slice = slice;
  audition_pending = true;
}

void DemoClip_GetSlicerStatus(SlicerStatus *out)
{
  Slicer_GetStatus(&slicer, out);
}

void DemoClip_GetTransportStatus(DemoTransportStatus *out)
{
  if (out == NULL)
  {
    return;
  }
  out->running = transport.running;
  out->bpm_x100 = transport.bpm_x100;
  out->steps_per_beat = PatternOf(voice_target)->steps_per_beat;
  out->bar_phase = bar_phase;
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
  out->voice = voice_target;
  out->renders = renders;
  out->render_us_max = render_us_max;
  out->slice_render_us_max = slice_render_us_max;
  out->switches = switches;
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
  slice_render_us_max = 0U;
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
  /* Every pattern (0026 item 2). */
  grain_pattern.steps_per_beat = fit_steps_per_beat;
  slice_pattern.steps_per_beat = fit_steps_per_beat;
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

/* An on step of the active pattern fires its engine. Granular: the grains move
 * to the step's position (decision 0027 item 6); an off step keeps the previous
 * position (0020 item 8). Slicer: the step's slice plays from offset_frames into
 * it, cutting the one before (0022 item 3); an off step fires nothing, so the
 * slice before plays on to its end. */
static void Fire(uint32_t step, uint32_t offset_frames)
{
  const StepPattern *pattern = PatternOf(voice);

  if (!pattern->steps[step].on)
  {
    return;
  }
  if (voice == (uint8_t)DEMO_VOICE_SLICE)
  {
    (void)Slicer_Trigger(&slicer, (uint8_t)pattern->steps[step].value, offset_frames);
  }
  else
  {
    held_step = step;
    PinPosition();
  }
}

/* A new step of the active pattern; the inactive pattern fires nothing (0027
 * item 3). */
static void Step(void)
{
  uint32_t step;

  if (fit_pending)
  {
    ApplyFit(); /* at the step boundary while running */
  }
  step = StepPattern_Playhead(PatternOf(voice), &transport);
  playhead = step;
  ++steps_fired;
  Fire(step, 0U);
}

/* Output frames of one step at the transport's tempo. */
static uint32_t StepFrames(uint8_t steps_per_beat)
{
  const uint32_t q = (steps_per_beat == 0U) ? 1U : steps_per_beat;

  return (uint32_t)(((uint64_t)TRANSPORT_RATE_HZ * 60U * 100U) /
                    ((uint64_t)transport.bpm_x100 * q));
}

/* The engine switched in joins the step the transport is on, in time: the
 * Slicer from as far into the slice as the step has run. */
static void SwitchIn(void)
{
  const StepPattern *pattern = PatternOf(voice);
  const uint32_t step_frames = StepFrames(pattern->steps_per_beat);
  const uint32_t to_next = Transport_FramesToNextStep(&transport, pattern->steps_per_beat);

  if (!transport.running)
  {
    return;
  }
  playhead = StepPattern_Playhead(pattern, &transport);
  Fire(playhead, (to_next < step_frames) ? (step_frames - to_next) : 0U);
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
    Slicer_Silence(&slicer);
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
  /* An engine switch: faded while both engines play, at once otherwise. A
   * switch to or from the plain loop cancels one in progress. */
  if (!IsEngine(voice_target) && (switch_phase != (uint8_t)SWITCH_NONE))
  {
    switch_phase = (uint8_t)SWITCH_NONE;
  }
  if ((voice_target != voice) && (switch_phase == (uint8_t)SWITCH_NONE))
  {
    if (IsEngine(voice) && IsEngine(voice_target) && Granular_Active(&engine) &&
        Slicer_Active(&slicer))
    {
      switch_phase = (uint8_t)SWITCH_OUT;
      switch_done = 0U;
      ++switches;
    }
    else
    {
      voice = voice_target;
    }
  }
  /* An audition plays only with the Slicer settled and the transport stopped. */
  if (audition_pending)
  {
    audition_pending = false;
    if ((voice == (uint8_t)DEMO_VOICE_SLICE) && (switch_phase == (uint8_t)SWITCH_NONE) &&
        !transport.running)
    {
      (void)Slicer_Trigger(&slicer, audition_slice, 0U);
    }
  }
}

static bool RenderVoice(uint16_t *stereo, uint32_t frames)
{
  switch (voice)
  {
    case DEMO_VOICE_LOOP:
      return ClipPlayer_Render(&player, stereo, frames);
    case DEMO_VOICE_SLICE:
      return Slicer_Render(&slicer, stereo, frames);
    case DEMO_VOICE_GRAIN:
    default:
      return Granular_Render(&engine, stereo, frames);
  }
}

/* The switch's gain over frames, from switch_done: down while the old engine
 * fades out, up while the new one fades in. */
static void Fade(uint16_t *stereo, uint32_t frames)
{
  const bool out = switch_phase == (uint8_t)SWITCH_OUT;
  uint32_t frame;

  for (frame = 0U; frame < frames; ++frame)
  {
    const uint32_t at = switch_done + frame;
    const int32_t gain = (int32_t)(out ? (DEMO_CLIP_SWITCH_FADE_FRAMES - at) : at);
    const int32_t value = ((int32_t)(int16_t)stereo[2U * frame] * gain) /
                          (int32_t)DEMO_CLIP_SWITCH_FADE_FRAMES;

    stereo[2U * frame] = (uint16_t)(int16_t)value;
    stereo[(2U * frame) + 1U] = (uint16_t)(int16_t)value;
  }
}

/* Renders frame_count frames, split where a step starts so the step takes effect
 * on its own frame, and where a switch moves from one fade to the next. The
 * clock runs whichever voice plays, or none. */
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
    const uint32_t next = Transport_FramesToNextStep(&transport, PatternOf(voice)->steps_per_beat);
    bool switched = false;

    if ((next != 0U) && (next < chunk))
    {
      chunk = next;
    }
    if ((switch_phase != (uint8_t)SWITCH_NONE) &&
        ((DEMO_CLIP_SWITCH_FADE_FRAMES - switch_done) < chunk))
    {
      chunk = DEMO_CLIP_SWITCH_FADE_FRAMES - switch_done;
    }
    if (RenderVoice(&stereo[2U * done], chunk))
    {
      rendered = true;
      if (switch_phase != (uint8_t)SWITCH_NONE)
      {
        Fade(&stereo[2U * done], chunk);
      }
    }
    Transport_Advance(&transport, chunk);
    done += chunk;
    if (switch_phase != (uint8_t)SWITCH_NONE)
    {
      switch_done += chunk;
      if (switch_done >= DEMO_CLIP_SWITCH_FADE_FRAMES)
      {
        switch_done = 0U;
        if (switch_phase == (uint8_t)SWITCH_OUT)
        {
          voice = voice_target;
          switch_phase = (uint8_t)SWITCH_IN;
          switched = true;
        }
        else
        {
          switch_phase = (uint8_t)SWITCH_NONE;
        }
      }
    }
    if (transport.running && (next == chunk))
    {
      Step();
    }
    else if (switched)
    {
      SwitchIn();
    }
  }
  /* One 4/4 bar is 4 beats (0026 item 1): the phase is the beat position
   * modulo 4 beats, in 1/65536 of a bar. */
  bar_phase = (uint16_t)((Transport_BeatsQ16(&transport) & ((4ULL << 16) - 1U)) >> 2);
  return rendered;
}

bool DemoClip_RenderMonitor(uint16_t *stereo, uint32_t frame_count)
{
  const uint32_t start = DWT->CYCCNT;
  const bool slicing = voice == (uint8_t)DEMO_VOICE_SLICE;
  const bool rendered = RenderSequenced(stereo, frame_count);

  if (rendered)
  {
    const uint32_t elapsed_us = (DWT->CYCCNT - start) / (SystemCoreClock / 1000000U);

    ++renders;
    if (elapsed_us > render_us_max)
    {
      render_us_max = elapsed_us;
    }
    if (slicing && (elapsed_us > slice_render_us_max))
    {
      slice_render_us_max = elapsed_us;
    }
    if (elapsed_us > CLIP_RENDER_BUDGET_US)
    {
      ++render_over_budget;
    }
  }
  return rendered;
}
