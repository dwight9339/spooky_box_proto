#ifndef TEST_HAL_H
#define TEST_HAL_H
#include <stdint.h>
#include <stddef.h>
typedef struct { uint32_t instance; } UART_HandleTypeDef;
typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
typedef struct { uint32_t CTRL; } TestSysTick;
extern TestSysTick test_systick;
#define SysTick (&test_systick)
#define SysTick_CTRL_TICKINT_Msk 2U
#define UART7_IRQn 7
extern uint32_t test_tick, test_primask, test_ipsr;
uint32_t __get_PRIMASK(void);
uint32_t __get_IPSR(void);
void __disable_irq(void);
void __set_PRIMASK(uint32_t mask);
uint32_t HAL_GetTick(void);
void HAL_Delay(uint32_t ms);
void HAL_NVIC_SetPriority(int irq, uint32_t preempt, uint32_t sub);
void HAL_NVIC_ClearPendingIRQ(int irq);
void HAL_NVIC_EnableIRQ(int irq);
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *uart,
                                     const uint8_t *bytes, uint16_t length);
HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *uart);
void HAL_UART_IRQHandler(UART_HandleTypeDef *uart);
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *uart);
void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart);
#endif
