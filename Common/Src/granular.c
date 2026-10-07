#include "granular.h"

#include <stddef.h>
#include <string.h>

#define GRANULAR_CHUNK_FRAMES (sizeof(((GranularEngine *)0)->mix) / sizeof(int32_t))
#define GRANULAR_FRAMES_PER_MS (GRANULAR_OUTPUT_RATE_HZ / 1000U)
#define GRANULAR_ENVELOPE_FULL 32767

_Static_assert(GRANULAR_CHUNK_FRAMES == 512U, "one radio half per chunk");

/* 2^(k/12) in Q16 for k = 0..11. */
static const uint32_t semitone_q16[12] = {
  65536U, 69433U, 73562U, 77936U, 82570U, 87480U,
  92682U, 98193U, 104032U, 110218U, 116772U, 123715U
};

static uint32_t SquareRoot(uint32_t value)
{
  uint32_t root = 0U;
  uint32_t bit = 1UL << 30;

  while (bit > value)
  {
    bit >>= 2;
  }
  while (bit != 0U)
  {
    if (value >= root + bit)
    {
      value -= root + bit;
      root = (root >> 1) + bit;
    }
    else
    {
      root >>= 1;
    }
    bit >>= 2;
  }
  return root;
}

void Granular_DefaultParams(GranularParams *params)
{
  if (params == NULL)
  {
    return;
  }
  params->position_permille = 500U;
  params->size_ms = 80U;
  params->density = 20U;
  params->pitch_semitones = 0;
  params->spray_permille = 0U;
  params->envelope_percent = 50U;
  params->level_percent = 80U;
}

static uint32_t Clamp(uint32_t value, uint32_t low, uint32_t high, bool *unchanged)
{
  if (value < low)
  {
    *unchanged = false;
    return low;
  }
  if (value > high)
  {
    *unchanged = false;
    return high;
  }
  return value;
}

bool Granular_ClampParams(GranularParams *params)
{
  bool unchanged = true;

  if (params == NULL)
  {
    return false;
  }
  params->position_permille = (uint16_t)Clamp(params->position_permille, 0U, 1000U, &unchanged);
  params->size_ms = (uint16_t)Clamp(params->size_ms, GRANULAR_SIZE_MS_MIN, GRANULAR_SIZE_MS_MAX,
                                    &unchanged);
  params->density = (uint16_t)Clamp(params->density, GRANULAR_DENSITY_MIN,
                                    GRANULAR_DENSITY_MAX, &unchanged);
  if (params->pitch_semitones < GRANULAR_PITCH_MIN)
  {
    params->pitch_semitones = GRANULAR_PITCH_MIN;
    unchanged = false;
  }
  else if (params->pitch_semitones > GRANULAR_PITCH_MAX)
  {
    params->pitch_semitones = GRANULAR_PITCH_MAX;
    unchanged = false;
  }
  params->spray_permille = (uint16_t)Clamp(params->spray_permille, 0U, 1000U, &unchanged);
  params->envelope_percent = (uint8_t)Clamp(params->envelope_percent, 0U, 100U, &unchanged);
  params->level_percent = (uint8_t)Clamp(params->level_percent, 0U, 100U, &unchanged);
  return unchanged;
}

/* Clip samples per output frame in Q16: half a sample at unity, since the clip
 * is at half the output rate. */
static uint32_t Increment(int32_t semitones)
{
  int32_t octave = 0;
  int32_t step = semitones;
  uint32_t increment;

  while (step < 0)
  {
    step += 12;
    --octave;
  }
  while (step >= 12)
  {
    step -= 12;
    ++octave;
  }
  increment = semitone_q16[step] >> 1;
  return (octave >= 0) ? (increment << (uint32_t)octave) : (increment >> (uint32_t)-octave);
}

static void Convert(const GranularParams *params, uint32_t count, GranularControl *control)
{
  const uint32_t overlap_x1000 = (uint32_t)params->density * params->size_ms;
  const uint32_t root = SquareRoot(((overlap_x1000 < 1000U) ? 1000U : overlap_x1000) * 1000U);
  uint32_t gain = ((uint32_t)params->level_percent * 32768U / 100U) * 1000U / root;
  uint32_t ramp;

  control->position = (uint32_t)(((uint64_t)params->position_permille * count) / 1000U);
  if ((count > 0U) && (control->position >= count))
  {
    control->position = count - 1U;
  }
  control->spray = (uint32_t)(((uint64_t)params->spray_permille * count) / 1000U);
  control->length = (uint32_t)params->size_ms * GRANULAR_FRAMES_PER_MS;
  ramp = (control->length * params->envelope_percent) / 200U;
  if (ramp < GRANULAR_RAMP_MIN_FRAMES)
  {
    ramp = GRANULAR_RAMP_MIN_FRAMES;
  }
  control->ramp = (ramp > (control->length / 2U)) ? (control->length / 2U) : ramp;
  control->interval = GRANULAR_OUTPUT_RATE_HZ / params->density;
  control->increment = Increment(params->pitch_semitones);
  control->gain_q15 = (int32_t)((gain > 32767U) ? 32767U : gain);
}

static void Stage(GranularEngine *engine, const GranularControl *control)
{
  const uint32_t *words = (const uint32_t *)(const void *)control;
  uint32_t index;

  engine->pending = false;
  for (index = 0U; index < (sizeof(*control) / sizeof(uint32_t)); ++index)
  {
    engine->staged[index] = words[index];
  }
  engine->pending = true;
}

static void TakeStaged(GranularEngine *engine)
{
  uint32_t *words = (uint32_t *)(void *)&engine->control;
  uint32_t index;

  for (index = 0U; index < (sizeof(engine->control) / sizeof(uint32_t)); ++index)
  {
    words[index] = engine->staged[index];
  }
  engine->pending = false;
  if (engine->until_next > engine->control.interval)
  {
    engine->until_next = engine->control.interval;
  }
}

void Granular_Init(GranularEngine *engine, uint32_t seed)
{
  if (engine == NULL)
  {
    return;
  }
  (void)memset(engine, 0, sizeof(*engine));
  engine->random = (seed != 0U) ? seed : 0x2545F491U;
  Granular_DefaultParams(&engine->params);
  Convert(&engine->params, 0U, &engine->control);
}

void Granular_SetParams(GranularEngine *engine, const GranularParams *params)
{
  GranularControl control;

  if ((engine == NULL) || (params == NULL))
  {
    return;
  }
  engine->params = *params;
  (void)Granular_ClampParams(&engine->params);
  Convert(&engine->params, engine->count, &control);
  Stage(engine, &control);
}

bool Granular_Start(GranularEngine *engine, const int16_t *samples, uint32_t count)
{
  uint32_t grain;

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
  for (grain = 0U; grain < GRANULAR_MAX_GRAINS; ++grain)
  {
    engine->grains[grain].active = false;
  }
  Convert(&engine->params, count, &engine->control);
  engine->pending = false;
  engine->until_next = 0U;
  engine->status.active = 0U;
  engine->active = true;
  return true;
}

void Granular_Stop(GranularEngine *engine)
{
  if (engine != NULL)
  {
    engine->active = false;
  }
}

bool Granular_Active(const GranularEngine *engine)
{
  return (engine != NULL) && engine->active;
}

static uint32_t Random(GranularEngine *engine)
{
  uint32_t value = engine->random;

  value ^= value << 13;
  value ^= value >> 17;
  value ^= value << 5;
  engine->random = value;
  return value;
}

/* A random offset in [-spread / 2, spread - spread / 2]. */
static int64_t Spread(GranularEngine *engine, uint32_t spread)
{
  if (spread == 0U)
  {
    return 0;
  }
  return (int64_t)(Random(engine) % (spread + 1U)) - (int64_t)(spread / 2U);
}

static uint32_t Wrap(int64_t value, uint32_t modulus)
{
  int64_t wrapped = value % (int64_t)modulus;

  return (uint32_t)((wrapped < 0) ? (wrapped + (int64_t)modulus) : wrapped);
}

static uint32_t GrainStart(GranularEngine *engine, uint32_t count)
{
  const GranularControl *control = &engine->control;

  return Wrap((int64_t)control->position + Spread(engine, control->spray), count);
}

static void StartGrain(GranularEngine *engine, uint32_t count)
{
  uint32_t index;

  for (index = 0U; index < GRANULAR_MAX_GRAINS; ++index)
  {
    GranularGrain *grain = &engine->grains[index];

    if (!grain->active)
    {
      grain->start = GrainStart(engine, count);
      grain->index = grain->start;
      grain->fraction = 0U;
      grain->elapsed = 0U;
      grain->length = engine->control.length;
      grain->ramp = engine->control.ramp;
      grain->increment = engine->control.increment;
      grain->active = true;
      engine->status.last_start = grain->start;
      ++engine->status.grains_started;
      return;
    }
  }
  ++engine->status.grains_dropped;
}

static int32_t Envelope(const GranularGrain *grain, uint32_t elapsed)
{
  if (elapsed < grain->ramp)
  {
    return (int32_t)((elapsed * (uint32_t)GRANULAR_ENVELOPE_FULL) / grain->ramp);
  }
  if (elapsed >= (grain->length - grain->ramp))
  {
    return (int32_t)(((grain->length - elapsed) * (uint32_t)GRANULAR_ENVELOPE_FULL) /
                     grain->ramp);
  }
  return GRANULAR_ENVELOPE_FULL;
}

static void RenderGrain(GranularGrain *grain, int32_t *mix, uint32_t frames,
                        const int16_t *samples, uint32_t count)
{
  const uint32_t left = grain->length - grain->elapsed;
  const uint32_t todo = (frames < left) ? frames : left;
  uint32_t index = grain->index;
  uint32_t fraction = grain->fraction;
  uint32_t elapsed = grain->elapsed;
  uint32_t frame;

  for (frame = 0U; frame < todo; ++frame)
  {
    const uint32_t next = (index + 1U == count) ? 0U : index + 1U;
    const int32_t a = samples[index];
    const int32_t b = samples[next];
    const int32_t value = a + (int32_t)(((int64_t)(b - a) * (int32_t)fraction) >> 16);

    mix[frame] += (value * Envelope(grain, elapsed)) >> 15;
    ++elapsed;
    fraction += grain->increment;
    index += fraction >> 16;
    fraction &= 0xFFFFU;
    while (index >= count)
    {
      index -= count;
    }
  }
  grain->index = index;
  grain->fraction = fraction;
  grain->elapsed = elapsed;
  if (elapsed >= grain->length)
  {
    grain->active = false;
  }
}

static uint16_t Saturate(int64_t value)
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

bool Granular_Render(GranularEngine *engine, uint16_t *stereo, uint32_t frame_count)
{
  const int16_t *samples;
  uint32_t count;
  uint32_t done = 0U;
  uint32_t active = 0U;
  uint32_t index;

  if ((engine == NULL) || (stereo == NULL) || !engine->active)
  {
    return false;
  }
  samples = engine->samples;
  count = engine->count;
  if ((samples == NULL) || (count == 0U))
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
    const uint32_t chunk = (remaining < GRANULAR_CHUNK_FRAMES) ? remaining
                                                                : GRANULAR_CHUNK_FRAMES;
    uint32_t position = 0U;
    uint32_t frame;

    (void)memset(engine->mix, 0, chunk * sizeof(engine->mix[0]));
    while (position < chunk)
    {
      uint32_t segment;
      uint32_t grain;

      if (engine->until_next == 0U)
      {
        StartGrain(engine, count);
        engine->until_next = engine->control.interval;
      }
      segment = chunk - position;
      if (segment > engine->until_next)
      {
        segment = engine->until_next;
      }
      for (grain = 0U; grain < GRANULAR_MAX_GRAINS; ++grain)
      {
        if (engine->grains[grain].active)
        {
          RenderGrain(&engine->grains[grain], &engine->mix[position], segment, samples, count);
        }
      }
      position += segment;
      engine->until_next -= segment;
    }
    for (frame = 0U; frame < chunk; ++frame)
    {
      const uint16_t value =
        Saturate(((int64_t)engine->mix[frame] * engine->control.gain_q15) >> 15);

      stereo[2U * (done + frame)] = value;
      stereo[(2U * (done + frame)) + 1U] = value;
    }
    done += chunk;
  }
  for (index = 0U; index < GRANULAR_MAX_GRAINS; ++index)
  {
    active += engine->grains[index].active ? 1U : 0U;
  }
  engine->status.active = active;
  if (active > engine->status.active_high_water)
  {
    engine->status.active_high_water = active;
  }
  ++engine->status.renders;
  return true;
}

void Granular_GetStatus(const GranularEngine *engine, GranularStatus *status)
{
  if ((engine == NULL) || (status == NULL))
  {
    return;
  }
  status->grains_started = engine->status.grains_started;
  status->grains_dropped = engine->status.grains_dropped;
  status->active = engine->status.active;
  status->active_high_water = engine->status.active_high_water;
  status->renders = engine->status.renders;
  status->last_start = engine->status.last_start;
}

uint32_t Granular_GetGrains(const GranularEngine *engine, uint16_t *position_permille,
                            uint8_t *envelope, uint32_t capacity)
{
  const uint32_t count = (engine != NULL) ? engine->count : 0U;
  uint32_t written = 0U;
  uint32_t index;

  if ((engine == NULL) || (position_permille == NULL) || (envelope == NULL) ||
      !engine->active || (count == 0U))
  {
    return 0U;
  }
  for (index = 0U; (index < GRANULAR_MAX_GRAINS) && (written < capacity); ++index)
  {
    const GranularGrain *grain = &engine->grains[index];
    const uint32_t at = grain->index;
    const uint32_t elapsed = grain->elapsed;

    if (!grain->active || (at >= count) || (elapsed >= grain->length))
    {
      continue;
    }
    position_permille[written] = (uint16_t)(((uint64_t)at * 1000U) / count);
    envelope[written] = (uint8_t)(Envelope(grain, elapsed) >> 7);
    ++written;
  }
  return written;
}
