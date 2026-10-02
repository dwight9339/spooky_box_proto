#include "sd_media.h"

#include <stddef.h>

#define SD_MEDIA_MAX_ALIGN_SECTORS 32768U /* 16 MiB */

uint32_t SdMedia_AlignSectors(uint8_t au_size_code)
{
  /* SD Physical Layer AU_SIZE: 1..9 are 16 KiB << (code - 1); A..F are 8, 12,
   * 16, 24, 32 and 64 MiB. */
  static const uint32_t au_kib[16] = {
    0U, 16U, 32U, 64U, 128U, 256U, 512U, 1024U, 2048U, 4096U,
    8192U, 12288U, 16384U, 24576U, 32768U, 65536U
  };
  uint32_t sectors;

  if ((au_size_code == 0U) || (au_size_code >= 16U))
  {
    return 1U;
  }
  sectors = au_kib[au_size_code] * 2U;
  sectors &= (~sectors + 1U); /* Largest power of two dividing the AU. */
  return (sectors > SD_MEDIA_MAX_ALIGN_SECTORS) ? SD_MEDIA_MAX_ALIGN_SECTORS : sectors;
}

void SdFormatArm_Arm(SdFormatArm *arm, uint32_t now_ms)
{
  if (arm != NULL)
  {
    arm->armed = true;
    arm->armed_ms = now_ms;
  }
}

bool SdFormatArm_Confirm(SdFormatArm *arm, uint32_t now_ms)
{
  bool confirmed;

  if (arm == NULL)
  {
    return false;
  }
  confirmed = arm->armed && ((now_ms - arm->armed_ms) <= SD_FORMAT_CONFIRM_WINDOW_MS);
  arm->armed = false;
  return confirmed;
}
