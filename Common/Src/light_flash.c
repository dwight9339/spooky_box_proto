#include "light_flash.h"

#include <stddef.h>

void LightFlash_DefaultConfig(LightFlashConfig *config)
{
  if (config == NULL)
  {
    return;
  }
  config->boost_permille[0] = 333U;
  config->boost_permille[1] = 667U;
  config->boost_permille[2] = LIGHT_FLASH_FULL;
  config->full_decay_ms = 240U;
}

bool LightFlash_Init(LightFlash *flash, const LightFlashConfig *config)
{
  uint32_t size;

  if ((flash == NULL) || (config == NULL) || (config->full_decay_ms == 0U))
  {
    return false;
  }
  for (size = 0U; size < LIGHT_FLASH_ONSET_SIZES; ++size)
  {
    const uint16_t boost = config->boost_permille[size];

    if ((boost == 0U) || (boost > LIGHT_FLASH_FULL) ||
        ((size > 0U) && (boost < config->boost_permille[size - 1U])))
    {
      return false;
    }
  }
  flash->config = *config;
  flash->start_boost = 0U;
  flash->start_ms = 0U;
  flash->active = false;
  return true;
}

uint16_t LightFlash_Boost(LightFlash *flash, uint32_t now_ms)
{
  uint32_t elapsed;
  uint32_t fallen;

  if ((flash == NULL) || !flash->active)
  {
    return 0U;
  }
  elapsed = now_ms - flash->start_ms;
  if (elapsed >= flash->config.full_decay_ms)
  {
    flash->active = false;
    return 0U;
  }
  fallen = ((elapsed * LIGHT_FLASH_FULL) + (flash->config.full_decay_ms / 2U)) /
           flash->config.full_decay_ms;
  if (fallen >= flash->start_boost)
  {
    flash->active = false;
    return 0U;
  }
  return (uint16_t)(flash->start_boost - fallen);
}

void LightFlash_OnOnset(LightFlash *flash, uint8_t size, uint32_t now_ms)
{
  uint16_t boost;

  if ((flash == NULL) || (size == 0U) || (size > LIGHT_FLASH_ONSET_SIZES))
  {
    return;
  }
  boost = flash->config.boost_permille[size - 1U];
  if (boost < LightFlash_Boost(flash, now_ms))
  {
    return;
  }
  flash->start_boost = boost;
  flash->start_ms = now_ms;
  flash->active = true;
}

uint16_t LightFlash_Apply(uint16_t lightness_permille, uint16_t boost_permille)
{
  const uint32_t lightness = (lightness_permille > LIGHT_FLASH_FULL)
    ? LIGHT_FLASH_FULL : lightness_permille;
  const uint32_t boost = (boost_permille > LIGHT_FLASH_FULL)
    ? LIGHT_FLASH_FULL : boost_permille;

  return (uint16_t)(lightness +
                    ((((LIGHT_FLASH_FULL - lightness) * boost) + (LIGHT_FLASH_FULL / 2U)) /
                     LIGHT_FLASH_FULL));
}
