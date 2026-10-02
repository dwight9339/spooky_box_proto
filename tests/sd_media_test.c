/*
 * Host tests for the SD media helpers behind SD FORMAT (jjy.14): allocation-unit
 * alignment for f_mkfs and the arm/confirm window. Host results only; not
 * hardware evidence.
 */

#include <stdio.h>

#include "sd_media.h"

static int failures;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            printf("FAIL line %d: %s\n", __LINE__, #cond);                       \
            ++failures;                                                          \
        }                                                                        \
    } while (0)

static void TestAlignment(void)
{
  CHECK(SdMedia_AlignSectors(0U) == 1U);      /* Undefined AU: no alignment. */
  CHECK(SdMedia_AlignSectors(1U) == 32U);     /* 16 KiB */
  CHECK(SdMedia_AlignSectors(9U) == 8192U);   /* 4 MiB, typical SDHC */
  CHECK(SdMedia_AlignSectors(10U) == 16384U); /* 8 MiB */
  CHECK(SdMedia_AlignSectors(11U) == 8192U);  /* 12 MiB: 4 MiB divides it */
  CHECK(SdMedia_AlignSectors(12U) == 32768U); /* 16 MiB */
  CHECK(SdMedia_AlignSectors(13U) == 16384U); /* 24 MiB: 8 MiB divides it */
  CHECK(SdMedia_AlignSectors(14U) == 32768U); /* 32 MiB capped at 16 MiB */
  CHECK(SdMedia_AlignSectors(15U) == 32768U); /* 64 MiB capped at 16 MiB */
  CHECK(SdMedia_AlignSectors(16U) == 1U);
  for (unsigned int code = 0U; code < 32U; ++code)
  {
    uint32_t sectors = SdMedia_AlignSectors((uint8_t)code);
    CHECK((sectors != 0U) && ((sectors & (sectors - 1U)) == 0U));
    CHECK(sectors <= 32768U);
  }
}

static void TestConfirmWindow(void)
{
  SdFormatArm arm = {false, 0U};

  CHECK(!SdFormatArm_Confirm(&arm, 0U)); /* Not armed. */
  SdFormatArm_Arm(&arm, 1000U);
  CHECK(SdFormatArm_Confirm(&arm, 1000U + SD_FORMAT_CONFIRM_WINDOW_MS));
  CHECK(!SdFormatArm_Confirm(&arm, 1001U)); /* Consumed. */

  SdFormatArm_Arm(&arm, 1000U);
  CHECK(!SdFormatArm_Confirm(&arm, 1001U + SD_FORMAT_CONFIRM_WINDOW_MS));
  CHECK(!SdFormatArm_Confirm(&arm, 1002U)); /* An expired confirm also disarms. */

  /* The window survives the millisecond tick wrapping. */
  SdFormatArm_Arm(&arm, 0xFFFFFF00U);
  CHECK(SdFormatArm_Confirm(&arm, 0x00000100U));

  SdFormatArm_Arm(NULL, 0U);
  CHECK(!SdFormatArm_Confirm(NULL, 0U));
}

int main(void)
{
  TestAlignment();
  TestConfirmWindow();
  if (failures != 0)
  {
    printf("sd_media_test: %d failure(s)\n", failures);
    return 1;
  }
  puts("sd_media_test: all tests passed");
  return 0;
}
