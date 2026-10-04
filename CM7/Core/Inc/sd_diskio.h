#ifndef SD_DISKIO_H
#define SD_DISKIO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* The last disk_initialize outcome, kept because a failed mount deinitializes
 * the HAL handle and clears its error code. */
typedef enum
{
  SD_INIT_OK = 0,
  SD_INIT_NO_CARD,
  SD_INIT_HAL_FAILED, /* HAL_SD_Init (identification, ACMD41, select) failed. */
  SD_INIT_NOT_READY   /* Initialized, but never reached the transfer state. */
} SdInitStage;

typedef struct
{
  SdInitStage stage;
  uint32_t hal_error;  /* HAL_SD_GetError at the failure. */
  uint32_t card_state; /* Last HAL_SD_GetCardState, 0xFF for a failed CMD13. */
  uint32_t init_ms;    /* HAL_SD_Init duration. */
  uint32_t ready_ms;   /* Wait for the transfer state. */
} SdInitReport;

void SdDiskIo_Reset(void);
void SdDiskIo_GetInitReport(SdInitReport *report);
const char *SdDiskIo_InitStageName(SdInitStage stage);
/* Formats the last init report as "stage=... hal=0x... state=... init_ms=...
 * ready_ms=..." for error replies. */
const char *SdDiskIo_InitDetail(char *buffer, unsigned int size);

#ifdef __cplusplus
}
#endif

#endif
