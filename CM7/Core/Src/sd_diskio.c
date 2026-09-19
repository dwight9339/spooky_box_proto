#include "diskio.h"
#include "main.h"
#include "sd_diskio.h"

#include <stdbool.h>

#define SD_BLOCK_SIZE              512U
#define SD_TRANSFER_TIMEOUT_MS   30000U

static volatile DSTATUS sd_status = STA_NOINIT;

static bool SdWaitReady(uint32_t timeout_ms)
{
  uint32_t start = HAL_GetTick();

  do
  {
    HAL_SD_CardStateTypeDef state = HAL_SD_GetCardState(&hsd1);

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

void SdDiskIo_Reset(void)
{
  sd_status = STA_NOINIT;
}

DSTATUS disk_initialize(BYTE pdrv)
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

  if (HAL_SD_GetState(&hsd1) != HAL_SD_STATE_RESET)
  {
    (void)HAL_SD_DeInit(&hsd1);
  }

  if ((HAL_SD_Init(&hsd1) != HAL_OK) ||
      !SdWaitReady(SD_TRANSFER_TIMEOUT_MS))
  {
    sd_status = STA_NOINIT;
    return sd_status;
  }

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
      if (buff == NULL)
      {
        return RES_PARERR;
      }
      *(DWORD *)buff = 1U;
      return RES_OK;

    default:
      return RES_PARERR;
  }
}
#endif
