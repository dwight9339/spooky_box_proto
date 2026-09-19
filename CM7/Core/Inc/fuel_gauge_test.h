#ifndef FUEL_GAUGE_TEST_H
#define FUEL_GAUGE_TEST_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "stm32h7xx_hal.h"

typedef struct
{
  uint16_t voltage_mV;
  uint16_t remaining_capacity_mAh;
  uint16_t full_charge_capacity_mAh;
  uint16_t design_capacity_mAh;
  int16_t average_current_mA;
  int16_t average_power_mW;
  uint16_t state_of_charge_pct;
  uint8_t state_of_health_pct;
  uint8_t state_of_health_status;
  uint16_t flags;
  bool battery_present;
  bool full;
} FuelGaugeTelemetry;

bool FuelGaugeTest_Start(I2C_HandleTypeDef *i2c);
bool FuelGaugeTest_ReportNow(void);
bool FuelGaugeTest_ReadTelemetry(FuelGaugeTelemetry *telemetry);
void FuelGaugeTest_Service(void);

#ifdef __cplusplus
}
#endif

#endif /* FUEL_GAUGE_TEST_H */
