#ifndef SD_TEST_H
#define SD_TEST_H

#include <stdbool.h>
#include "stm32h7xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

void SdTest_Start(SD_HandleTypeDef *sd);
bool SdTest_HandleCommand(const char *command);
void SdTest_Service(void);
void SdTest_Stop(void);

#ifdef __cplusplus
}
#endif

#endif
