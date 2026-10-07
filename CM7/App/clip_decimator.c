#include "clip_decimator.h"

#include <stddef.h>
#include <string.h>

#define CLIP_DECIMATOR_CENTRE ((CLIP_DECIMATOR_TAPS - 1U) / 2U)
#define CLIP_DECIMATOR_SIDE_TAPS ((CLIP_DECIMATOR_CENTRE + 1U) / 2U)

/* Q15 halfband taps at odd offsets 1, 3, ..., 19 from the centre; the centre tap
 * is one half and the even offsets are zero. With the centre they sum to 32,768:
 * unity gain at DC. */
static const int32_t centre_tap = 16384;
static const int32_t side_taps[CLIP_DECIMATOR_SIDE_TAPS] = {
  10329, -3185, 1632, -913, 506, -264, 124, -50, 15, -2
};

_Static_assert(CLIP_DECIMATOR_SIDE_TAPS == 10U, "a 39-tap halfband has ten side taps");

bool ClipRange_Select(const uint32_t *segment_frames, uint32_t segment_count,
                      uint32_t wanted_frames, ClipRange *range)
{
  ClipRangePart newest_first[CLIP_RANGE_MAX_PARTS];
  uint32_t needed = wanted_frames;
  uint32_t index = segment_count;
  uint8_t count = 0U;
  uint8_t part;

  if (range == NULL)
  {
    return false;
  }
  (void)memset(range, 0, sizeof(*range));
  if (segment_frames == NULL)
  {
    return false;
  }
  while ((index > 0U) && (needed > 0U) && (count < CLIP_RANGE_MAX_PARTS))
  {
    const uint32_t held = segment_frames[--index];
    const uint32_t take = (held < needed) ? held : needed;

    if (take == 0U)
    {
      continue;
    }
    newest_first[count].segment = (uint8_t)index;
    newest_first[count].first_frame = held - take;
    newest_first[count].frames = take;
    needed -= take;
    range->frames += take;
    ++count;
  }
  for (part = 0U; part < count; ++part)
  {
    range->parts[part] = newest_first[count - 1U - part];
  }
  range->count = count;
  /* An even count keeps the end at the save point: drop the oldest frame. */
  if ((range->frames & 1U) != 0U)
  {
    ++range->parts[0].first_frame;
    --range->parts[0].frames;
    --range->frames;
    if (range->parts[0].frames == 0U)
    {
      for (part = 1U; part < range->count; ++part)
      {
        range->parts[part - 1U] = range->parts[part];
      }
      --range->count;
      (void)memset(&range->parts[range->count], 0, sizeof(range->parts[0]));
    }
  }
  return range->frames > 0U;
}

void ClipDecimator_Init(ClipDecimator *decimator)
{
  if (decimator != NULL)
  {
    (void)memset(decimator, 0, sizeof(*decimator));
  }
}

static int16_t Saturate(int64_t value)
{
  if (value > 32767)
  {
    return 32767;
  }
  if (value < -32768)
  {
    return -32768;
  }
  return (int16_t)value;
}

/* One output from the window ending at the newest frame. The window holds
 * (L + R); the shift by 16 is Q15 and the halving of the mono sum. */
static int16_t Filter(const ClipDecimator *decimator)
{
  const int32_t *window = &decimator->history[decimator->next];
  int64_t sum = (int64_t)centre_tap * window[CLIP_DECIMATOR_CENTRE];
  uint32_t tap;

  for (tap = 0U; tap < CLIP_DECIMATOR_SIDE_TAPS; ++tap)
  {
    const uint32_t offset = (2U * tap) + 1U;

    sum += (int64_t)side_taps[tap] * (int64_t)(window[CLIP_DECIMATOR_CENTRE - offset] +
                                               window[CLIP_DECIMATOR_CENTRE + offset]);
  }
  return Saturate((sum + 32768) >> 16);
}

uint32_t ClipDecimator_Process(ClipDecimator *decimator, const int16_t *frames,
                               uint32_t frame_count, int16_t *out, uint32_t capacity)
{
  uint32_t written = 0U;
  uint32_t frame;

  if ((decimator == NULL) || (frames == NULL) || (out == NULL))
  {
    return 0U;
  }
  for (frame = 0U; frame < frame_count; ++frame)
  {
    const int16_t *source = &frames[frame * CLIP_SOURCE_CHANNELS];
    const int32_t mono = (int32_t)source[0] + (int32_t)source[1];

    if (decimator->odd && (written >= capacity))
    {
      break; /* the next frame would complete a sample with nowhere to go */
    }
    if (!decimator->primed)
    {
      uint32_t index;

      for (index = 0U; index < (CLIP_DECIMATOR_TAPS * 2U); ++index)
      {
        decimator->history[index] = mono;
      }
      decimator->primed = true;
    }
    decimator->history[decimator->next] = mono;
    decimator->history[decimator->next + CLIP_DECIMATOR_TAPS] = mono;
    decimator->next = (decimator->next + 1U == CLIP_DECIMATOR_TAPS) ? 0U
                                                                  : decimator->next + 1U;
    if (decimator->odd)
    {
      out[written++] = Filter(decimator);
    }
    decimator->odd = !decimator->odd;
  }
  return written;
}
