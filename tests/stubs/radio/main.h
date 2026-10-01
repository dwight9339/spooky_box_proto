#ifndef TEST_RADIO_MAIN_H
#define TEST_RADIO_MAIN_H
/* Stand-in for CM7/Core/Inc/main.h in radio_control_test: the GPIO and I2C surface
 * that radio_control_service.c uses. radio_control_test.c implements it. */
#include "stm32h7xx_hal.h"
typedef struct { uint32_t instance; } GPIO_TypeDef;
typedef enum { GPIO_PIN_RESET = 0, GPIO_PIN_SET } GPIO_PinState;
extern GPIO_TypeDef test_gpiob;
#define GPIOB (&test_gpiob)
#define GPIO_PIN_8 0x0100U
#define GPIO_PIN_9 0x0200U
#define RADIO_RST_GPIO_Port GPIOB
#define RADIO_RST_Pin 0x0001U
#define RADIO_INT_GPIO_Port GPIOB
#define RADIO_INT_Pin 0x0002U
#define RADIO_GPIO_1_GPIO_Port GPIOB
#define RADIO_GPIO_1_Pin 0x0004U
#define RADIO_SW_SWITCH_GPIO_Port GPIOB
#define RADIO_SW_SWITCH_Pin 0x0008U
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state);
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin);
HAL_StatusTypeDef HAL_I2C_Master_Transmit(I2C_HandleTypeDef *i2c, uint16_t address,
                                          uint8_t *data, uint16_t length, uint32_t timeout);
HAL_StatusTypeDef HAL_I2C_Master_Receive(I2C_HandleTypeDef *i2c, uint16_t address,
                                         uint8_t *data, uint16_t length, uint32_t timeout);
HAL_StatusTypeDef HAL_I2C_IsDeviceReady(I2C_HandleTypeDef *i2c, uint16_t address,
                                        uint32_t trials, uint32_t timeout);
uint32_t HAL_I2C_GetError(I2C_HandleTypeDef *i2c);
#endif
