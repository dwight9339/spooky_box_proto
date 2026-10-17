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

#if defined(SPOOKY_DEMO)
/* Demo-only surfaces (demo_field.c). Switch edges and encoder detents are
 * forwarded to DemoField_OnControl and DemoField_OnDetents from the UI service
 * pass. LightsStart gives the two button LEDs and the twelve encoder LED
 * channels to timer PWM (TIM16, TIM17, TIM1, TIM2, TIM4, TIM12, TIM13, TIM14;
 * p04.12); duty is in permille, encoder channels red, green, blue. SafeOff
 * returns those pins to GPIO, low, until the next reset. DisplayStart resets
 * and blanks the OLED (about 200 ms, boot only); DisplayWritePage writes one 128-byte page (bounded SPI6 transfer). Pressed
 * reads a control's raw level, for the initial state only. */
bool UiBoardTest_DemoLightsStart(void);
void UiBoardTest_DemoSetLights(const uint16_t button_duty[2], const uint16_t encoder_duty[4][3]);
bool UiBoardTest_DemoDisplayStart(void);
bool UiBoardTest_DemoDisplayWritePage(uint8_t page, const uint8_t *bytes);
bool UiBoardTest_DemoPressed(uint8_t control);
#endif

#ifdef __cplusplus
}
#endif

#endif /* UI_BOARD_TEST_H */
