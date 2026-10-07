#include "slicer.h"

#include <stddef.h>
#include <string.h>

#define SLICER_CHUNK_FRAMES (sizeof(((SlicerEngine *)0)->mix) / sizeof(int32_t))
#define SLICER_FULL 32767

_Static_assert(SLICER_CHUNK_FRAMES == 512U, "one radio half per chunk");
_Static_assert(SLICER_OUTPUT_RATE_HZ == (2U * SLICER_SOURCE_RATE_HZ),
               "the clip is read at half a sample per frame at unity");

/* 2^(k/12) in Q16 for k = 0..11. */
static const uint32_t semitone_q16[12] = {
  65536U, 69433U, 73562U, 77936U, 82570U, 87480U,
  92682U, 98193U, 104032U, 110218U, 116772U, 123715U
};

void Slicer_DefaultSlice(SlicerSlice *slice)
{
  if (slice == NULL)
  {
    return;
  }
  slice->pitch_semitones = 0;
  slice->gate_percent = 100U;
  slice->level_percent = 80U;
  slice->reserved = 0U;
}

bool Slicer_ClampSlice(SlicerSlice *slice)
{
  bool unchanged = true;

  if (slice == NULL)
  {
    return false;
  }
  if (slice->pitch_semitones < SLICER_PITCH_MIN)
  {
    slice->pitch_semitones = SLICER_PITCH_MIN;
    unchanged = false;
  }
  else if (slice->pitch_semitones > SLICER_PITCH_MAX)
  {
    slice->pitch_semitones = SLICER_PITCH_MAX;
    unchanged = false;
  }
  if (slice->gate_percent < SLICER_GATE_MIN)
  {
    slice->gate_percent = SLICER_GATE_MIN;
    unchanged = false;
  }
  else if (slice->gate_percent > 100U)
  {
    slice->gate_percent = 100U;
    unchanged = false;
  }
  if (slice->level_percent > 100U)
  {
    slice->level_percent = 100U;
    unchanged = false;
  }
  return unchanged;
}

void Slicer_DefaultSetup(SlicerSetup *setup, uint32_t clip_samples, uint8_t count)
{
  uint32_t slice;

  if (setup == NULL)
  {
    return;
  }
  if ((count == 0U) || (count > SLICE_MAP_MAX_SLICES))
  {
    count = (uint8_t)SLICE_MAP_MAX_SLICES;
  }
  (void)SliceMap_InitEqual(&setup->map, clip_samples, count);
  for (slice = 0U; slice < SLICE_MAP_MAX_SLICES; ++slice)
  {
    Slicer_DefaultSlice(&setup->slices[slice]);
  }
}

/* Clip samples per output frame in Q16: half a sample at unity. */
static uint32_t Increment(int32_t semitones)
{
  if (semitones < 0)
  {
    return (semitones <= -12) ? (65536U >> 2)
                              : (semitone_q16[semitones + 12] >> 2);
  }
  return (semitones >= 12) ? 65536U : (semitone_q16[semitones] >> 1);
}

uint32_t Slicer_SliceFrames(const SlicerSetup *setup, uint8_t slice)
{
  const SlicerSlice *params;
  uint64_t frames;
  uint32_t increment;

  if ((setup == NULL) || (slice >= setup->map.count))
  {
    return 0U;
  }
  params = &setup->slices[slice];
  increment = Increment(params->pitch_semitones);
  frames = (((uint64_t)SliceMap_Length(&setup->map, slice) << 16) + increment - 1U) / increment;
  frames = (frames * params->gate_percent) / 100U;
  return (frames == 0U) ? 1U : (uint32_t)frames;
}

static void Stage(SlicerEngine *engine, const SlicerSetup *setup)
{
  const uint32_t *words = (const uint32_t *)(const void *)setup;
  uint32_t index;

  engine->pending = false;
  for (index = 0U; index < (sizeof(*setup) / sizeof(uint32_t)); ++index)
  {
    engine->staged[index] = words[index];
  }
  engine->pending = true;
}

/* Makes the map fit the clip: one made for another length is made equal again. */
static void FitMap(SlicerEngine *engine)
{
  SliceMap *map = &engine->setup.map;

  if ((engine->count != 0U) && (map->clip_samples != engine->count))
  {
    (void)SliceMap_InitEqual(map, engine->count, (map->count != 0U) ? map->count
                                                                    : (uint8_t)SLICE_MAP_MAX_SLICES);
  }
}

static void TakeStaged(SlicerEngine *engine)
{
  uint32_t *words = (uint32_t *)(void *)&engine->setup;
  uint32_t index;

  for (index = 0U; index < (sizeof(engine->setup) / sizeof(uint32_t)); ++index)
  {
    words[index] = engine->staged[index];
  }
  engine->pending = false;
  FitMap(engine);
}

void Slicer_Init(SlicerEngine *engine)
{
  if (engine == NULL)
  {
    return;
  }
  (void)memset(engine, 0, sizeof(*engine));
  Slicer_DefaultSetup(&engine->setup, 0U, (uint8_t)SLICE_MAP_MAX_SLICES);
  engine->status.sounding = SLICER_NO_SLICE;
}

void Slicer_SetSetup(SlicerEngine *engine, const SlicerSetup *setup)
{
  SlicerSetup clamped;
  uint32_t slice;

  if ((engine == NULL) || (setup == NULL))
  {
    return;
  }
  clamped = *setup;
  for (slice = 0U; slice < SLICE_MAP_MAX_SLICES; ++slice)
  {
    (void)Slicer_ClampSlice(&clamped.slices[slice]);
  }
  Stage(engine, &clamped);
}

bool Slicer_Start(SlicerEngine *engine, const int16_t *samples, uint32_t count)
{
  if (engine == NULL)
  {
    return false;
  }
  engine->active = false;
  if ((samples == NULL) || (count == 0U))
  {
    return false;
  }
  engine->samples = samples;
  engine->count = count;
  engine->voices[0].active = false;
  engine->voices[1].active = false;
  engine->current = 0U;
  if (engine->pending)
  {
    TakeStaged(engine);
  }
  FitMap(engine);
  engine->status.sounding = SLICER_NO_SLICE;
  engine->active = true;
  return true;
}

void Slicer_Stop(SlicerEngine *engine)
{
  if (engine != NULL)
  {
    engine->active = false;
  }
}

bool Slicer_Active(const SlicerEngine *engine)
{
  return (engine != NULL) && engine->active;
}

/* Shortens a voice so it ends with a fade of at most SLICER_FADE_FRAMES from
 * where it is now. A voice already closer to its end keeps its own fade, so
 * its level never jumps. */
static void FadeOut(SlicerVoice *voice)
{
  if (voice->active && ((voice->length - voice->elapsed) > SLICER_FADE_FRAMES))
  {
    voice->length = voice->elapsed + SLICER_FADE_FRAMES;
    voice->fade_out = SLICER_FADE_FRAMES;
  }
}

/* The playing voice starts fading and moves to the fading slot. */
static void Cut(SlicerEngine *engine)
{
  SlicerVoice *playing = &engine->voices[engine->current];
  SlicerVoice *fading = &engine->voices[engine->current ^ 1U];

  if (!playing->active)
  {
    return;
  }
  if (fading->active)
  {
    fading->active = false; /* two voices at most */
    ++engine->status.fades_cut;
  }
  FadeOut(playing);
  engine->current ^= 1U;
}

bool Slicer_Trigger(SlicerEngine *engine, uint8_t slice, uint32_t offset_frames)
{
  SlicerVoice *voice;
  const SlicerSlice *params;
  uint64_t offset_q16;
  uint32_t start;

  if ((engine == NULL) || !engine->active || (engine->count == 0U))
  {
    return false;
  }
  if (engine->pending)
  {
    TakeStaged(engine);
  }
  if (slice >= engine->setup.map.count)
  {
    return false;
  }
  Cut(engine);
  ++engine->status.triggers;
  if (!SliceMap_Enabled(&engine->setup.map, slice))
  {
    ++engine->status.silent;
    return true;
  }
  voice = &engine->voices[engine->current];
  params = &engine->setup.slices[slice];
  start = SliceMap_Start(&engine->setup.map, slice);
  voice->slice = slice;
  voice->increment = Increment(params->pitch_semitones);
  voice->end = SliceMap_End(&engine->setup.map, slice);
  voice->length = Slicer_SliceFrames(&engine->setup, slice);
  if (offset_frames >= voice->length)
  {
    voice->active = false; /* joined after the slice would have ended */
    return true;
  }
  voice->fade_out = (voice->length < (2U * SLICER_FADE_FRAMES)) ? ((voice->length + 1U) / 2U)
                                                                 : SLICER_FADE_FRAMES;
  voice->elapsed = offset_frames;
  offset_q16 = (uint64_t)offset_frames * voice->increment;
  voice->index = start + (uint32_t)(offset_q16 >> 16);
  voice->fraction = (uint32_t)(offset_q16 & 0xFFFFU);
  voice->faded_in = 0U;
  voice->gain_q15 = (int32_t)(((uint32_t)params->level_percent * (uint32_t)SLICER_FULL) / 100U);
  voice->active = voice->index < voice->end;
  return true;
}

void Slicer_Silence(SlicerEngine *engine)
{
  if (engine != NULL)
  {
    Cut(engine);
  }
}

/* The voice's level now, Q15: the fade-in times the end fade times its gain. */
static int32_t Envelope(const SlicerVoice *voice)
{
  const uint32_t remaining = voice->length - voice->elapsed;
  int32_t in = SLICER_FULL;
  int32_t out = SLICER_FULL;

  if (voice->faded_in < SLICER_FADE_FRAMES)
  {
    in = (int32_t)((voice->faded_in * (uint32_t)SLICER_FULL) / SLICER_FADE_FRAMES);
  }
  if (remaining < voice->fade_out)
  {
    out = (int32_t)((remaining * (uint32_t)SLICER_FULL) / voice->fade_out);
  }
  return (((in * out) >> 15) * voice->gain_q15) >> 15;
}

static void RenderVoice(SlicerVoice *voice, int32_t *mix, uint32_t frames,
                        const int16_t *samples)
{
  uint32_t index = voice->index;
  uint32_t fraction = voice->fraction;
  uint32_t frame;

  for (frame = 0U; frame < frames; ++frame)
  {
    int32_t a;
    int32_t b;
    int32_t value;

    if ((voice->elapsed >= voice->length) || (index >= voice->end))
    {
      voice->active = false;
      break;
    }
    a = samples[index];
    b = (index + 1U < voice->end) ? samples[index + 1U] : a;
    value = a + (int32_t)(((int64_t)(b - a) * (int32_t)fraction) >> 16);
    mix[frame] += (value * Envelope(voice)) >> 15;
    ++voice->elapsed;
    ++voice->faded_in;
    fraction += voice->increment;
    index += fraction >> 16;
    fraction &= 0xFFFFU;
  }
  voice->index = index;
  voice->fraction = fraction;
}

static uint16_t Saturate(int32_t value)
{
  if (value > 32767)
  {
    return (uint16_t)(int16_t)32767;
  }
  if (value < -32768)
  {
    return (uint16_t)(int16_t)-32768;
  }
  return (uint16_t)(int16_t)value;
}

bool Slicer_Render(SlicerEngine *engine, uint16_t *stereo, uint32_t frame_count)
{
  const int16_t *samples;
  const SlicerVoice *playing;
  uint32_t done = 0U;

  if ((engine == NULL) || (stereo == NULL) || !engine->active)
  {
    return false;
  }
  samples = engine->samples;
  if ((samples == NULL) || (engine->count == 0U))
  {
    return false;
  }
  if (engine->pending)
  {
    TakeStaged(engine);
  }
  while (done < frame_count)
  {
    const uint32_t remaining = frame_count - done;
    const uint32_t chunk = (remaining < SLICER_CHUNK_FRAMES) ? remaining : SLICER_CHUNK_FRAMES;
    uint32_t voice;
    uint32_t frame;

    (void)memset(engine->mix, 0, chunk * sizeof(engine->mix[0]));
    for (voice = 0U; voice < 2U; ++voice)
    {
      if (engine->voices[voice].active)
      {
        RenderVoice(&engine->voices[voice], engine->mix, chunk, samples);
      }
    }
    for (frame = 0U; frame < chunk; ++frame)
    {
      const uint16_t value = Saturate(engine->mix[frame]);

      stereo[2U * (done + frame)] = value;
      stereo[(2U * (done + frame)) + 1U] = value;
    }
    done += chunk;
  }
  playing = &engine->voices[engine->current];
  if (playing->active)
  {
    engine->status.sounding = playing->slice;
    engine->status.position_permille =
      (uint16_t)(((uint64_t)playing->index * 1000U) / engine->count);
  }
  else
  {
    engine->status.sounding = SLICER_NO_SLICE;
  }
  ++engine->status.renders;
  return true;
}

void Slicer_GetStatus(const SlicerEngine *engine, SlicerStatus *status)
{
  if ((engine == NULL) || (status == NULL))
  {
    return;
  }
  status->triggers = engine->status.triggers;
  status->silent = engine->status.silent;
  status->fades_cut = engine->status.fades_cut;
  status->renders = engine->status.renders;
  status->sounding = engine->status.sounding;
  status->position_permille = engine->status.position_permille;
}
