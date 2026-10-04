#ifndef SPOOKY_MATRIX_ADAPTER_H
#define SPOOKY_MATRIX_ADAPTER_H

/*
 * Firmware wiring of the decision 0013 matrix (full_spooky_proto-54w.8). It
 * feeds matrix_service the EMF level (magnetometer, every 100 ms), the radio
 * onsets (radio_activity_feed) and the published Session events, and writes at
 * most one register run per foreground pass to the IS31FL3741 on I2C2, timing
 * each write.
 *
 * Off until UI MATRIX FEEDBACK ON. While the recorder captures, normal images
 * do no I2C2 I/O: the matrix is switched off through its enable pin and
 * rewritten in full after the capture. Only the opt-in
 * SPOOKY_MATRIX_RECORDING_QUALIFICATION build keeps it running during capture,
 * for the bench qualification. Foreground only.
 */

#include <stdbool.h>

#include "sm/session_port.h"

void MatrixAdapter_Init(void);
void MatrixAdapter_Service(void);
/* UI MATRIX FEEDBACK [ON|OFF] and UI MATRIX TRAIL ON|OFF, already upper-case
 * and past the command policy. False if the command is not one of them. */
bool MatrixAdapter_HandleCommand(const char *command);
void MatrixAdapter_OnSessionEvent(SesPublished event);

#endif /* SPOOKY_MATRIX_ADAPTER_H */
