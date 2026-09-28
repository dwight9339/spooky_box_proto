#include "app_events.h"

#include "stm32h7xx_hal.h"

/* Zero-initialized: valid and empty before the first post. */
static EventQueue queue;

bool AppEvents_Post(EvqClass event_class, uint16_t type, uint32_t arg0, uint32_t arg1)
{
  return EventQueue_Post(&queue, event_class, type, arg0, arg1, HAL_GetTick());
}

uint32_t AppEvents_Service(EvqDispatch dispatch, void *context)
{
  return EventQueue_Service(&queue, HAL_GetTick(), dispatch, context);
}

void AppEvents_GetStats(EvqStats *stats)
{
  EventQueue_GetStats(&queue, stats);
}
