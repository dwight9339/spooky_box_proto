#ifndef SPOOKY_BOARD_DIAGNOSTICS_H
#define SPOOKY_BOARD_DIAGNOSTICS_H
#include <stdbool.h>

/* UART7 remains owned by M7 until a separately tested ownership transfer. */
bool BoardDiagnostics_StartConsole(void);
/* allow_bus_io is false while recording: the reply then uses the last fuel-gauge
 * snapshot and reports its age. */
void BoardDiagnostics_SendBatteryStatus(bool charging_only, bool allow_bus_io);
#endif
