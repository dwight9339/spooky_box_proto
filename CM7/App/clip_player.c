#include "clip_player.h"

#include <stddef.h>

void ClipPlayer_Init(ClipPlayer *player)
{
  if (player == NULL)
  {
    return;
  }
  player->active = false;
  player->samples = NULL;
  player->count = 0U;
  player->position = 0U;
  player->loops = 0U;
}

bool ClipPlayer_Start(ClipPlayer *player, const int16_t *samples, uint32_t count)
{
  if (player == NULL)
  {
    return false;
  }
  player->active = false;
  if ((samples == NULL) || (count == 0U))
  {
    return false;
  }
  player->samples = samples;
  player->count = count;
  player->position = 0U;
  player->loops = 0U;
  player->active = true;
  return true;
}

void ClipPlayer_Stop(ClipPlayer *player)
{
  if (player != NULL)
  {
    player->active = false;
  }
}

bool ClipPlayer_Active(const ClipPlayer *player)
{
  return (player != NULL) && player->active;
}

bool ClipPlayer_Render(ClipPlayer *player, uint16_t *stereo, uint32_t frame_count)
{
  const int16_t *samples;
  uint32_t count;
  uint32_t position;
  uint32_t loops;
  uint32_t frame;

  if ((player == NULL) || (stereo == NULL) || !player->active)
  {
    return false;
  }
  samples = player->samples;
  count = player->count;
  position = player->position;
  loops = player->loops;
  if ((samples == NULL) || (count == 0U))
  {
    return false;
  }
  for (frame = 0U; frame < frame_count; ++frame)
  {
    const uint32_t index = position / 2U;
    int32_t value = samples[index];

    if ((position & 1U) != 0U)
    {
      const uint32_t next = (index + 1U == count) ? 0U : index + 1U;

      value = (value + (int32_t)samples[next]) / 2;
    }
    stereo[2U * frame] = (uint16_t)(int16_t)value;
    stereo[(2U * frame) + 1U] = (uint16_t)(int16_t)value;
    if (++position == (2U * count))
    {
      position = 0U;
      ++loops;
    }
  }
  player->position = position;
  player->loops = loops;
  return true;
}
