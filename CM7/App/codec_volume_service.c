/* Extracted from the proven M7 radio/headphone bring-up without changing the
 * codec register sequence, volume mapping or polling cadence. */
#include "codec_volume_service.h"

#include "main.h"

#include <stdio.h>

#define SGTL5000_I2C_ADDRESS_HAL       (0x0AU << 1U)
#define SGTL5000_CHIP_ID_REGISTER      0x0000U
#define SGTL5000_CHIP_DIG_POWER        0x0002U
#define SGTL5000_CHIP_CLK_CTRL         0x0004U
#define SGTL5000_CHIP_I2S_CTRL         0x0006U
#define SGTL5000_CHIP_SSS_CTRL         0x000AU
#define SGTL5000_CHIP_ADCDAC_CTRL      0x000EU
#define SGTL5000_CHIP_DAC_VOL          0x0010U
#define SGTL5000_CHIP_ANA_HP_CTRL      0x0022U
#define SGTL5000_CHIP_ANA_CTRL         0x0024U
#define SGTL5000_CHIP_LINREG_CTRL      0x0026U
#define SGTL5000_CHIP_REF_CTRL         0x0028U
#define SGTL5000_CHIP_ANA_POWER        0x0030U
#define SGTL5000_CHIP_SHORT_CTRL       0x003CU

#define CODEC_I2C_TIMEOUT_MS           100U
#define DEFAULT_HP_VOLUME_CODE         0x54U /* -30 dB; 0x18 is 0 dB. */
#define VOLUME_SAMPLE_PERIOD_MS        10U
#define VOLUME_MUTE_THRESHOLD          1024U
#define HP_VOLUME_0DB_CODE             0x18U
#define HP_VOLUME_MIN_CODE             0x7FU

typedef struct
{
  uint16_t reg;
  uint16_t value;
} CodecRegisterValue;

static I2C_HandleTypeDef *codec_i2c;
static ADC_HandleTypeDef *codec_volume_adc;
static SAI_HandleTypeDef *codec_sai;
static bool codec_ready;
static bool volume_ready;
static bool volume_muted = true;
static bool transition_muted;
static bool headphone_inserted;
static CodecVolumeError last_error;
static uint32_t volume_filtered;
static uint32_t volume_last_sample_tick;
static uint8_t volume_last_code = DEFAULT_HP_VOLUME_CODE;

static bool ReadJack(GPIO_TypeDef *port, uint16_t pin)
{
  return HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_RESET;
}

static HAL_StatusTypeDef StartMasterClock(SAI_HandleTypeDef *sai,
                                          uint32_t *mclk_hz)
{
  uint32_t sai_clock_hz;

  if ((sai == NULL) || (mclk_hz == NULL))
  {
    return HAL_ERROR;
  }

  /* SYS_MCLK must be running before the SGTL5000 control port responds. */
  __HAL_SAI_DISABLE(sai);
  sai->Init.NoDivider = SAI_MASTERDIVIDER_ENABLE;
  sai->Init.MckOverSampling = SAI_MCK_OVERSAMPLING_DISABLE;
  sai->Init.OutputDrive = SAI_OUTPUTDRIVE_ENABLE;
  sai->Init.FIFOThreshold = SAI_FIFOTHRESHOLD_1QF;
  sai->Init.AudioFrequency = SAI_AUDIO_FREQUENCY_48K;
  sai->Init.MckOutput = SAI_MCK_OUTPUT_ENABLE;
  if (HAL_SAI_InitProtocol(sai, SAI_I2S_STANDARD,
                           SAI_PROTOCOL_DATASIZE_16BIT, 2U) != HAL_OK)
  {
    return HAL_ERROR;
  }

  __HAL_SAI_ENABLE(sai);
  HAL_Delay(1U);

  sai_clock_hz = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_SAI1);
  *mclk_hz = (sai->Init.Mckdiv != 0U)
               ? sai_clock_hz / sai->Init.Mckdiv
               : sai_clock_hz;
  return HAL_OK;
}

bool CodecVolume_ProbeControlPath(I2C_HandleTypeDef *i2c,
                                 SAI_HandleTypeDef *sai)
{
  uint8_t data[2] = {0U, 0U};
  uint16_t chip_id;
  uint32_t mclk_hz = 0U;

  if ((i2c == NULL) || (sai == NULL))
  {
    return false;
  }

  printf("\r\n[audio] SGTL5000 control-path smoke test\r\n");
  printf("[audio] I2C4 PF14/PF15; expected address 0x0A\r\n");

  if (StartMasterClock(sai, &mclk_hz) != HAL_OK)
  {
    printf("[audio] FAIL: could not start SAI1 MCLK\r\n");
    return false;
  }
  printf("[audio] PE2 MCLK approximately %lu Hz\r\n",
         (unsigned long)mclk_hz);

  if (HAL_I2C_IsDeviceReady(i2c, SGTL5000_I2C_ADDRESS_HAL, 3U,
                            CODEC_I2C_TIMEOUT_MS) != HAL_OK)
  {
    printf("[audio] FAIL: no ACK at 0x0A; I2C error=0x%08lX\r\n",
           (unsigned long)HAL_I2C_GetError(i2c));
    __HAL_SAI_DISABLE(sai);
    return false;
  }

  if (HAL_I2C_Mem_Read(i2c, SGTL5000_I2C_ADDRESS_HAL,
                       SGTL5000_CHIP_ID_REGISTER, I2C_MEMADD_SIZE_16BIT,
                       data, sizeof(data), CODEC_I2C_TIMEOUT_MS) != HAL_OK)
  {
    printf("[audio] FAIL: CHIP_ID read; I2C error=0x%08lX\r\n",
           (unsigned long)HAL_I2C_GetError(i2c));
    __HAL_SAI_DISABLE(sai);
    return false;
  }

  chip_id = ((uint16_t)data[0] << 8) | data[1];
  __HAL_SAI_DISABLE(sai);
  if ((chip_id >> 8) != 0xA0U)
  {
    printf("[audio] FAIL: CHIP_ID=0x%04X, expected 0xA0xx\r\n", chip_id);
    return false;
  }

  printf("[audio] PASS: CHIP_ID=0x%04X; MCLK stopped; outputs unchanged\r\n",
         chip_id);
  return true;
}

static bool WriteChecked(uint16_t reg, uint16_t value)
{
  uint8_t data[2] = {(uint8_t)(value >> 8), (uint8_t)value};
  uint8_t readback_data[2] = {0U, 0U};
  uint16_t readback;
  uint16_t verify_mask = 0xFFFFU;

  if ((codec_i2c == NULL) ||
      (HAL_I2C_Mem_Write(codec_i2c, SGTL5000_I2C_ADDRESS_HAL, reg,
                         I2C_MEMADD_SIZE_16BIT, data, sizeof(data),
                         CODEC_I2C_TIMEOUT_MS) != HAL_OK) ||
      (HAL_I2C_Mem_Read(codec_i2c, SGTL5000_I2C_ADDRESS_HAL, reg,
                        I2C_MEMADD_SIZE_16BIT, readback_data,
                        sizeof(readback_data), CODEC_I2C_TIMEOUT_MS) != HAL_OK))
  {
    printf("[audio] FAIL: codec register 0x%04X I2C access\r\n", reg);
    return false;
  }

  readback = ((uint16_t)readback_data[0] << 8) | readback_data[1];
  if (reg == SGTL5000_CHIP_ADCDAC_CTRL)
  {
    /* DAC volume-ramp busy flags are live read-only bits. */
    verify_mask = 0xCFFFU;
  }
  if ((readback & verify_mask) != (value & verify_mask))
  {
    printf("[audio] FAIL: codec reg 0x%04X wrote 0x%04X read 0x%04X\r\n",
           reg, value, readback);
    return false;
  }
  return true;
}

static bool ApplyOutputState(void)
{
  const bool effective_mute = transition_muted || !headphone_inserted ||
                              (volume_ready && volume_muted);
  const uint8_t volume_code = volume_ready
    ? volume_last_code : DEFAULT_HP_VOLUME_CODE;

  if (!codec_ready)
  {
    return false;
  }
  if (effective_mute)
  {
    if (!WriteChecked(SGTL5000_CHIP_ANA_CTRL, 0x0133U) ||
        !WriteChecked(SGTL5000_CHIP_ADCDAC_CTRL, 0x020CU))
    {
      return false;
    }
  }

  if (!WriteChecked(SGTL5000_CHIP_ANA_HP_CTRL,
                    ((uint16_t)volume_code << 8) | volume_code))
  {
    return false;
  }

  if (!effective_mute)
  {
    return WriteChecked(SGTL5000_CHIP_ADCDAC_CTRL, 0x0200U) &&
           WriteChecked(SGTL5000_CHIP_ANA_CTRL, 0x0123U);
  }
  return true;
}

static bool ReadVolumeRaw(uint16_t *raw)
{
  if ((codec_volume_adc == NULL) || (raw == NULL) ||
      (HAL_ADC_Start(codec_volume_adc) != HAL_OK) ||
      (HAL_ADC_PollForConversion(codec_volume_adc, 2U) != HAL_OK))
  {
    if (codec_volume_adc != NULL)
    {
      (void)HAL_ADC_Stop(codec_volume_adc);
    }
    return false;
  }
  *raw = (uint16_t)HAL_ADC_GetValue(codec_volume_adc);
  (void)HAL_ADC_Stop(codec_volume_adc);
  return true;
}

static bool InitVolume(void)
{
  uint32_t initial_sum = 0U;

  volume_ready = false;
  if ((codec_volume_adc == NULL) ||
      (HAL_ADCEx_Calibration_Start(codec_volume_adc,
                                  ADC_CALIB_OFFSET_LINEARITY,
                                  ADC_SINGLE_ENDED) != HAL_OK))
  {
    printf("[audio] FAIL: ADC3 calibration for volume pot\r\n");
    return false;
  }
  for (uint32_t index = 0U; index < 8U; ++index)
  {
    uint16_t raw;
    if (!ReadVolumeRaw(&raw))
    {
      printf("[audio] FAIL: volume-pot ADC conversion\r\n");
      return false;
    }
    initial_sum += raw;
  }
  volume_filtered = initial_sum / 8U;
  volume_last_sample_tick = HAL_GetTick();
  volume_last_code = DEFAULT_HP_VOLUME_CODE;
  volume_muted = true;
  volume_ready = true;
  printf("[audio] volume pot PF10/ADC3 ready; initial ADC=%lu\r\n",
         (unsigned long)volume_filtered);
  return true;
}

static bool ServiceVolume(bool force)
{
  const uint32_t now = HAL_GetTick();
  uint32_t scaled;
  uint32_t code_delta;
  uint16_t raw;
  uint8_t code;
  bool muted;
  uint8_t previous_code;
  bool previous_muted;

  if (!volume_ready)
  {
    return false;
  }
  if (!force && ((now - volume_last_sample_tick) < VOLUME_SAMPLE_PERIOD_MS))
  {
    return true;
  }
  volume_last_sample_tick = now;
  if (!ReadVolumeRaw(&raw))
  {
    printf("[audio] FAIL: volume-pot ADC conversion\r\n");
    return false;
  }

  /* A 1/8 IIR filter rejects wiper noise without making the knob sluggish. */
  volume_filtered = ((volume_filtered * 7U) + raw + 4U) / 8U;
  muted = volume_filtered <= VOLUME_MUTE_THRESHOLD;
  if (muted)
  {
    code = HP_VOLUME_MIN_CODE;
  }
  else
  {
    scaled = ((volume_filtered - VOLUME_MUTE_THRESHOLD) *
              (HP_VOLUME_MIN_CODE - HP_VOLUME_0DB_CODE)) /
             (65535U - VOLUME_MUTE_THRESHOLD);
    code = (uint8_t)(HP_VOLUME_MIN_CODE - scaled);
  }

  code_delta = (code > volume_last_code) ? (code - volume_last_code)
                                         : (volume_last_code - code);
  /* One-dB hysteresis plus the codec zero-cross detector limits zipper noise. */
  if (!force && (muted == volume_muted) && (code_delta < 2U))
  {
    return true;
  }

  previous_code = volume_last_code;
  previous_muted = volume_muted;
  volume_last_code = code;
  volume_muted = muted;
  if (muted != previous_muted)
  {
    if (!ApplyOutputState())
    {
      volume_last_code = previous_code;
      volume_muted = previous_muted;
      return false;
    }
  }
  else if (!WriteChecked(SGTL5000_CHIP_ANA_HP_CTRL,
                         ((uint16_t)code << 8) | code))
  {
    volume_last_code = previous_code;
    volume_muted = previous_muted;
    return false;
  }

  if (muted)
  {
    printf("[audio] volume ADC=%lu MUTED\r\n",
           (unsigned long)volume_filtered);
  }
  else
  {
    const uint32_t attenuation_half_db = code - HP_VOLUME_0DB_CODE;
    printf("[audio] volume ADC=%lu -%lu.%lu dB\r\n",
           (unsigned long)volume_filtered,
           (unsigned long)(attenuation_half_db / 2U),
           (unsigned long)((attenuation_half_db & 1U) ? 5U : 0U));
  }
  return true;
}

static bool StartDigitalHeadphones(void)
{
  static const CodecRegisterValue startup[] =
  {
    {SGTL5000_CHIP_ANA_CTRL,    0x0133U},
    {SGTL5000_CHIP_ADCDAC_CTRL, 0x020CU},
    {SGTL5000_CHIP_ANA_POWER,   0x4260U},
    {SGTL5000_CHIP_LINREG_CTRL, 0x006CU},
    {SGTL5000_CHIP_REF_CTRL,    0x01EFU},
    {SGTL5000_CHIP_SHORT_CTRL,  0x1106U},
    {SGTL5000_CHIP_CLK_CTRL,    0x0008U},
    {SGTL5000_CHIP_I2S_CTRL,    0x0130U},
    {SGTL5000_CHIP_SSS_CTRL,    0x0010U},
    {SGTL5000_CHIP_DAC_VOL,     0x3C3CU},
    {SGTL5000_CHIP_ANA_HP_CTRL, 0x7F7FU},
    {SGTL5000_CHIP_DIG_POWER,   0x0021U},
    {SGTL5000_CHIP_ANA_POWER,   0x42FCU}
  };
  uint8_t chip_id_data[2];
  uint16_t chip_id;
  uint32_t mclk_hz = 0U;

  printf("\r\n[audio] SGTL5000 I2S -> DAC -> headphone setup\r\n");
  if (StartMasterClock(codec_sai, &mclk_hz) != HAL_OK)
  {
    printf("[audio] FAIL: SAI1 MCLK start\r\n");
    return false;
  }
  printf("[audio] PE2 MCLK approximately %lu Hz\r\n",
         (unsigned long)mclk_hz);

  if (HAL_I2C_Mem_Read(codec_i2c, SGTL5000_I2C_ADDRESS_HAL,
                       SGTL5000_CHIP_ID_REGISTER, I2C_MEMADD_SIZE_16BIT,
                       chip_id_data, sizeof(chip_id_data),
                       CODEC_I2C_TIMEOUT_MS) != HAL_OK)
  {
    printf("[audio] FAIL: CHIP_ID read\r\n");
    return false;
  }
  chip_id = ((uint16_t)chip_id_data[0] << 8) | chip_id_data[1];
  if ((chip_id >> 8) != 0xA0U)
  {
    printf("[audio] FAIL: CHIP_ID=0x%04X\r\n", chip_id);
    return false;
  }

  for (uint32_t index = 0U; index < sizeof(startup) / sizeof(startup[0]);
       ++index)
  {
    if (!WriteChecked(startup[index].reg, startup[index].value))
    {
      return false;
    }
  }
  HAL_Delay(450U);
  printf("[audio] PASS: CHIP_ID=0x%04X; headphone path configured muted\r\n",
         chip_id);
  return true;
}

bool CodecVolume_Init(I2C_HandleTypeDef *i2c, ADC_HandleTypeDef *volume_adc,
                      SAI_HandleTypeDef *sai)
{
  if ((i2c == NULL) || (volume_adc == NULL) || (sai == NULL))
  {
    return false;
  }

  codec_i2c = i2c;
  codec_volume_adc = volume_adc;
  codec_sai = sai;
  codec_ready = false;
  volume_ready = false;
  transition_muted = false;
  headphone_inserted = false;
  last_error = CODEC_VOLUME_ERROR_NONE;

  if (!InitVolume() || !StartDigitalHeadphones())
  {
    return false;
  }
  codec_ready = true;
  return ServiceVolume(true);
}

bool CodecVolume_Service(void)
{
  const bool inserted = ReadJack(HEADPHONE_JACK_DETECT_GPIO_Port,
                                 HEADPHONE_JACK_DETECT_Pin);

  last_error = CODEC_VOLUME_ERROR_NONE;
  if (!codec_ready)
  {
    last_error = CODEC_VOLUME_ERROR_OUTPUT;
    return false;
  }
  if (!ServiceVolume(false))
  {
    last_error = CODEC_VOLUME_ERROR_ADC;
    return false;
  }
  if (inserted != headphone_inserted)
  {
    const bool previous_inserted = headphone_inserted;
    headphone_inserted = inserted;
    if (!ApplyOutputState())
    {
      headphone_inserted = previous_inserted;
      last_error = CODEC_VOLUME_ERROR_OUTPUT;
      return false;
    }
    printf("[audio] headphones %s; output %s\r\n",
           inserted ? "inserted" : "removed",
           inserted ? (volume_muted ? "muted by volume pot" :
                         "enabled under volume-pot control") : "muted");
  }
  return true;
}

bool CodecVolume_RefreshVolume(void)
{
  last_error = CODEC_VOLUME_ERROR_NONE;
  if (!codec_ready)
  {
    last_error = CODEC_VOLUME_ERROR_OUTPUT;
    return false;
  }
  if (!ServiceVolume(true))
  {
    last_error = CODEC_VOLUME_ERROR_ADC;
    return false;
  }
  return true;
}

bool CodecVolume_SetTransitionMuted(bool muted)
{
  const bool previous = transition_muted;

  if (!codec_ready)
  {
    return false;
  }
  transition_muted = muted;
  /* The legacy path did not issue an extra codec transaction when a band
   * transition ended with no headphones present. Clear the gate now; the next
   * insertion event will apply the resulting output state. */
  if (!muted && !headphone_inserted)
  {
    return true;
  }
  if (!ApplyOutputState())
  {
    transition_muted = previous;
    return false;
  }
  return true;
}

bool CodecVolume_GetStatus(CodecVolumeStatus *status)
{
  if (status == NULL)
  {
    return false;
  }
  status->ready = codec_ready && volume_ready;
  status->volume_muted = volume_muted;
  status->transition_muted = transition_muted;
  status->line_in_inserted = ReadJack(LINE_IN_JACK_DETECT_GPIO_Port,
                                      LINE_IN_JACK_DETECT_Pin);
  status->headphone_inserted =
    ReadJack(HEADPHONE_JACK_DETECT_GPIO_Port, HEADPHONE_JACK_DETECT_Pin);
  status->volume_adc = volume_filtered;
  status->headphone_volume_code = volume_last_code;
  status->attenuation_half_db = volume_last_code - HP_VOLUME_0DB_CODE;
  status->last_error = last_error;
  return true;
}
