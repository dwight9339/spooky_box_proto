#include "foreground_budget.h"

uint32_t ForegroundBudget_Milliseconds(ForegroundService service)
{
  if (service == FOREGROUND_SERVICE_LOOP)
  {
    return FOREGROUND_LOOP_BUDGET_MS;
  }
  if (service == FOREGROUND_SERVICE_RECORDER)
  {
    return FOREGROUND_RECORDER_BUDGET_MS;
  }
  return FOREGROUND_AUX_BUDGET_MS;
}

const char *ForegroundBudget_Name(ForegroundService service)
{
  static const char *const names[FOREGROUND_SERVICE_COUNT] = {
    "LOOP", "IPC", "AUDIO", "RECORDER", "FUEL", "USB", "WAV",
    "LOGGER", "DIAGNOSTICS", "UI", "SD_TEST", "POWER", "MAG", "DISPATCH",
    "CLASSIC", "ACTIVITY", "MATRIX",
#if defined(SPOOKY_DEMO)
    "DEMO"
#endif
  };
  return ((uint32_t)service < FOREGROUND_SERVICE_COUNT) ? names[service] : "UNKNOWN";
}
