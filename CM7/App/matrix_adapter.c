#include "matrix_adapter.h"

#include <stdio.h>
#include <string.h>

#include "emf_level.h"
#include "magnetometer_test.h"
#include "main.h"
#include "matrix_service.h"
#include "radio_activity_feed.h"
#include "radio_recorder.h"
#include "ui_board_test.h"
#include "usb_test.h"

/* Consecutive failed writes before the matrix is given up. */
#define MATRIX_ADAPTER_MAX_FAILURES 3U
/* Runs keep going out in one pass until this much time has been spent, so a
 * whole frame lands well inside a kick (decision 0013 item 8). */
#define MATRIX_ADAPTER_PASS_BUDGET_US 2000U

static bool adapter_ready;
static bool suspended;            /* switched off for a capture */
static uint32_t suspensions;
static uint32_t consecutive_failures;
static uint32_t cycles_per_us;

static bool Start(void);

static uint32_t Now(void)
{
  return HAL_GetTick();
}

void MatrixAdapter_Init(void)
{
  EmfLevelConfig emf;

  EmfLevel_DefaultConfig(&emf);
  adapter_ready = EmfLevel_Init(&emf) && MatrixService_Init(Now());
  suspended = false;
  suspensions = 0U;
  consecutive_failures = 0U;
  /* Write times from the cycle counter. */
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  cycles_per_us = SystemCoreClock / 1000000U;
  if (!adapter_ready)
  {
    printf("[matrix] FAIL: configuration rejected; feedback unavailable\r\n");
  }
#if defined(SPOOKY_MATRIX_RECORDING_QUALIFICATION)
  /* The recording regression flashes and reboots the board, so the
   * qualification image starts the feedback itself. */
  else if (!Start())
  {
    printf("[matrix] FAIL: feedback did not start at boot\r\n");
  }
#endif
}

static void Stop(bool blank, const char *why)
{
  MatrixService_SetEnabled(false, Now());
  MagnetometerTest_SetEmfFeed(false);
  UiBoardTest_MatrixRelease(blank);
  suspended = false;
  if (why != NULL)
  {
    printf("[matrix] feedback off: %s\r\n", why);
  }
}

static bool Start(void)
{
  if (!adapter_ready || !UiBoardTest_MatrixAcquire())
  {
    return false;
  }
  consecutive_failures = 0U;
  suspended = false;
  MagnetometerTest_SetEmfFeed(true);
  MatrixService_SetEnabled(true, Now());
  return true;
}

void MatrixAdapter_Service(void)
{
  MatrixServiceInput input;
  EmfLevelReading emf;
  MatrixWriterRun run;
  uint32_t now;
  uint32_t pass_start;

  if (!MatrixService_IsEnabled())
  {
    return;
  }
  if (!UiBoardTest_MatrixHeld())
  {
    Stop(false, "matrix released (UI OFF)");
    return;
  }
#if !defined(SPOOKY_MATRIX_RECORDING_QUALIFICATION)
  /* No I2C2 I/O while the recorder captures (Principle VI guard): the enable
   * pin is a GPIO, and the whole frame is rewritten afterwards. */
  if (RadioRecorder_IsCapturing())
  {
    if (!suspended)
    {
      UiBoardTest_MatrixPowerOff();
      suspended = true;
      ++suspensions;
    }
    return;
  }
  if (suspended)
  {
    suspended = false;
    if (!UiBoardTest_MatrixAcquire())
    {
      Stop(false, "matrix did not restart after the capture");
      return;
    }
    MatrixService_Invalidate();
  }
#endif

  now = Now();
  (void)EmfLevel_Get(now, &emf);
  input.emf_known = emf.state == EMF_LEVEL_VALID;
  input.emf_bucket = emf.bucket;
  input.onset = RadioActivityFeed_TakeOnset(RADIO_ACTIVITY_READER_MATRIX);
  pass_start = DWT->CYCCNT;
  while (MatrixService_Service(now, &input, &run))
  {
    const uint32_t start = DWT->CYCCNT;
    const bool ok = UiBoardTest_MatrixWriteRun(run.page, run.reg, run.bytes, run.length);

    MatrixService_RunDone(ok, (DWT->CYCCNT - start) / cycles_per_us);
    consecutive_failures = ok ? 0U : (consecutive_failures + 1U);
    if (consecutive_failures >= MATRIX_ADAPTER_MAX_FAILURES)
    {
      Stop(false, "I2C2 writes failed");
      return;
    }
    if (!ok || (((DWT->CYCCNT - pass_start) / cycles_per_us) >= MATRIX_ADAPTER_PASS_BUDGET_US))
    {
      return;
    }
    input.onset = 0U;
  }
}

void MatrixAdapter_OnSessionEvent(SesPublished event)
{
  switch (event)
  {
    case SES_PUB_RECORDING_STARTED:
      MatrixService_OnRecording(true);
      break;
    case SES_PUB_RECORDING_COMPLETED:
    case SES_PUB_RECORDING_FILE_LIMIT:
    case SES_PUB_RECORDING_CANCELLED:
      MatrixService_OnRecording(false);
      break;
    case SES_PUB_RECORDING_ABORTED:
    case SES_PUB_RECORDING_CARD_FULL:
      MatrixService_OnRecordingFault(Now());
      break;
    default:
      break;
  }
}

static const char *EmfStateName(EmfLevelState state)
{
  static const char *const names[] = {
    "VALID", "NO_SAMPLE", "NO_BASELINE", "STALE", "SENSOR_FAULT"
  };

  return ((uint32_t)state < (sizeof(names) / sizeof(names[0]))) ? names[state] : "UNKNOWN";
}

static void SendStatus(void)
{
  MatrixServiceStatus status;
  EmfLevelReading emf;
  char response[256];

  MatrixService_GetStatus(&status);
  (void)EmfLevel_Get(Now(), &emf);
  (void)snprintf(response, sizeof(response),
                 "OK UI MATRIX FEEDBACK=%u TRAIL=%u SUSPENDED=%u EMF=%s BUCKET=%u "
                 "EMF_UT=%lu FRAMES=%lu RUNS=%lu FAILED=%lu SUPERSEDED=%lu "
                 "DROPPED_STEPS=%lu PENDING=%u RUN_US_MAX=%lu SUSPENSIONS=%lu\r\n",
                 status.enabled ? 1U : 0U, status.trail ? 1U : 0U,
                 suspended ? 1U : 0U, EmfStateName(emf.state), (unsigned)emf.bucket,
                 (unsigned long)emf.emf_uT, (unsigned long)status.frames,
                 (unsigned long)status.runs_written, (unsigned long)status.runs_failed,
                 (unsigned long)status.frames_superseded,
                 (unsigned long)status.dropped_steps, (unsigned)status.pending_runs,
                 (unsigned long)status.run_us_max, (unsigned long)suspensions);
  (void)UsbTest_SendText(response);
}

bool MatrixAdapter_HandleCommand(const char *command)
{
  if (strcmp(command, "UI MATRIX FEEDBACK") == 0)
  {
    SendStatus();
    return true;
  }
  if (strcmp(command, "UI MATRIX FEEDBACK ON") == 0)
  {
    if (!MatrixService_IsEnabled() && !Start())
    {
      (void)UsbTest_SendText("ERR UI MATRIX FEEDBACK matrix unavailable or busy\r\n");
      return true;
    }
    SendStatus();
    return true;
  }
  if (strcmp(command, "UI MATRIX FEEDBACK OFF") == 0)
  {
    if (MatrixService_IsEnabled())
    {
      /* Blank first unless the recorder captures: then only the enable pin. */
      Stop(!RadioRecorder_IsCapturing(), NULL);
    }
    SendStatus();
    return true;
  }
  if ((strcmp(command, "UI MATRIX TRAIL ON") == 0) ||
      (strcmp(command, "UI MATRIX TRAIL OFF") == 0))
  {
    MatrixService_SetTrail(strcmp(command, "UI MATRIX TRAIL ON") == 0);
    SendStatus();
    return true;
  }
  if ((strncmp(command, "UI MATRIX FEEDBACK", 18U) == 0) ||
      (strncmp(command, "UI MATRIX TRAIL", 15U) == 0))
  {
    (void)UsbTest_SendText(
      "ERR usage: UI MATRIX FEEDBACK [ON|OFF] | UI MATRIX TRAIL ON|OFF\r\n");
    return true;
  }
  return false;
}
