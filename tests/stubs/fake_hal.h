#ifndef FAKE_HAL_H
#define FAKE_HAL_H
#include "stm32h7xx_hal.h"
#include <stdbool.h>
extern HAL_StatusTypeDef test_tx_result;
extern bool test_auto_complete;
extern uint8_t test_output[32768];
extern uint32_t test_output_length, test_abort_count;
extern const uint8_t *test_tx_bytes;
extern uint16_t test_tx_length;
void TestComplete(void);
#endif
