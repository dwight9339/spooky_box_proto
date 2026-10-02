/*
 * Host tests for the SD media helpers behind SD FORMAT (jjy.14): allocation-unit
 * alignment for f_mkfs and the arm/confirm window. Host results only; not
 * hardware evidence.
 */

#include <stdio.h>
#include <string.h>

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

static void TestCardTypesAndRatings(void)
{
  CHECK(SdMedia_RecordingSupported(SD_MEDIA_CARD_SDHC_SDXC));
  CHECK(!SdMedia_RecordingSupported(SD_MEDIA_CARD_SDSC));
  CHECK(!SdMedia_RecordingSupported(3U)); /* HAL CARD_SECURED */
  CHECK(strcmp(SdMedia_CardTypeName(SD_MEDIA_CARD_SDSC), "SDSC") == 0);
  CHECK(strcmp(SdMedia_CardTypeName(SD_MEDIA_CARD_SDHC_SDXC), "SDHC/SDXC") == 0);
  CHECK(strcmp(SdMedia_CardTypeName(7U), "OTHER") == 0);
  CHECK(SdMedia_AuKib(0U) == 0U);
  CHECK(SdMedia_AuKib(9U) == 4096U);
  CHECK(SdMedia_AuKib(15U) == 65536U);
  CHECK(SdMedia_AuKib(16U) == 0U);
  CHECK(SdMedia_SpeedClass(0U) == 0U);
  CHECK(SdMedia_SpeedClass(4U) == 10U);
  CHECK(SdMedia_SpeedClass(5U) == 0U);
}

static void TestInfoLine(void)
{
  SdMediaInfo info;
  char line[256];
  char small[32];

  (void)memset(&info, 0, sizeof(info));
  info.card_type = SD_MEDIA_CARD_SDHC_SDXC;
  info.sectors = 31116288U;
  info.manufacturer_id = 0x03U;
  info.oem_id = 0x5344U; /* "SD" */
  info.product_name1 = 0x53433136U; /* "SC16" */
  info.product_name2 = (uint8_t)'G';
  info.product_revision = 0x80U;
  info.serial = 0x12345678U;
  info.manufacture_date = (uint16_t)((21U << 4) | 7U);
  info.speed_class_code = 4U;
  info.uhs_speed_grade = 1U;
  info.video_speed_class = 10U;
  info.au_size_code = 9U;
  CHECK(SdMedia_FormatInfo(&info, line, sizeof(line)) == strlen(line));
  CHECK(strcmp(line, "OK SD INFO TYPE=SDHC/SDXC SUPPORTED=1 CAPACITY=15193MiB MID=0x03 "
                     "OID=SD PNM=SC16G PRV=8.0 PSN=0x12345678 MDT=2021-07 SPEED_CLASS=10 "
                     "UHS_GRADE=1 VIDEO_CLASS=10 AU=4096KiB\r\n") == 0);

  /* SDSC is reported as unsupported; unprintable identity bytes become '?'. */
  info.card_type = SD_MEDIA_CARD_SDSC;
  info.oem_id = 0x0020U;
  info.product_name1 = 0x00FFFFFFU;
  CHECK(SdMedia_FormatInfo(&info, line, sizeof(line)) != 0U);
  CHECK(strstr(line, "TYPE=SDSC SUPPORTED=0 ") != NULL);
  CHECK(strstr(line, " OID=?? PNM=????G ") != NULL);

  /* Worst-case field widths still fit the 256-byte reply buffer. */
  (void)memset(&info, 0xFF, sizeof(info));
  CHECK(SdMedia_FormatInfo(&info, line, sizeof(line)) != 0U);
  CHECK(SdMedia_FormatInfo(&info, small, sizeof(small)) == 0U);
  CHECK(small[0] == '\0');
  CHECK(SdMedia_FormatInfo(NULL, line, sizeof(line)) == 0U);
}

int main(void)
{
  TestAlignment();
  TestCardTypesAndRatings();
  TestInfoLine();
  TestConfirmWindow();
  if (failures != 0)
  {
    printf("sd_media_test: %d failure(s)\n", failures);
    return 1;
  }
  puts("sd_media_test: all tests passed");
  return 0;
}
