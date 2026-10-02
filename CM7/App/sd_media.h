#ifndef SPOOKY_SD_MEDIA_H
#define SPOOKY_SD_MEDIA_H

/* Portable SD media helpers for the in-device format (jjy.14). */

#include <stdbool.h>
#include <stdint.h>

#define SD_FORMAT_CONFIRM_WINDOW_MS 10000U

/* FatFs alignment, in 512-byte sectors, for the SD Status AU_SIZE code: the
 * largest power of two that divides the allocation unit, at most 16 MiB (the
 * FatFs R0.12c limit). Codes 0 (undefined) and out-of-range give 1 (no
 * alignment). */
uint32_t SdMedia_AlignSectors(uint8_t au_size_code);

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
