#ifndef UI_BOARD_TEST_H
#define UI_BOARD_TEST_H

#include <stdbool.h>

#include "stm32h7xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

bool UiBoardTest_Start(I2C_HandleTypeDef *i2c, SPI_HandleTypeDef *display_spi);
bool UiBoardTest_HandleCommand(const char *command);
void UiBoardTest_Tick1ms(void);
void UiBoardTest_Service(void);
void UiBoardTest_SafeOff(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_BOARD_TEST_H */
