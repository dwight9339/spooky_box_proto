#include "transport.h"

#include <stddef.h>

/* Beats (Q16) per frame = bpm_x100 * 65536 / (100 * 60 * TRANSPORT_RATE_HZ)
 * = bpm_x100 * 32 / 140625. */
#define BEAT_NUMERATOR 32U
#define BEAT_DENOMINATOR 140625U

_Static_assert((uint64_t)100U * 60U * TRANSPORT_RATE_HZ * BEAT_NUMERATOR ==
               (uint64_t)65536U * BEAT_DENOMINATOR, "beats per frame");

static uint32_t ClampTempo(uint32_t bpm_x100)
{
  if (bpm_x100 < TRANSPORT_BPM_MIN_X100)
  {
    return TRANSPORT_BPM_MIN_X100;
  }
  return (bpm_x100 > TRANSPORT_BPM_MAX_X100) ? TRANSPORT_BPM_MAX_X100 : bpm_x100;
}

static uint8_t ClampSteps(uint8_t steps_per_beat)
{
  if (steps_per_beat < 1U)
  {
    return 1U;
  }
  return (steps_per_beat > 8U) ? 8U : steps_per_beat;
}

void Transport_Init(Transport *transport)
{
  if (transport == NULL)
  {
    return;
  }
  transport->bpm_x100 = TRANSPORT_BPM_DEFAULT_X100;
  transport->running = false;
  transport->base_q16 = 0U;
  transport->frames = 0U;
}

uint64_t Transport_BeatsQ16(const Transport *transport)
{
  if (transport == NULL)
  {
    return 0U;
  }
  return transport->base_q16 +
         ((transport->frames * transport->bpm_x100 * BEAT_NUMERATOR) / BEAT_DENOMINATOR);
}

void Transport_SetTempo(Transport *transport, uint32_t bpm_x100)
{
  if (transport == NULL)
  {
    return;
  }
  transport->base_q16 = Transport_BeatsQ16(transport);
  transport->frames = 0U;
  transport->bpm_x100 = ClampTempo(bpm_x100);
}

void Transport_Start(Transport *transport)
{
  if (transport == NULL)
  {
    return;
  }
  transport->running = true;
  transport->base_q16 = 0U;
  transport->frames = 0U;
}

void Transport_Stop(Transport *transport)
{
  if (transport != NULL)
  {
    transport->running = false;
  }
}

void Transport_Advance(Transport *transport, uint32_t frames)
{
  if ((transport != NULL) && transport->running)
  {
    transport->frames += frames;
  }
}

uint32_t Transport_Step(const Transport *transport, uint8_t steps_per_beat)
{
  return (uint32_t)((Transport_BeatsQ16(transport) * ClampSteps(steps_per_beat)) >> 16);
}

uint32_t Transport_FramesToNextStep(const Transport *transport, uint8_t steps_per_beat)
{
  const uint8_t q = ClampSteps(steps_per_beat);
  uint64_t target;
  uint64_t need;
  uint64_t frames;

  if ((transport == NULL) || !transport->running)
  {
    return 0U;
  }
  /* The first beat position (Q16) on the next step, rounded up so that
   * Transport_Step reaches it exactly there. */
  target = ((((uint64_t)Transport_Step(transport, q) + 1U) << 16) + q - 1U) / q;
  need = target - transport->base_q16;
  frames = ((need * BEAT_DENOMINATOR) + ((uint64_t)transport->bpm_x100 * BEAT_NUMERATOR) - 1U) /
           ((uint64_t)transport->bpm_x100 * BEAT_NUMERATOR);
  return (uint32_t)(frames - transport->frames);
}

bool Transport_Fit(uint32_t clip_samples, uint32_t clip_rate_hz, uint8_t steps,
                   uint32_t *bpm_x100, uint8_t *steps_per_beat)
{
  static const uint8_t divisions[] = {4U, 2U, 1U}; /* finest first: it wins a tie */
  const uint64_t numerator = (uint64_t)100U * 60U * steps * clip_rate_hz;
  uint32_t index;

  if ((clip_samples == 0U) || (clip_rate_hz == 0U) || (steps == 0U) || (bpm_x100 == NULL) ||
      (steps_per_beat == NULL))
  {
    return false;
  }
  for (index = 0U; index < (sizeof(divisions) / sizeof(divisions[0])); ++index)
  {
    const uint64_t denominator = (uint64_t)clip_samples * divisions[index];
    const uint64_t tempo = (numerator + (denominator / 2U)) / denominator;

    if ((tempo >= TRANSPORT_FIT_MIN_X100) && (tempo <= TRANSPORT_FIT_MAX_X100))
    {
      *bpm_x100 = (uint32_t)tempo;
      *steps_per_beat = divisions[index];
      return true;
    }
  }
  {
    const uint64_t denominator = (uint64_t)clip_samples * 4U;
    const uint64_t tempo = (numerator + (denominator / 2U)) / denominator;

    *bpm_x100 = ClampTempo((tempo > TRANSPORT_BPM_MAX_X100) ? TRANSPORT_BPM_MAX_X100
                                                            : (uint32_t)tempo);
    *steps_per_beat = 4U;
  }
  return true;
}
