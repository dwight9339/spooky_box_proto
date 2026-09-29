#include "fuel_gauge_test.h"

#include <stdio.h>

#define BQ27441_ADDRESS_HAL             (0x55U << 1U)
#define BQ27441_CONTROL                 0x00U
#define BQ27441_TEMPERATURE             0x02U
#define BQ27441_VOLTAGE                 0x04U
#define BQ27441_FLAGS                   0x06U
#define BQ27441_REMAINING_CAPACITY      0x0CU
#define BQ27441_FULL_CHARGE_CAPACITY    0x0EU
#define BQ27441_AVERAGE_CURRENT         0x10U
#define BQ27441_AVERAGE_POWER           0x18U
#define BQ27441_STATE_OF_CHARGE         0x1CU
#define BQ27441_STATE_OF_HEALTH         0x20U
#define BQ27441_DESIGN_CAPACITY         0x3CU

#define BQ27441_CONTROL_DEVICE_TYPE     0x0001U
#define BQ27441_CONTROL_FW_VERSION      0x0002U
#define BQ27441_CONTROL_DM_CODE         0x0004U
#define BQ27441_CONTROL_CHEM_ID         0x0008U
#define BQ27441_CONTROL_STATUS          0x0000U
#define BQ27441_CONTROL_SET_CFGUPDATE   0x0013U
#define BQ27441_CONTROL_SEALED          0x0020U
#define BQ27441_CONTROL_SOFT_RESET      0x0042U
#define BQ27441_UNSEAL_KEY              0x8000U
#define BQ27441_EXPECTED_DEVICE_TYPE    0x0421U

#define BQ27441_STATUS_SEALED           0x2000U

#define BQ27441_FLAG_DSG                0x0001U
#define BQ27441_FLAG_SOCF               0x0002U
#define BQ27441_FLAG_SOC1               0x0004U
#define BQ27441_FLAG_BAT_DET            0x0008U
#define BQ27441_FLAG_CFGUPMODE          0x0010U
#define BQ27441_FLAG_ITPOR              0x0020U
#define BQ27441_FLAG_CHG                0x0100U
#define BQ27441_FLAG_FC                 0x0200U

#define BQ27441_BLOCK_DATA_CLASS        0x3EU
#define BQ27441_BLOCK_DATA_BLOCK        0x3FU
#define BQ27441_BLOCK_DATA_START        0x40U
#define BQ27441_BLOCK_CHECKSUM          0x60U
#define BQ27441_BLOCK_DATA_CONTROL      0x61U
#define BQ27441_STATE_CLASS             82U
#define BQ27441_STATE_BLOCK             0U
#define BQ27441_STATE_DESIGN_CAP_OFFSET 10U
#define BQ27441_STATE_DESIGN_EN_OFFSET  12U
#define BQ27441_STATE_DEFAULT_CAP_OFFSET 14U
#define BQ27441_BLOCK_SIZE              32U

#define FUEL_GAUGE_I2C_TIMEOUT_MS       100U
#define FUEL_GAUGE_CONFIG_TIMEOUT_MS    1500U
#define FUEL_GAUGE_REPORT_PERIOD_MS     300000U
#define PROTOTYPE_BATTERY_CAPACITY_MAH  3700U
#define PROTOTYPE_BATTERY_ENERGY_MWH    13690U

typedef struct
{
  uint16_t temperature_dK;
  uint16_t voltage_mV;
  uint16_t flags;
  uint16_t remaining_capacity_mAh;
  uint16_t full_charge_capacity_mAh;
  int16_t average_current_mA;
  int16_t average_power_mW;
  uint16_t state_of_charge_pct;
  uint8_t state_of_health_pct;
  uint8_t state_of_health_status;
  uint16_t design_capacity_mAh;
} FuelGaugeSnapshot;

static I2C_HandleTypeDef *fuel_i2c;
static uint32_t fuel_last_report_ms;
static bool fuel_running;
static FuelGaugeSnapshot fuel_cached_snapshot;
static bool fuel_cache_valid;

static bool FuelGaugeReadBytes(uint8_t command, uint8_t *bytes,
                               uint16_t length)
{
  if ((fuel_i2c == NULL) || (bytes == NULL) || (length == 0U))
  {
    return false;
  }

  return HAL_I2C_Mem_Read(fuel_i2c, BQ27441_ADDRESS_HAL, command,
                          I2C_MEMADD_SIZE_8BIT, bytes, length,
                          FUEL_GAUGE_I2C_TIMEOUT_MS) == HAL_OK;
}

static bool FuelGaugeWriteBytes(uint8_t command, const uint8_t *bytes,
                                uint16_t length)
{
  if ((fuel_i2c == NULL) || (bytes == NULL) || (length == 0U))
  {
    return false;
  }

  return HAL_I2C_Mem_Write(fuel_i2c, BQ27441_ADDRESS_HAL, command,
                           I2C_MEMADD_SIZE_8BIT, (uint8_t *)bytes, length,
                           FUEL_GAUGE_I2C_TIMEOUT_MS) == HAL_OK;
}

static bool FuelGaugeReadWord(uint8_t command, uint16_t *value)
{
  uint8_t bytes[2];

  if ((fuel_i2c == NULL) || (value == NULL))
  {
    return false;
  }

  if (!FuelGaugeReadBytes(command, bytes, sizeof(bytes)))
  {
    return false;
  }

  *value = (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U);
  return true;
}

static bool FuelGaugeWriteControl(uint16_t subcommand)
{
  uint8_t bytes[2] = {
    (uint8_t)(subcommand & 0xFFU),
    (uint8_t)(subcommand >> 8U)
  };

  return FuelGaugeWriteBytes(BQ27441_CONTROL, bytes, sizeof(bytes));
}

static bool FuelGaugeReadControl(uint16_t subcommand, uint16_t *value)
{
  if ((value == NULL) || !FuelGaugeWriteControl(subcommand))
  {
    return false;
  }

  HAL_Delay(2U);
  return FuelGaugeReadWord(BQ27441_CONTROL, value);
}

static bool FuelGaugeWaitFlag(uint16_t mask, bool set, uint32_t timeout_ms)
{
  uint32_t start = HAL_GetTick();
  uint16_t flags;

  do
  {
    if (!FuelGaugeReadWord(BQ27441_FLAGS, &flags))
    {
      return false;
    }
    if (((flags & mask) != 0U) == set)
    {
      return true;
    }
    HAL_Delay(10U);
  } while ((uint32_t)(HAL_GetTick() - start) < timeout_ms);

  return false;
}

static bool FuelGaugeWaitSealed(bool sealed, uint32_t timeout_ms)
{
  uint32_t start = HAL_GetTick();
  uint16_t status;

  do
  {
    if (!FuelGaugeReadControl(BQ27441_CONTROL_STATUS, &status))
    {
      return false;
    }
    if (((status & BQ27441_STATUS_SEALED) != 0U) == sealed)
    {
      return true;
    }
    HAL_Delay(10U);
  } while ((uint32_t)(HAL_GetTick() - start) < timeout_ms);

  return false;
}

static uint16_t FuelGaugeBlockReadBe16(const uint8_t *block, uint8_t offset)
{
  return ((uint16_t)block[offset] << 8U) | block[offset + 1U];
}

static void FuelGaugeBlockWriteBe16(uint8_t *block, uint8_t offset,
                                    uint16_t value)
{
  block[offset] = (uint8_t)(value >> 8U);
  block[offset + 1U] = (uint8_t)(value & 0xFFU);
}

static uint8_t FuelGaugeBlockChecksum(const uint8_t *block)
{
  uint16_t sum = 0U;
  uint32_t index;

  for (index = 0U; index < BQ27441_BLOCK_SIZE; ++index)
  {
    sum = (uint16_t)(sum + block[index]);
  }
  return (uint8_t)(0xFFU - (sum & 0xFFU));
}

static bool FuelGaugeSelectStateBlock(void)
{
  const uint8_t enable = 0U;
  const uint8_t class_id = BQ27441_STATE_CLASS;
  const uint8_t block_id = BQ27441_STATE_BLOCK;

  if (!FuelGaugeWriteBytes(BQ27441_BLOCK_DATA_CONTROL, &enable, 1U) ||
      !FuelGaugeWriteBytes(BQ27441_BLOCK_DATA_CLASS, &class_id, 1U) ||
      !FuelGaugeWriteBytes(BQ27441_BLOCK_DATA_BLOCK, &block_id, 1U))
  {
    return false;
  }

  HAL_Delay(2U);
  return true;
}

static bool FuelGaugeApplyPrototypeConfiguration(void)
{
  uint8_t block[BQ27441_BLOCK_SIZE];
  uint8_t verify_block[BQ27441_BLOCK_SIZE];
  uint8_t stored_checksum;
  uint8_t expected_checksum;
  uint8_t verify_checksum;
  uint16_t status;
  uint16_t flags;
  uint16_t old_capacity;
  uint16_t old_energy;
  uint16_t default_capacity;
  bool was_sealed;
  bool config_mode = false;
  bool data_verified = false;
  bool cleanup_ok = true;

  printf("[fuel] CONFIG: applying 3700 mAh / 13690 mWh persistent setup\r\n");

  if (!FuelGaugeReadControl(BQ27441_CONTROL_STATUS, &status))
  {
    printf("[fuel] CONFIG FAIL: could not read CONTROL_STATUS\r\n");
    return false;
  }
  was_sealed = (status & BQ27441_STATUS_SEALED) != 0U;
  printf("[fuel] CONFIG: gauge was %s\r\n",
         was_sealed ? "sealed" : "unsealed");

  if (was_sealed)
  {
    if (!FuelGaugeWriteControl(BQ27441_UNSEAL_KEY) ||
        !FuelGaugeWriteControl(BQ27441_UNSEAL_KEY) ||
        !FuelGaugeWaitSealed(false, FUEL_GAUGE_CONFIG_TIMEOUT_MS))
    {
      printf("[fuel] CONFIG FAIL: could not unseal gauge\r\n");
      return false;
    }
  }

  if (!FuelGaugeWriteControl(BQ27441_CONTROL_SET_CFGUPDATE))
  {
    printf("[fuel] CONFIG FAIL: SET_CFGUPDATE command failed\r\n");
    goto cleanup;
  }
  config_mode = true;
  if (!FuelGaugeWaitFlag(BQ27441_FLAG_CFGUPMODE, true,
                         FUEL_GAUGE_CONFIG_TIMEOUT_MS))
  {
    printf("[fuel] CONFIG FAIL: gauge did not enter CFGUPDATE\r\n");
    goto cleanup;
  }

  if (!FuelGaugeSelectStateBlock() ||
      !FuelGaugeReadBytes(BQ27441_BLOCK_DATA_START, block, sizeof(block)) ||
      !FuelGaugeReadBytes(BQ27441_BLOCK_CHECKSUM, &stored_checksum, 1U))
  {
    printf("[fuel] CONFIG FAIL: could not read State block\r\n");
    goto cleanup;
  }

  expected_checksum = FuelGaugeBlockChecksum(block);
  if (stored_checksum != expected_checksum)
  {
    printf("[fuel] CONFIG FAIL: existing checksum=0x%02X calculated=0x%02X\r\n",
           stored_checksum, expected_checksum);
    goto cleanup;
  }

  old_capacity = FuelGaugeBlockReadBe16(block,
                                        BQ27441_STATE_DESIGN_CAP_OFFSET);
  old_energy = FuelGaugeBlockReadBe16(block, BQ27441_STATE_DESIGN_EN_OFFSET);
  default_capacity = FuelGaugeBlockReadBe16(
    block, BQ27441_STATE_DEFAULT_CAP_OFFSET);
  printf("[fuel] CONFIG: before design=%u mAh energy=%u mWh "
         "default=%u mAh checksum=0x%02X\r\n",
         old_capacity, old_energy, default_capacity, stored_checksum);

  FuelGaugeBlockWriteBe16(block, BQ27441_STATE_DESIGN_CAP_OFFSET,
                          PROTOTYPE_BATTERY_CAPACITY_MAH);
  FuelGaugeBlockWriteBe16(block, BQ27441_STATE_DESIGN_EN_OFFSET,
                          PROTOTYPE_BATTERY_ENERGY_MWH);
  expected_checksum = FuelGaugeBlockChecksum(block);

  if (!FuelGaugeWriteBytes(
        (uint8_t)(BQ27441_BLOCK_DATA_START +
                  BQ27441_STATE_DESIGN_CAP_OFFSET),
        &block[BQ27441_STATE_DESIGN_CAP_OFFSET], 4U) ||
      !FuelGaugeWriteBytes(BQ27441_BLOCK_CHECKSUM, &expected_checksum, 1U))
  {
    printf("[fuel] CONFIG FAIL: data/checksum write failed\r\n");
    goto cleanup;
  }
  HAL_Delay(10U);

  if (!FuelGaugeSelectStateBlock() ||
      !FuelGaugeReadBytes(BQ27441_BLOCK_DATA_START, verify_block,
                          sizeof(verify_block)) ||
      !FuelGaugeReadBytes(BQ27441_BLOCK_CHECKSUM, &verify_checksum, 1U))
  {
    printf("[fuel] CONFIG FAIL: readback failed\r\n");
    goto cleanup;
  }

  if ((FuelGaugeBlockReadBe16(verify_block,
                              BQ27441_STATE_DESIGN_CAP_OFFSET) !=
       PROTOTYPE_BATTERY_CAPACITY_MAH) ||
      (FuelGaugeBlockReadBe16(verify_block,
                              BQ27441_STATE_DESIGN_EN_OFFSET) !=
       PROTOTYPE_BATTERY_ENERGY_MWH) ||
      (FuelGaugeBlockReadBe16(verify_block,
                              BQ27441_STATE_DEFAULT_CAP_OFFSET) !=
       default_capacity) ||
      (verify_checksum != FuelGaugeBlockChecksum(verify_block)))
  {
    printf("[fuel] CONFIG FAIL: verified data/checksum mismatch\r\n");
    goto cleanup;
  }

  printf("[fuel] CONFIG: verified design=%u mAh energy=%u mWh; "
         "default preserved=%u mAh\r\n",
         PROTOTYPE_BATTERY_CAPACITY_MAH,
         PROTOTYPE_BATTERY_ENERGY_MWH,
         default_capacity);
  data_verified = true;

cleanup:
  if (config_mode)
  {
    if (!FuelGaugeWriteControl(BQ27441_CONTROL_SOFT_RESET) ||
        !FuelGaugeWaitFlag(BQ27441_FLAG_CFGUPMODE, false,
                           FUEL_GAUGE_CONFIG_TIMEOUT_MS) ||
        !FuelGaugeWaitFlag(BQ27441_FLAG_ITPOR, false,
                           FUEL_GAUGE_CONFIG_TIMEOUT_MS))
    {
      printf("[fuel] CONFIG FAIL: could not exit CFGUPDATE cleanly\r\n");
      cleanup_ok = false;
    }
  }

  if (was_sealed)
  {
    if (!FuelGaugeReadControl(BQ27441_CONTROL_STATUS, &status) ||
        (((status & BQ27441_STATUS_SEALED) == 0U) &&
         (!FuelGaugeWriteControl(BQ27441_CONTROL_SEALED) ||
          !FuelGaugeWaitSealed(true, FUEL_GAUGE_CONFIG_TIMEOUT_MS))))
    {
      printf("[fuel] CONFIG FAIL: could not restore sealed state\r\n");
      cleanup_ok = false;
    }
  }

  if (!data_verified || !cleanup_ok ||
      !FuelGaugeReadWord(BQ27441_FLAGS, &flags))
  {
    return false;
  }

  printf("[fuel] CONFIG PASS: persistent values committed; ITPOR=%u "
         "CFGUP=%u seal=%s\r\n",
         (flags & BQ27441_FLAG_ITPOR) != 0U,
         (flags & BQ27441_FLAG_CFGUPMODE) != 0U,
         was_sealed ? "restored" : "unchanged-unsealed");
  return true;
}

static bool FuelGaugeReadSnapshot(FuelGaugeSnapshot *snapshot)
{
  uint16_t current;
  uint16_t power;
  uint16_t health;

  if (snapshot == NULL)
  {
    return false;
  }

  if (!FuelGaugeReadWord(BQ27441_TEMPERATURE, &snapshot->temperature_dK) ||
      !FuelGaugeReadWord(BQ27441_VOLTAGE, &snapshot->voltage_mV) ||
      !FuelGaugeReadWord(BQ27441_FLAGS, &snapshot->flags) ||
      !FuelGaugeReadWord(BQ27441_REMAINING_CAPACITY,
                         &snapshot->remaining_capacity_mAh) ||
      !FuelGaugeReadWord(BQ27441_FULL_CHARGE_CAPACITY,
                         &snapshot->full_charge_capacity_mAh) ||
      !FuelGaugeReadWord(BQ27441_AVERAGE_CURRENT, &current) ||
      !FuelGaugeReadWord(BQ27441_AVERAGE_POWER, &power) ||
      !FuelGaugeReadWord(BQ27441_STATE_OF_CHARGE,
                         &snapshot->state_of_charge_pct) ||
      !FuelGaugeReadWord(BQ27441_STATE_OF_HEALTH, &health) ||
      !FuelGaugeReadWord(BQ27441_DESIGN_CAPACITY,
                         &snapshot->design_capacity_mAh))
  {
    return false;
  }

  snapshot->average_current_mA = (int16_t)current;
  snapshot->average_power_mW = (int16_t)power;
  snapshot->state_of_health_pct = (uint8_t)(health & 0xFFU);
  snapshot->state_of_health_status = (uint8_t)(health >> 8U);
  return true;
}

static void FuelGaugePrintSnapshot(const FuelGaugeSnapshot *snapshot)
{
  int32_t temperature_dC;
  uint32_t temperature_magnitude;
  const char *direction;

  temperature_dC = (int32_t)snapshot->temperature_dK - 2732;
  temperature_magnitude = (temperature_dC < 0)
    ? (uint32_t)(-temperature_dC)
    : (uint32_t)temperature_dC;

  if (snapshot->average_current_mA > 5)
  {
    direction = "charging";
  }
  else if (snapshot->average_current_mA < -5)
  {
    direction = "discharging";
  }
  else
  {
    direction = "idle";
  }

  printf("[fuel] %u mV, SOC=%u%%, %s%lu.%01lu C, %s at %d mA / %d mW\r\n",
         snapshot->voltage_mV,
         snapshot->state_of_charge_pct,
         (temperature_dC < 0) ? "-" : "",
         (unsigned long)(temperature_magnitude / 10U),
         (unsigned long)(temperature_magnitude % 10U),
         direction,
         snapshot->average_current_mA,
         snapshot->average_power_mW);
  printf("[fuel] remaining=%u mAh, full=%u mAh, design=%u mAh; "
         "SOH=%u%% status=%u\r\n",
         snapshot->remaining_capacity_mAh,
         snapshot->full_charge_capacity_mAh,
         snapshot->design_capacity_mAh,
         snapshot->state_of_health_pct,
         snapshot->state_of_health_status);
  printf("[fuel] flags=0x%04X BAT_DET=%u ITPOR=%u CHG=%u DSG=%u FC=%u "
         "SOC1=%u SOCF=%u CFGUP=%u\r\n",
         snapshot->flags,
         (snapshot->flags & BQ27441_FLAG_BAT_DET) != 0U,
         (snapshot->flags & BQ27441_FLAG_ITPOR) != 0U,
         (snapshot->flags & BQ27441_FLAG_CHG) != 0U,
         (snapshot->flags & BQ27441_FLAG_DSG) != 0U,
         (snapshot->flags & BQ27441_FLAG_FC) != 0U,
         (snapshot->flags & BQ27441_FLAG_SOC1) != 0U,
         (snapshot->flags & BQ27441_FLAG_SOCF) != 0U,
         (snapshot->flags & BQ27441_FLAG_CFGUPMODE) != 0U);
}

static void FuelGaugePrintChargingUpdate(const FuelGaugeSnapshot *snapshot)
{
  int32_t temperature_dC;
  uint32_t temperature_magnitude;
  const char *direction;

  temperature_dC = (int32_t)snapshot->temperature_dK - 2732;
  temperature_magnitude = (temperature_dC < 0)
    ? (uint32_t)(-temperature_dC)
    : (uint32_t)temperature_dC;

  if (snapshot->average_current_mA > 5)
  {
    direction = "charging";
  }
  else if (snapshot->average_current_mA < -5)
  {
    direction = "discharging";
  }
  else
  {
    direction = "idle";
  }

  printf("[fuel] update: SOC=%u%%, %u mV, %s at %d mA / %d mW, "
         "%u/%u mAh, %s%lu.%01lu C\r\n",
         snapshot->state_of_charge_pct,
         snapshot->voltage_mV,
         direction,
         snapshot->average_current_mA,
         snapshot->average_power_mW,
         snapshot->remaining_capacity_mAh,
         snapshot->full_charge_capacity_mAh,
         (temperature_dC < 0) ? "-" : "",
         (unsigned long)(temperature_magnitude / 10U),
         (unsigned long)(temperature_magnitude % 10U));
}

bool FuelGaugeTest_Start(I2C_HandleTypeDef *i2c)
{
  FuelGaugeSnapshot snapshot;
  uint16_t device_type;
  uint16_t firmware_version;
  uint16_t dm_code;
  uint16_t chemistry_id;

  fuel_i2c = i2c;
  fuel_running = false;
  fuel_cache_valid = false;

  printf("\r\n[fuel] BQ27441-G1A fuel-gauge test\r\n");
  printf("[fuel] I2C2 PB10/PB11; expected address 0x55\r\n");
  printf("[fuel] target configuration: 3700 mAh / 13690 mWh\r\n");

  if ((fuel_i2c == NULL) ||
      (HAL_I2C_IsDeviceReady(fuel_i2c, BQ27441_ADDRESS_HAL, 3U,
                             FUEL_GAUGE_I2C_TIMEOUT_MS) != HAL_OK))
  {
    printf("[fuel] FAIL: no ACK at 0x55; I2C error=0x%08lX\r\n",
           (unsigned long)((fuel_i2c == NULL) ? 0U
                                             : HAL_I2C_GetError(fuel_i2c)));
    return false;
  }

  if (!FuelGaugeReadControl(BQ27441_CONTROL_DEVICE_TYPE, &device_type) ||
      !FuelGaugeReadControl(BQ27441_CONTROL_FW_VERSION, &firmware_version) ||
      !FuelGaugeReadControl(BQ27441_CONTROL_DM_CODE, &dm_code) ||
      !FuelGaugeReadControl(BQ27441_CONTROL_CHEM_ID, &chemistry_id))
  {
    printf("[fuel] FAIL: control-subcommand read; I2C error=0x%08lX\r\n",
           (unsigned long)HAL_I2C_GetError(fuel_i2c));
    return false;
  }

  if (device_type != BQ27441_EXPECTED_DEVICE_TYPE)
  {
    printf("[fuel] FAIL: DEVICE_TYPE=0x%04X, expected 0x%04X\r\n",
           device_type, BQ27441_EXPECTED_DEVICE_TYPE);
    return false;
  }

  printf("[fuel] PASS: DEVICE_TYPE=0x%04X FW=0x%04X DM=0x%02X "
         "CHEM_ID=0x%04X\r\n",
         device_type, firmware_version, dm_code & 0xFFU, chemistry_id);

  if (!FuelGaugeReadSnapshot(&snapshot))
  {
    printf("[fuel] FAIL: telemetry read; I2C error=0x%08lX\r\n",
           (unsigned long)HAL_I2C_GetError(fuel_i2c));
    return false;
  }

  FuelGaugePrintSnapshot(&snapshot);
  if ((snapshot.flags & BQ27441_FLAG_BAT_DET) == 0U)
  {
    printf("[fuel] FAIL: BAT_DET is clear; refusing configuration write\r\n");
    return false;
  }
  if (snapshot.design_capacity_mAh != PROTOTYPE_BATTERY_CAPACITY_MAH)
  {
    printf("[fuel] WARN: gauge design capacity is %u mAh, but the attached "
           "battery is %u mAh\r\n",
           snapshot.design_capacity_mAh, PROTOTYPE_BATTERY_CAPACITY_MAH);
    printf("[fuel] WARN: voltage/current are useful; SOC and mAh need gauge "
           "configuration\r\n");
  }

  if ((snapshot.design_capacity_mAh != PROTOTYPE_BATTERY_CAPACITY_MAH) ||
      ((snapshot.flags & BQ27441_FLAG_ITPOR) != 0U))
  {
    if (!FuelGaugeApplyPrototypeConfiguration())
    {
      printf("[fuel] FAIL: prototype configuration was not completed\r\n");
      return false;
    }

    HAL_Delay(100U);
    if (!FuelGaugeReadSnapshot(&snapshot) ||
        (snapshot.design_capacity_mAh != PROTOTYPE_BATTERY_CAPACITY_MAH) ||
        ((snapshot.flags & (BQ27441_FLAG_ITPOR |
                            BQ27441_FLAG_CFGUPMODE)) != 0U))
    {
      printf("[fuel] FAIL: post-configuration telemetry verification\r\n");
      return false;
    }
    printf("[fuel] post-configuration telemetry\r\n");
    FuelGaugePrintSnapshot(&snapshot);
  }
  else
  {
    printf("[fuel] CONFIG: already applied; no data-memory write needed\r\n");
  }

  fuel_last_report_ms = HAL_GetTick();
  fuel_cached_snapshot = snapshot;
  fuel_cache_valid = true;
  fuel_running = true;
  return true;
}

void FuelGaugeTest_Service(bool allow_bus_io)
{
  uint32_t now;

  if (!fuel_running || !allow_bus_io)
  {
    return;
  }

  now = HAL_GetTick();
  if ((uint32_t)(now - fuel_last_report_ms) < FUEL_GAUGE_REPORT_PERIOD_MS)
  {
    return;
  }
  fuel_last_report_ms = now;

  (void)FuelGaugeTest_ReportNow();
}

bool FuelGaugeTest_ReportNow(void)
{
  FuelGaugeSnapshot snapshot;

  if (!fuel_running)
  {
    return false;
  }

  if (!FuelGaugeReadSnapshot(&snapshot))
  {
    printf("[fuel] WARN: telemetry read failed; I2C error=0x%08lX\r\n",
           (unsigned long)HAL_I2C_GetError(fuel_i2c));
    return false;
  }

  fuel_last_report_ms = HAL_GetTick();
  fuel_cached_snapshot = snapshot;
  fuel_cache_valid = true;
  FuelGaugePrintChargingUpdate(&snapshot);
  return true;
}

bool FuelGaugeTest_ReadTelemetry(FuelGaugeTelemetry *telemetry)
{
  FuelGaugeSnapshot snapshot;

  if ((telemetry == NULL) || !fuel_running || !fuel_cache_valid)
  {
    return false;
  }
  snapshot = fuel_cached_snapshot;

  telemetry->voltage_mV = snapshot.voltage_mV;
  telemetry->remaining_capacity_mAh = snapshot.remaining_capacity_mAh;
  telemetry->full_charge_capacity_mAh = snapshot.full_charge_capacity_mAh;
  telemetry->design_capacity_mAh = snapshot.design_capacity_mAh;
  telemetry->average_current_mA = snapshot.average_current_mA;
  telemetry->average_power_mW = snapshot.average_power_mW;
  telemetry->state_of_charge_pct = snapshot.state_of_charge_pct;
  telemetry->state_of_health_pct = snapshot.state_of_health_pct;
  telemetry->state_of_health_status = snapshot.state_of_health_status;
  telemetry->flags = snapshot.flags;
  telemetry->battery_present =
    (snapshot.flags & BQ27441_FLAG_BAT_DET) != 0U;
  telemetry->full = (snapshot.flags & BQ27441_FLAG_FC) != 0U;
  return true;
}
