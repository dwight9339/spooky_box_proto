#ifndef SPOOKY_BOARD_DIAGNOSTICS_H
#define SPOOKY_BOARD_DIAGNOSTICS_H
#include <stdbool.h>

/* UART7 remains owned by M7 until a separately tested ownership transfer. */
bool BoardDiagnostics_StartConsole(void);
void BoardDiagnostics_SendBatteryStatus(bool charging_only);
#endif
