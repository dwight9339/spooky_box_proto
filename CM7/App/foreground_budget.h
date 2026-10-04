#ifndef SPOOKY_FOREGROUND_BUDGET_H
#define SPOOKY_FOREGROUND_BUDGET_H

#include <stdint.h>

/* A recorder block arrives every 4096 / 48000 = 85.33 ms.  The aggregate
 * foreground budget stays below one block period; the recorder receives most
 * of that interval and every auxiliary service must return incrementally. */
#define FOREGROUND_RECORDER_BLOCK_MS 86U
#define FOREGROUND_LOOP_BUDGET_MS    75U
#define FOREGROUND_AUX_BUDGET_MS     10U
#define FOREGROUND_RECORDER_BUDGET_MS 70U

typedef enum
{
  FOREGROUND_SERVICE_LOOP = 0,
  FOREGROUND_SERVICE_IPC,
  FOREGROUND_SERVICE_AUDIO,
  FOREGROUND_SERVICE_RECORDER,
  FOREGROUND_SERVICE_FUEL,
  FOREGROUND_SERVICE_USB,
  FOREGROUND_SERVICE_WAV,
  FOREGROUND_SERVICE_LOGGER,
  FOREGROUND_SERVICE_DIAGNOSTICS,
  FOREGROUND_SERVICE_UI,
  FOREGROUND_SERVICE_SD_TEST,
  FOREGROUND_SERVICE_POWER,
  FOREGROUND_SERVICE_MAGNETOMETER,
  FOREGROUND_SERVICE_DISPATCH,
  FOREGROUND_SERVICE_CLASSIC,
  FOREGROUND_SERVICE_ACTIVITY,
  FOREGROUND_SERVICE_MATRIX,
#if defined(SPOOKY_DEMO)
  FOREGROUND_SERVICE_DEMO,     /* demo_field.c: display page, lights, ticks */
#endif
  FOREGROUND_SERVICE_COUNT
} ForegroundService;

uint32_t ForegroundBudget_Milliseconds(ForegroundService service);
const char *ForegroundBudget_Name(ForegroundService service);

#endif
