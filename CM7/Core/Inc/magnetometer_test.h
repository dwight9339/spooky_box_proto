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
void MagnetometerTest_Service(void);

#ifdef __cplusplus
}
#endif

#endif /* MAGNETOMETER_TEST_H */
