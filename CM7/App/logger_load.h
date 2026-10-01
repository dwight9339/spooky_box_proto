#ifndef SPOOKY_LOGGER_LOAD_H
#define SPOOKY_LOGGER_LOAD_H

/*
 * Bench-only logger load and UART fault injection for jjy.6, compiled only with
 * SPOOKY_LOGGER_LOAD_QUALIFICATION. Foreground only. Each load line goes through
 * TargetLogger_Write so rejected writes and bytes can be matched exactly against
 * the logger counters.
 */

#include <stdbool.h>

/* Input is the existing CLI's uppercase, trimmed command. */
bool LoggerLoad_HandleCommand(const char *command);
void LoggerLoad_Service(void);

#endif /* SPOOKY_LOGGER_LOAD_H */
