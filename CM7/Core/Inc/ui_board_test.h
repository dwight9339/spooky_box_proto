#ifndef UI_BOARD_TEST_H
#define UI_BOARD_TEST_H

#include <stdbool.h>
#include <stdint.h>

#include "stm32h7xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

bool UiBoardTest_Start(I2C_HandleTypeDef *i2c, SPI_HandleTypeDef *display_spi);
bool UiBoardTest_HandleCommand(const char *command);
void UiBoardTest_Tick1ms(void);
void UiBoardTest_Service(bool recording);
void UiBoardTest_SafeOff(void);

/* The matrix for the feedback service (matrix_adapter.c). Acquire powers and
 * initializes it (bus I/O); while held, the UI test commands report it busy.
 * PowerOff only drops its enable pin, so it may be used while the recorder
 * captures; the matrix stays held and Acquire powers it again. UI OFF releases
 * it. */
bool UiBoardTest_MatrixAcquire(void);
bool UiBoardTest_MatrixHeld(void);
bool UiBoardTest_MatrixWriteRun(uint8_t page, uint8_t reg, const uint8_t *bytes,
                                uint8_t length);
void UiBoardTest_MatrixPowerOff(void);
/* blank: clear the PWM registers first (bus I/O). */
void UiBoardTest_MatrixRelease(bool blank);

#ifdef __cplusplus
}
#endif

#endif /* UI_BOARD_TEST_H */
