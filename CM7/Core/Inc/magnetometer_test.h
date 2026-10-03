#ifndef MAGNETOMETER_TEST_H
#define MAGNETOMETER_TEST_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "stm32h7xx_hal.h"

bool MagnetometerTest_Start(I2C_HandleTypeDef *i2c);
bool MagnetometerTest_HandleCommand(const char *command);
bool MagnetometerTest_Sleep(void);
/* Bus I/O happens only when allow_bus_io: no samples, and so a stale EMF
 * level, while the recorder captures. */
void MagnetometerTest_Service(bool allow_bus_io);
/* Samples the EMF metric every 100 ms into emf_level (decision 0013) while
 * enabled; the sensor stays in continuous mode meanwhile. */
void MagnetometerTest_SetEmfFeed(bool enabled);

#ifdef __cplusplus
}
#endif

#endif /* MAGNETOMETER_TEST_H */
