#include "sd_media.h"

#include <stdio.h>

#define SD_MEDIA_MAX_ALIGN_SECTORS 32768U /* 16 MiB */

bool SdMedia_RecordingSupported(uint32_t card_type)
{
  return card_type == SD_MEDIA_CARD_SDHC_SDXC;
}

const char *SdMedia_CardTypeName(uint32_t card_type)
{
  return (card_type == SD_MEDIA_CARD_SDHC_SDXC) ? "SDHC/SDXC" :
         (card_type == SD_MEDIA_CARD_SDSC) ? "SDSC" : "OTHER";
}

uint32_t SdMedia_AuKib(uint8_t au_size_code)
{
  /* SD Physical Layer AU_SIZE: 1..9 are 16 KiB << (code - 1); A..F are 8, 12,
   * 16, 24, 32 and 64 MiB. */
  static const uint32_t au_kib[16] = {
    0U, 16U, 32U, 64U, 128U, 256U, 512U, 1024U, 2048U, 4096U,
    8192U, 12288U, 16384U, 24576U, 32768U, 65536U
  };
  return (au_size_code < 16U) ? au_kib[au_size_code] : 0U;
}

uint32_t SdMedia_AlignSectors(uint8_t au_size_code)
{
  uint32_t sectors = SdMedia_AuKib(au_size_code) * 2U;

  if (sectors == 0U)
  {
    return 1U;
  }
  sectors &= (~sectors + 1U); /* Largest power of two dividing the AU. */
  return (sectors > SD_MEDIA_MAX_ALIGN_SECTORS) ? SD_MEDIA_MAX_ALIGN_SECTORS : sectors;
}

uint32_t SdMedia_SpeedClass(uint8_t speed_class_code)
{
  static const uint32_t classes[5] = {0U, 2U, 4U, 6U, 10U};
  return (speed_class_code < 5U) ? classes[speed_class_code] : 0U;
}

static char Printable(uint32_t value)
{
  value &= 0xFFU;
  return ((value >= 0x20U) && (value < 0x7FU) && (value != (uint32_t)' ')) ?
    (char)value : '?';
}

size_t SdMedia_FormatInfo(const SdMediaInfo *info, char *out, size_t size)
{
  int length;

  if ((info == NULL) || (out == NULL) || (size == 0U))
  {
    return 0U;
  }
  length = snprintf(out, size,
    "OK SD INFO TYPE=%s SUPPORTED=%u CAPACITY=%luMiB MID=0x%02X OID=%c%c "
    "PNM=%c%c%c%c%c PRV=%u.%u PSN=0x%08lX MDT=%04u-%02u SPEED_CLASS=%lu "
    "UHS_GRADE=%u VIDEO_CLASS=%u AU=%luKiB\r\n",
    SdMedia_CardTypeName(info->card_type),
    SdMedia_RecordingSupported(info->card_type) ? 1U : 0U,
    (unsigned long)(info->sectors / 2048U),
    (unsigned int)info->manufacturer_id,
    Printable((uint32_t)info->oem_id >> 8), Printable(info->oem_id),
    Printable(info->product_name1 >> 24), Printable(info->product_name1 >> 16),
    Printable(info->product_name1 >> 8), Printable(info->product_name1),
    Printable(info->product_name2),
    (unsigned int)(info->product_revision >> 4),
    (unsigned int)(info->product_revision & 0x0FU),
    (unsigned long)info->serial,
    2000U + ((unsigned int)(info->manufacture_date >> 4) & 0xFFU),
    (unsigned int)(info->manufacture_date & 0x0FU),
    (unsigned long)SdMedia_SpeedClass(info->speed_class_code),
    (unsigned int)info->uhs_speed_grade, (unsigned int)info->video_speed_class,
    (unsigned long)SdMedia_AuKib(info->au_size_code));
  if ((length <= 0) || ((size_t)length >= size))
  {
    out[0] = '\0';
    return 0U;
  }
  return (size_t)length;
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
