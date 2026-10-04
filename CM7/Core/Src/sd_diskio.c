#include "diskio.h"
#include "main.h"
#include "sd_diskio.h"
#include "sd_media.h"

#include <stdbool.h>
#include <stdio.h>

#define SD_BLOCK_SIZE              512U
#define SD_TRANSFER_TIMEOUT_MS   30000U
/* A card that completed initialization is selected straight into the transfer
 * state; one that is still not there after this is treated as unusable rather
 * than waiting the full transfer timeout (jjy.15). */
#define SD_INIT_READY_TIMEOUT_MS  1000U

static volatile DSTATUS sd_status = STA_NOINIT;
static SdInitReport sd_init_report;

static bool SdWaitReady(uint32_t timeout_ms)
{
  uint32_t start = HAL_GetTick();

  do
  {
    HAL_SD_CardStateTypeDef state = HAL_SD_GetCardState(&hsd1);

    sd_init_report.card_state = (uint32_t)state;
    if (state == HAL_SD_CARD_TRANSFER)
    {
      return true;
    }
    if ((state == HAL_SD_CARD_ERROR) || !SD_CARD_IS_PRESENT())
    {
      return false;
    }
  } while ((HAL_GetTick() - start) < timeout_ms);

  return false;
}

void SdDiskIo_GetInitReport(SdInitReport *report)
{
  if (report != NULL)
  {
    *report = sd_init_report;
  }
}

const char *SdDiskIo_InitStageName(SdInitStage stage)
{
  static const char *const names[] = {"OK", "NO_CARD", "HAL_INIT", "NOT_READY"};
  return ((unsigned int)stage < (sizeof(names) / sizeof(names[0])))
    ? names[(unsigned int)stage] : "UNKNOWN";
}

const char *SdDiskIo_InitDetail(char *buffer, unsigned int size)
{
  if ((buffer == NULL) || (size == 0U))
  {
    return "";
  }
  (void)snprintf(buffer, size, "stage=%s hal=0x%08lX state=%lu init_ms=%lu ready_ms=%lu",
                 SdDiskIo_InitStageName(sd_init_report.stage),
                 (unsigned long)sd_init_report.hal_error,
                 (unsigned long)sd_init_report.card_state,
                 (unsigned long)sd_init_report.init_ms,
                 (unsigned long)sd_init_report.ready_ms);
  return buffer;
}

void SdDiskIo_Reset(void)
{
  sd_status = STA_NOINIT;
}

DSTATUS disk_initialize(BYTE pdrv)
{
  uint32_t start;

  if (pdrv != 0U)
  {
    return STA_NOINIT;
  }

  sd_init_report = (SdInitReport){0};
  if (!SD_CARD_IS_PRESENT())
  {
    sd_init_report.stage = SD_INIT_NO_CARD;
    sd_status = STA_NOINIT | STA_NODISK;
    return sd_status;
  }

  if (HAL_SD_GetState(&hsd1) != HAL_SD_STATE_RESET)
  {
    (void)HAL_SD_DeInit(&hsd1);
  }

  start = HAL_GetTick();
  if (HAL_SD_Init(&hsd1) != HAL_OK)
  {
    sd_init_report.stage = SD_INIT_HAL_FAILED;
    sd_init_report.hal_error = HAL_SD_GetError(&hsd1);
    sd_init_report.init_ms = HAL_GetTick() - start;
    sd_status = STA_NOINIT;
    return sd_status;
  }
  sd_init_report.init_ms = HAL_GetTick() - start;
  start = HAL_GetTick();
  if (!SdWaitReady(SD_INIT_READY_TIMEOUT_MS))
  {
    sd_init_report.stage = SD_INIT_NOT_READY;
    sd_init_report.hal_error = HAL_SD_GetError(&hsd1);
    sd_init_report.ready_ms = HAL_GetTick() - start;
    sd_status = STA_NOINIT;
    return sd_status;
  }
  sd_init_report.ready_ms = HAL_GetTick() - start;

  sd_status = 0U;
  return sd_status;
}

DSTATUS disk_status(BYTE pdrv)
{
  if (pdrv != 0U)
  {
    return STA_NOINIT;
  }
  if (!SD_CARD_IS_PRESENT())
  {
    sd_status = STA_NOINIT | STA_NODISK;
    return sd_status;
  }
  if (((sd_status & STA_NOINIT) == 0U) && !SdWaitReady(100U))
  {
    sd_status = STA_NOINIT;
  }
  return sd_status;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count)
{
  if ((pdrv != 0U) || (buff == NULL) || (count == 0U))
  {
    return RES_PARERR;
  }
  if ((disk_status(pdrv) & STA_NOINIT) != 0U)
  {
    return RES_NOTRDY;
  }
  if (HAL_SD_ReadBlocks(&hsd1, buff, sector, count,
                        SD_TRANSFER_TIMEOUT_MS) != HAL_OK)
  {
    return RES_ERROR;
  }
  return SdWaitReady(SD_TRANSFER_TIMEOUT_MS) ? RES_OK : RES_ERROR;
}

#if _USE_WRITE == 1
DRESULT disk_write(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count)
{
  if ((pdrv != 0U) || (buff == NULL) || (count == 0U))
  {
    return RES_PARERR;
  }
  if ((disk_status(pdrv) & STA_NOINIT) != 0U)
  {
    return RES_NOTRDY;
  }
  if (HAL_SD_WriteBlocks(&hsd1, buff, sector, count,
                         SD_TRANSFER_TIMEOUT_MS) != HAL_OK)
  {
    return RES_ERROR;
  }
  return SdWaitReady(SD_TRANSFER_TIMEOUT_MS) ? RES_OK : RES_ERROR;
}
#endif

#if _USE_IOCTL == 1
DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
  HAL_SD_CardInfoTypeDef info;

  if (pdrv != 0U)
  {
    return RES_PARERR;
  }
  if ((disk_status(pdrv) & STA_NOINIT) != 0U)
  {
    return RES_NOTRDY;
  }

  switch (cmd)
  {
    case CTRL_SYNC:
      return SdWaitReady(SD_TRANSFER_TIMEOUT_MS) ? RES_OK : RES_ERROR;

    case GET_SECTOR_COUNT:
      if ((buff == NULL) || (HAL_SD_GetCardInfo(&hsd1, &info) != HAL_OK))
      {
        return RES_ERROR;
      }
      *(DWORD *)buff = info.LogBlockNbr;
      return RES_OK;

    case GET_SECTOR_SIZE:
      if (buff == NULL)
      {
        return RES_PARERR;
      }
      *(WORD *)buff = SD_BLOCK_SIZE;
      return RES_OK;

    case GET_BLOCK_SIZE:
    {
      HAL_SD_CardStatusTypeDef card_status;

      if (buff == NULL)
      {
        return RES_PARERR;
      }
      /* Erase-block alignment for f_mkfs: the card's allocation unit. */
      *(DWORD *)buff = (HAL_SD_GetCardStatus(&hsd1, &card_status) == HAL_OK)
        ? SdMedia_AlignSectors(card_status.AllocationUnitSize) : 1U;
      return RES_OK;
    }

    default:
      return RES_PARERR;
  }
}
#endif
