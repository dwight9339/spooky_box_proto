#ifndef SPOOKY_SD_MEDIA_H
#define SPOOKY_SD_MEDIA_H

/* Portable SD media helpers: in-device format (jjy.14) and the media policy of
 * decision 0014 (supported card types, SD INFO). */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SD_FORMAT_CONFIRM_WINDOW_MS 10000U

/* Card types, with the values of the STM32 HAL CARD_SDSC and CARD_SDHC_SDXC. */
#define SD_MEDIA_CARD_SDSC      0U
#define SD_MEDIA_CARD_SDHC_SDXC 1U

/* Decision 0014: only SDHC/SDXC cards record or format. */
bool SdMedia_RecordingSupported(uint32_t card_type);
const char *SdMedia_CardTypeName(uint32_t card_type);

/* SD Status AU_SIZE code to KiB; 0 when undefined or out of range. */
uint32_t SdMedia_AuKib(uint8_t au_size_code);
/* FatFs alignment, in 512-byte sectors, for the SD Status AU_SIZE code: the
 * largest power of two that divides the allocation unit, at most 16 MiB (the
 * FatFs R0.12c limit). Codes 0 (undefined) and out-of-range give 1 (no
 * alignment). */
uint32_t SdMedia_AlignSectors(uint8_t au_size_code);
/* SD Status SPEED_CLASS code to the class number (0, 2, 4, 6 or 10); 0 when
 * reserved. */
uint32_t SdMedia_SpeedClass(uint8_t speed_class_code);

/* Card identity (CID) and ratings (SD Status) for SD INFO. */
typedef struct SdMediaInfo
{
  uint32_t card_type;
  uint32_t sectors;
  uint8_t manufacturer_id;
  uint16_t oem_id;        /* Two ASCII characters. */
  uint32_t product_name1; /* First four ASCII characters, most significant first. */
  uint8_t product_name2;  /* Fifth character. */
  uint8_t product_revision;
  uint32_t serial;
  uint16_t manufacture_date; /* CID MDT: (year - 2000) << 4 | month. */
  uint8_t speed_class_code;
  uint8_t uhs_speed_grade;
  uint8_t video_speed_class;
  uint8_t au_size_code;
} SdMediaInfo;

/* Writes the CRLF-terminated OK SD INFO reply; returns its length, or 0 if it
 * does not fit. Non-printable identity characters are shown as '?'. */
size_t SdMedia_FormatInfo(const SdMediaInfo *info, char *out, size_t size);

/* Two-step confirmation for a destructive format: Arm, then Confirm within
 * SD_FORMAT_CONFIRM_WINDOW_MS. Confirm consumes the arm either way. */
typedef struct SdFormatArm
{
  bool armed;
  uint32_t armed_ms;
} SdFormatArm;

void SdFormatArm_Arm(SdFormatArm *arm, uint32_t now_ms);
bool SdFormatArm_Confirm(SdFormatArm *arm, uint32_t now_ms);

#endif /* SPOOKY_SD_MEDIA_H */
