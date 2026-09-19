#include "magnetometer_test.h"

#include "usb_test.h"

#include <stdio.h>
#include <string.h>

#define TMAG5273_ADDRESS_HAL             (0x35U << 1U)
#define TMAG5273_DEVICE_CONFIG_1         0x00U
#define TMAG5273_DEVICE_CONFIG_2         0x01U
#define TMAG5273_SENSOR_CONFIG_1         0x02U
#define TMAG5273_SENSOR_CONFIG_2         0x03U
#define TMAG5273_T_CONFIG                0x07U
#define TMAG5273_INT_CONFIG_1            0x08U
#define TMAG5273_DEVICE_ID               0x0DU
#define TMAG5273_MANUFACTURER_ID_LSB     0x0EU
#define TMAG5273_X_MSB_RESULT            0x12U

#define TMAG5273_MANUFACTURER_ID         0x5449U
#define TMAG5273_VARIANT_A1              0x01U
#define TMAG5273_VARIANT_A2              0x02U
#define TMAG5273_CONFIG_32X_AVERAGING    0x14U
#define TMAG5273_CONFIG_XYZ              0x70U
#define TMAG5273_MODE_SLEEP              0x01U
#define TMAG5273_MODE_CONTINUOUS         0x02U
#define TMAG5273_RESULT_READY            0x01U
#define TMAG5273_DIAGNOSTIC_ERROR        0x02U

#define MAGNETOMETER_I2C_TIMEOUT_MS      100U
#define MAGNETOMETER_WAKE_DELAY_MS       1U
#define MAGNETOMETER_STARTUP_DELAY_MS    5U
#define MAGNETOMETER_DEFAULT_PERIOD_MS   100U
#define MAGNETOMETER_MIN_PERIOD_MS       50U
#define MAGNETOMETER_MAX_PERIOD_MS       60000U
#define EMF_BASELINE_UPDATE_PERIOD_MS    5000U
#define EMF_BASELINE_FILTER_SHIFT        4U

typedef enum
{
  MAG_STREAM_NONE,
  MAG_STREAM_AXES,
  MAG_STREAM_EMF
} MagnetometerStreamMode;

typedef struct
{
  int16_t raw_x;
  int16_t raw_y;
  int16_t raw_z;
  int32_t x_uT;
  int32_t y_uT;
  int32_t z_uT;
  uint32_t magnitude_uT;
  uint32_t baseline_uT;
  uint32_t emf_uT;
  uint8_t conversion_status;
} MagnetometerSample;

static I2C_HandleTypeDef *mag_i2c;
static uint32_t mag_range_uT;
static uint32_t mag_stream_period_ms = MAGNETOMETER_DEFAULT_PERIOD_MS;
static uint32_t mag_next_stream_ms;
static uint32_t mag_next_baseline_ms;
static int32_t mag_baseline_q8;
static uint16_t mag_manufacturer_id;
static uint8_t mag_device_id;
static bool mag_ready;
static bool mag_continuous;
static bool mag_baseline_valid;
static MagnetometerStreamMode mag_stream_mode;

static bool MagRead(uint8_t reg, uint8_t *data, uint16_t length)
{
  if ((mag_i2c == NULL) || (data == NULL) || (length == 0U))
  {
    return false;
  }
  return HAL_I2C_Mem_Read(mag_i2c, TMAG5273_ADDRESS_HAL, reg,
                          I2C_MEMADD_SIZE_8BIT, data, length,
                          MAGNETOMETER_I2C_TIMEOUT_MS) == HAL_OK;
}

static bool MagWrite(uint8_t reg, uint8_t value)
{
  if (mag_i2c == NULL)
  {
    return false;
  }
  return HAL_I2C_Mem_Write(mag_i2c, TMAG5273_ADDRESS_HAL, reg,
                           I2C_MEMADD_SIZE_8BIT, &value, 1U,
                           MAGNETOMETER_I2C_TIMEOUT_MS) == HAL_OK;
}

static bool MagSetContinuous(bool enabled)
{
  if (!mag_ready)
  {
    return false;
  }

  if (enabled && !mag_continuous)
  {
    /*
     * In sleep mode, the first valid I2C address only wakes the TMAG5273;
     * the device intentionally NACKs it.  Generate that wake transaction,
     * then wait longer than the specified 50 us sleep-to-standby time before
     * sending the operating-mode register write.
     */
    (void)HAL_I2C_IsDeviceReady(mag_i2c, TMAG5273_ADDRESS_HAL, 1U,
                                MAGNETOMETER_I2C_TIMEOUT_MS);
    HAL_Delay(MAGNETOMETER_WAKE_DELAY_MS);
  }

  if (!MagWrite(TMAG5273_DEVICE_CONFIG_2,
                enabled ? TMAG5273_MODE_CONTINUOUS : TMAG5273_MODE_SLEEP))
  {
    return false;
  }
  mag_continuous = enabled;
  if (enabled)
  {
    HAL_Delay(MAGNETOMETER_STARTUP_DELAY_MS);
  }
  return true;
}

static int16_t MagDecodeBe16(const uint8_t *bytes)
{
  return (int16_t)(((uint16_t)bytes[0] << 8U) | bytes[1]);
}

static int32_t MagRawToMicrotesla(int16_t raw)
{
  return (int32_t)(((int64_t)raw * (int64_t)mag_range_uT) / 32768LL);
}

static uint32_t MagIntegerSquareRoot(uint64_t value)
{
  uint64_t bit = 1ULL << 62U;
  uint64_t result = 0U;

  while (bit > value)
  {
    bit >>= 2U;
  }
  while (bit != 0U)
  {
    if (value >= (result + bit))
    {
      value -= result + bit;
      result = (result >> 1U) + bit;
    }
    else
    {
      result >>= 1U;
    }
    bit >>= 2U;
  }
  return (uint32_t)result;
}

static uint32_t MagBaselineMicrotesla(void)
{
  return mag_baseline_valid ? (uint32_t)((mag_baseline_q8 + 128) >> 8U) : 0U;
}

static void MagUpdateMetric(MagnetometerSample *sample)
{
  int64_t x_squared;
  int64_t y_squared;
  int64_t z_squared;

  x_squared = (int64_t)sample->x_uT * sample->x_uT;
  y_squared = (int64_t)sample->y_uT * sample->y_uT;
  z_squared = (int64_t)sample->z_uT * sample->z_uT;
  sample->magnitude_uT = MagIntegerSquareRoot(
    (uint64_t)(x_squared + y_squared + z_squared));
  sample->baseline_uT = MagBaselineMicrotesla();
  sample->emf_uT = (sample->magnitude_uT >= sample->baseline_uT)
    ? sample->magnitude_uT - sample->baseline_uT
    : sample->baseline_uT - sample->magnitude_uT;
}

static void MagSetBaseline(const MagnetometerSample *sample, bool immediate)
{
  int32_t target_q8 = (int32_t)(sample->magnitude_uT << 8U);

  if (immediate || !mag_baseline_valid)
  {
    mag_baseline_q8 = target_q8;
    mag_baseline_valid = true;
  }
  else
  {
    int32_t difference = target_q8 - mag_baseline_q8;
    mag_baseline_q8 += difference / (1L << EMF_BASELINE_FILTER_SHIFT);
  }
}

static bool MagReadSample(MagnetometerSample *sample)
{
  uint8_t bytes[7];

  if ((sample == NULL) || !mag_continuous ||
      !MagRead(TMAG5273_X_MSB_RESULT, bytes, sizeof(bytes)))
  {
    return false;
  }
  sample->raw_x = MagDecodeBe16(&bytes[0]);
  sample->raw_y = MagDecodeBe16(&bytes[2]);
  sample->raw_z = MagDecodeBe16(&bytes[4]);
  sample->x_uT = MagRawToMicrotesla(sample->raw_x);
  sample->y_uT = MagRawToMicrotesla(sample->raw_y);
  sample->z_uT = MagRawToMicrotesla(sample->raw_z);
  sample->conversion_status = bytes[6];
  MagUpdateMetric(sample);
  return true;
}

static void MagFormatSample(const MagnetometerSample *sample, char *response,
                            size_t response_size)
{
  (void)snprintf(response, response_size,
                 "MAG X=%lduT Y=%lduT Z=%lduT RAW=%d,%d,%d SET=%u READY=%u DIAG=%u\r\n",
                 (long)sample->x_uT, (long)sample->y_uT,
                 (long)sample->z_uT, sample->raw_x, sample->raw_y,
                 sample->raw_z,
                 (unsigned int)(sample->conversion_status >> 5U),
                 (sample->conversion_status & TMAG5273_RESULT_READY) != 0U,
                 (sample->conversion_status & TMAG5273_DIAGNOSTIC_ERROR) != 0U);
}

static void EmfFormatSample(const MagnetometerSample *sample, char *response,
                            size_t response_size)
{
  (void)snprintf(response, response_size, "EMF=%luuT\r\n",
                 (unsigned long)sample->emf_uT);
}

static bool MagParsePeriod(const char *text, uint32_t *period_ms)
{
  uint32_t value = 0U;
  bool have_digit = false;

  if ((text == NULL) || (period_ms == NULL))
  {
    return false;
  }
  while ((*text == ' ') || (*text == '\t'))
  {
    ++text;
  }
  while ((*text >= '0') && (*text <= '9'))
  {
    have_digit = true;
    if (value > 1000000U)
    {
      return false;
    }
    value = (value * 10U) + (uint32_t)(*text - '0');
    ++text;
  }
  while ((*text == ' ') || (*text == '\t'))
  {
    ++text;
  }
  if (!have_digit || (*text != '\0'))
  {
    return false;
  }
  *period_ms = value;
  return true;
}

static void MagSendOneSample(void)
{
  MagnetometerSample sample;
  char response[144];
  bool was_continuous = mag_continuous;

  if (!was_continuous && !MagSetContinuous(true))
  {
    (void)UsbTest_SendText("ERR MAG failed to start conversion\r\n");
    return;
  }
  if (!MagReadSample(&sample))
  {
    (void)UsbTest_SendText("ERR MAG sample read failed\r\n");
  }
  else
  {
    MagFormatSample(&sample, response, sizeof(response));
    (void)UsbTest_SendText(response);
  }
  if (!was_continuous)
  {
    (void)MagSetContinuous(false);
  }
}

static bool MagAcquireSample(MagnetometerSample *sample)
{
  bool was_continuous = mag_continuous;
  bool success;

  if (!was_continuous && !MagSetContinuous(true))
  {
    return false;
  }
  success = MagReadSample(sample);
  if (!was_continuous && !MagSetContinuous(false))
  {
    success = false;
  }
  return success;
}

static void EmfSendOneSample(bool include_details)
{
  MagnetometerSample sample;
  char response[144];

  if (!MagAcquireSample(&sample))
  {
    (void)UsbTest_SendText("ERR EMF sample read failed\r\n");
    return;
  }
  if (include_details)
  {
    (void)snprintf(response, sizeof(response),
                   "OK EMF=%luuT FIELD=%luuT BASELINE=%luuT\r\n",
                   (unsigned long)sample.emf_uT,
                   (unsigned long)sample.magnitude_uT,
                   (unsigned long)sample.baseline_uT);
  }
  else
  {
    EmfFormatSample(&sample, response, sizeof(response));
  }
  (void)UsbTest_SendText(response);
}

bool MagnetometerTest_Start(I2C_HandleTypeDef *i2c)
{
  MagnetometerSample sample;
  uint8_t manufacturer[2];
  uint8_t variant;
  char sample_text[144];

  mag_i2c = i2c;
  mag_ready = false;
  mag_continuous = false;
  mag_baseline_valid = false;
  mag_stream_mode = MAG_STREAM_NONE;

  printf("\r\n[mag] TMAG5273 magnetometer test\r\n");
  printf("[mag] I2C2 PB10/PB11; expected address 0x35; MAG_INT PC7 unused\r\n");
  if ((mag_i2c == NULL) ||
      (HAL_I2C_IsDeviceReady(mag_i2c, TMAG5273_ADDRESS_HAL, 2U,
                             MAGNETOMETER_I2C_TIMEOUT_MS) != HAL_OK) ||
      !MagRead(TMAG5273_DEVICE_ID, &mag_device_id, 1U) ||
      !MagRead(TMAG5273_MANUFACTURER_ID_LSB, manufacturer,
               sizeof(manufacturer)))
  {
    printf("[mag] FAIL: no response from address 0x35\r\n");
    return false;
  }

  mag_manufacturer_id = (uint16_t)manufacturer[0] |
                        ((uint16_t)manufacturer[1] << 8U);
  variant = mag_device_id & 0x03U;
  if ((mag_manufacturer_id != TMAG5273_MANUFACTURER_ID) ||
      ((variant != TMAG5273_VARIANT_A1) &&
       (variant != TMAG5273_VARIANT_A2)))
  {
    printf("[mag] FAIL: manufacturer=0x%04X device=0x%02X\r\n",
           mag_manufacturer_id, mag_device_id);
    return false;
  }
  mag_range_uT = (variant == TMAG5273_VARIANT_A2) ? 133000U : 40000U;

  if (!MagWrite(TMAG5273_DEVICE_CONFIG_2, 0x00U) ||
      !MagWrite(TMAG5273_DEVICE_CONFIG_1, TMAG5273_CONFIG_32X_AVERAGING) ||
      !MagWrite(TMAG5273_SENSOR_CONFIG_1, TMAG5273_CONFIG_XYZ) ||
      !MagWrite(TMAG5273_SENSOR_CONFIG_2, 0x00U) ||
      !MagWrite(TMAG5273_T_CONFIG, 0x00U) ||
      !MagWrite(TMAG5273_INT_CONFIG_1, 0x00U))
  {
    printf("[mag] FAIL: configuration write\r\n");
    return false;
  }

  mag_ready = true;
  if (!MagSetContinuous(true) || !MagReadSample(&sample))
  {
    printf("[mag] FAIL: initial conversion\r\n");
    if (mag_continuous)
    {
      (void)MagSetContinuous(false);
    }
    mag_ready = false;
    return false;
  }
  MagSetBaseline(&sample, true);
  MagUpdateMetric(&sample);
  mag_next_baseline_ms = HAL_GetTick() + EMF_BASELINE_UPDATE_PERIOD_MS;
  MagFormatSample(&sample, sample_text, sizeof(sample_text));
  printf("[mag] PASS: manufacturer=0x%04X device=0x%02X variant=A%u range=+/- %lu uT\r\n",
         mag_manufacturer_id, mag_device_id,
         (variant == TMAG5273_VARIANT_A2) ? 2U : 1U,
         (unsigned long)mag_range_uT);
  printf("[mag] initial: %s", sample_text);
  if (!MagSetContinuous(false))
  {
    printf("[mag] WARN: could not enter sleep mode\r\n");
  }
  else
  {
    printf("[mag] sleeping; use MAG READ or MAG STREAM START [period-ms]\r\n");
  }
  return true;
}

bool MagnetometerTest_HandleCommand(const char *command)
{
  uint32_t period_ms = MAGNETOMETER_DEFAULT_PERIOD_MS;
  char response[128];
  bool is_mag_command;
  bool is_emf_command;

  if (command == NULL)
  {
    return false;
  }
  is_mag_command = (strncmp(command, "MAG", 3U) == 0) &&
                   ((command[3] == '\0') || (command[3] == ' ') ||
                    (command[3] == '\t'));
  is_emf_command = (strncmp(command, "EMF", 3U) == 0) &&
                   ((command[3] == '\0') || (command[3] == ' ') ||
                    (command[3] == '\t'));
  if (!is_mag_command && !is_emf_command)
  {
    return false;
  }
  if (!mag_ready)
  {
    (void)UsbTest_SendText("ERR sensor unavailable; check boot log and I2C2\r\n");
    return true;
  }
  if (is_mag_command)
  {
    if (strcmp(command, "MAG READ") == 0)
    {
      MagSendOneSample();
    }
    else if (strcmp(command, "MAG STATUS") == 0)
    {
      const char *stream_name = (mag_stream_mode == MAG_STREAM_AXES) ? "MAG" :
                                (mag_stream_mode == MAG_STREAM_EMF) ? "EMF" : "OFF";
      (void)snprintf(response, sizeof(response),
                     "OK MAG ID=0x%02X MFG=0x%04X RANGE=%luuT STREAM=%s PERIOD=%lums\r\n",
                     mag_device_id, mag_manufacturer_id,
                     (unsigned long)mag_range_uT, stream_name,
                     (unsigned long)mag_stream_period_ms);
      (void)UsbTest_SendText(response);
    }
    else if ((strcmp(command, "MAG STREAM START") == 0) ||
             (strncmp(command, "MAG STREAM START ", 17U) == 0))
    {
      if (command[16] != '\0')
      {
        if (!MagParsePeriod(&command[17], &period_ms))
        {
          (void)UsbTest_SendText("ERR usage: MAG STREAM START [period-ms]\r\n");
          return true;
        }
      }
      if ((period_ms < MAGNETOMETER_MIN_PERIOD_MS) ||
          (period_ms > MAGNETOMETER_MAX_PERIOD_MS))
      {
        (void)UsbTest_SendText("ERR MAG period range: 50..60000 ms\r\n");
        return true;
      }
      if (!mag_continuous && !MagSetContinuous(true))
      {
        (void)UsbTest_SendText("ERR MAG failed to start conversion\r\n");
        return true;
      }
      mag_stream_period_ms = period_ms;
      mag_stream_mode = MAG_STREAM_AXES;
      mag_next_stream_ms = HAL_GetTick() + mag_stream_period_ms;
      (void)snprintf(response, sizeof(response),
                     "OK MAG STREAM START period=%lu ms\r\n",
                     (unsigned long)mag_stream_period_ms);
      (void)UsbTest_SendText(response);
    }
    else if (strcmp(command, "MAG STREAM STOP") == 0)
    {
      mag_stream_mode = MAG_STREAM_NONE;
      if (!MagSetContinuous(false))
      {
        (void)UsbTest_SendText("ERR MAG stream stopped but sleep write failed\r\n");
      }
      else
      {
        (void)UsbTest_SendText("OK MAG STREAM STOP; sensor sleeping\r\n");
      }
    }
    else
    {
      (void)UsbTest_SendText(
        "ERR MAG commands: READ, STATUS, STREAM START [ms], STREAM STOP\r\n");
    }
    return true;
  }

  if (strcmp(command, "EMF READ") == 0)
  {
    EmfSendOneSample(false);
  }
  else if (strcmp(command, "EMF STATUS") == 0)
  {
    EmfSendOneSample(true);
  }
  else if (strcmp(command, "EMF ZERO") == 0)
  {
    MagnetometerSample sample;

    if (!MagAcquireSample(&sample))
    {
      (void)UsbTest_SendText("ERR EMF baseline read failed\r\n");
    }
    else
    {
      MagSetBaseline(&sample, true);
      MagUpdateMetric(&sample);
      mag_next_baseline_ms = HAL_GetTick() + EMF_BASELINE_UPDATE_PERIOD_MS;
      (void)snprintf(response, sizeof(response),
                     "OK EMF ZERO BASELINE=%luuT\r\n",
                     (unsigned long)sample.baseline_uT);
      (void)UsbTest_SendText(response);
    }
  }
  else if ((strcmp(command, "EMF STREAM START") == 0) ||
           (strncmp(command, "EMF STREAM START ", 17U) == 0))
  {
    if (command[16] != '\0')
    {
      if (!MagParsePeriod(&command[17], &period_ms))
      {
        (void)UsbTest_SendText("ERR usage: EMF STREAM START [period-ms]\r\n");
        return true;
      }
    }
    if ((period_ms < MAGNETOMETER_MIN_PERIOD_MS) ||
        (period_ms > MAGNETOMETER_MAX_PERIOD_MS))
    {
      (void)UsbTest_SendText("ERR EMF period range: 50..60000 ms\r\n");
      return true;
    }
    if (!mag_continuous && !MagSetContinuous(true))
    {
      (void)UsbTest_SendText("ERR EMF failed to start conversion\r\n");
      return true;
    }
    mag_stream_period_ms = period_ms;
    mag_stream_mode = MAG_STREAM_EMF;
    mag_next_stream_ms = HAL_GetTick() + mag_stream_period_ms;
    (void)snprintf(response, sizeof(response),
                   "OK EMF STREAM START period=%lu ms\r\n",
                   (unsigned long)mag_stream_period_ms);
    (void)UsbTest_SendText(response);
  }
  else if (strcmp(command, "EMF STREAM STOP") == 0)
  {
    mag_stream_mode = MAG_STREAM_NONE;
    if (!MagSetContinuous(false))
    {
      (void)UsbTest_SendText("ERR EMF stream stopped but sleep write failed\r\n");
    }
    else
    {
      (void)UsbTest_SendText("OK EMF STREAM STOP; sensor sleeping\r\n");
    }
  }
  else
  {
    (void)UsbTest_SendText(
      "ERR EMF commands: READ, STATUS, ZERO, STREAM START [ms], STREAM STOP\r\n");
  }
  return true;
}

bool MagnetometerTest_Sleep(void)
{
  mag_stream_mode = MAG_STREAM_NONE;
  if (!mag_ready || !mag_continuous)
  {
    return mag_ready;
  }
  return MagSetContinuous(false);
}

void MagnetometerTest_Service(void)
{
  MagnetometerSample sample;
  char response[144];
  uint32_t now;

  if (!mag_ready)
  {
    return;
  }
  now = HAL_GetTick();
  if ((int32_t)(now - mag_next_baseline_ms) >= 0)
  {
    bool was_continuous = mag_continuous;

    if ((!was_continuous && MagSetContinuous(true)) || was_continuous)
    {
      if (MagReadSample(&sample))
      {
        MagSetBaseline(&sample, false);
        MagUpdateMetric(&sample);
      }
      if (!was_continuous)
      {
        (void)MagSetContinuous(false);
      }
    }
    mag_next_baseline_ms = now + EMF_BASELINE_UPDATE_PERIOD_MS;
  }
  if ((mag_stream_mode == MAG_STREAM_NONE) || !mag_continuous ||
      ((int32_t)(now - mag_next_stream_ms) < 0))
  {
    return;
  }
  mag_next_stream_ms = now + mag_stream_period_ms;
  if (!MagReadSample(&sample))
  {
    (void)UsbTest_SendText("ERR MAG stream sample failed\r\n");
    return;
  }
  if (mag_stream_mode == MAG_STREAM_EMF)
  {
    EmfFormatSample(&sample, response, sizeof(response));
  }
  else
  {
    MagFormatSample(&sample, response, sizeof(response));
  }
  (void)UsbTest_SendText(response);
}
