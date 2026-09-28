#include "sd_test.h"

#include "ff.h"
#include "storage_service.h"
#include "usb_test.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SD_TEST_DEFAULT_MIB     64U
#define SD_TEST_MAX_MIB       1024U
#define SD_TEST_DEFAULT_PASSES   1U
#define SD_TEST_MAX_PASSES     100U
#define SD_TEST_MAX_TOTAL_MIB 8192U
#define SD_TEST_FILE_NAME       "SDTEST.BIN"
#define SD_IO_BUFFER_BYTES   16384U

static bool sd_card_was_present;
static uint8_t sd_io_buffer[SD_IO_BUFFER_BYTES];
static uint8_t sd_expected_buffer[SD_IO_BUFFER_BYTES];
typedef enum {SD_STRESS_IDLE, SD_STRESS_WRITE, SD_STRESS_VERIFY} SdStressState;
static SdStressState sd_stress_state;
static FIL sd_stress_file;
static bool sd_stress_file_open;
static uint32_t sd_stress_size_mib;
static uint32_t sd_stress_passes;
static uint32_t sd_stress_pass;
static uint32_t sd_stress_remaining;
static uint32_t sd_stress_offset;
static uint32_t sd_stress_chunk;
static uint32_t sd_stress_started_ms;
static uint32_t sd_stress_written;
static uint32_t sd_stress_verified;
static uint32_t sd_stress_write_max_ms;
static uint32_t sd_stress_read_max_ms;

static void SdSend(const char *text)
{
  uint32_t start_ms = HAL_GetTick();

  while (!UsbTest_SendText(text) &&
         ((HAL_GetTick() - start_ms) < 250U))
  {
    HAL_Delay(1U);
  }
  printf("[sd] %s", text);
}

static void SdStatus(void)
{
  HAL_SD_CardInfoTypeDef info;
  FRESULT result;
  uint64_t free_bytes = 0U;
  uint32_t capacity_mib;
  uint32_t free_mib;
  const char *card_type;
  char response[192];

  if (!StorageService_CardPresent())
  {
    SdSend("ERR SD no card detected\r\n");
    return;
  }

  result = StorageService_Acquire(STORAGE_OWNER_STATUS);
  if (result != FR_OK)
  {
    (void)snprintf(response, sizeof(response),
                   "ERR SD mount failed result=%s(%u) hal=0x%08lX\r\n",
                   StorageService_ResultName(result), (unsigned int)result,
                   (unsigned long)StorageService_HalError());
    SdSend(response);
    return;
  }
  if (!StorageService_GetCardInfo(STORAGE_OWNER_STATUS, &info))
  {
    (void)snprintf(response, sizeof(response),
                   "ERR SD card-info failed hal=0x%08lX\r\n",
                   (unsigned long)StorageService_HalError());
    (void)StorageService_Release(STORAGE_OWNER_STATUS);
    SdSend(response);
    return;
  }

  capacity_mib = (uint32_t)(((uint64_t)info.LogBlockNbr *
                             info.LogBlockSize) / (1024U * 1024U));
  card_type = (info.CardType == CARD_SDHC_SDXC) ? "SDHC/SDXC" :
              (info.CardType == CARD_SDSC) ? "SDSC" : "OTHER";

  if (StorageService_GetFreeBytes(STORAGE_OWNER_STATUS, &free_bytes))
  {
    free_mib = (uint32_t)(free_bytes / (1024U * 1024U));
    (void)snprintf(response, sizeof(response),
                   "OK SD PRESENT=1 MOUNTED=1 TYPE=%s CAPACITY=%luMiB "
                   "FREE=%luMiB BLOCKS=%lu BUS=4 CLOCKDIV=%lu\r\n",
                   card_type, (unsigned long)capacity_mib,
                   (unsigned long)free_mib, (unsigned long)info.LogBlockNbr,
                   (unsigned long)StorageService_ClockDiv());
  }
  else
  {
    (void)snprintf(response, sizeof(response),
                   "OK SD PRESENT=1 MOUNTED=1 TYPE=%s CAPACITY=%luMiB "
                   "FREE=? BLOCKS=%lu BUS=4 CLOCKDIV=%lu\r\n",
                   card_type, (unsigned long)capacity_mib,
                   (unsigned long)info.LogBlockNbr,
                   (unsigned long)StorageService_ClockDiv());
  }
  (void)StorageService_Release(STORAGE_OWNER_STATUS);
  SdSend(response);
}

static bool SdParseUint(const char **cursor, uint32_t *value)
{
  uint32_t parsed = 0U;
  bool saw_digit = false;

  while ((**cursor == ' ') || (**cursor == '\t'))
  {
    ++(*cursor);
  }
  while ((**cursor >= '0') && (**cursor <= '9'))
  {
    uint32_t digit = (uint32_t)(**cursor - '0');

    if (parsed > ((UINT32_MAX - digit) / 10U))
    {
      return false;
    }
    parsed = (parsed * 10U) + digit;
    saw_digit = true;
    ++(*cursor);
  }
  if (!saw_digit)
  {
    return false;
  }
  *value = parsed;
  return true;
}

static void SdFillPattern(uint32_t pass, uint32_t chunk_index)
{
  uint32_t state = 0xA5366B4DU ^ (pass * 0x9E3779B9U) ^ chunk_index;

  for (uint32_t index = 0U; index < sizeof(sd_io_buffer); ++index)
  {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    sd_io_buffer[index] = (uint8_t)(state >> 24);
  }
}

static void SdStressPhase(const char *phase)
{
  char response[80];

  (void)snprintf(response, sizeof(response),
                 "SD STRESS pass=%lu/%lu phase=%s\r\n",
                 (unsigned long)(sd_stress_pass + 1U),
                 (unsigned long)sd_stress_passes, phase);
  SdSend(response);
}

static void SdStressIdle(void)
{
  sd_stress_state = SD_STRESS_IDLE;
  sd_stress_file_open = false;
}

static void SdStressFailed(FRESULT result, uint32_t bad_offset)
{
  FRESULT close_result = FR_OK;
  char response[192];

  if (sd_stress_file_open)
  {
    close_result = f_close(&sd_stress_file);
  }
  SdStressIdle();
  if ((result == FR_OK) && (close_result != FR_OK))
  {
    result = close_result;
  }
  (void)StorageService_Release(STORAGE_OWNER_SD_STRESS);
  (void)snprintf(response, sizeof(response),
                 "ERR SD STRESS failed result=%s(%u) offset=%lu "
                 "hal=0x%08lX; %s retained\r\n",
                 StorageService_ResultName(result), (unsigned int)result,
                 (unsigned long)bad_offset,
                 (unsigned long)StorageService_HalError(),
                 SD_TEST_FILE_NAME);
  SdSend(response);
}

static void SdStressPassed(void)
{
  FRESULT result = FR_OK;
  uint32_t elapsed_ms;
  uint64_t io_bytes;
  uint32_t mib_per_second_x10;
  char response[224];

  if (sd_stress_file_open)
  {
    result = f_close(&sd_stress_file);
  }
  SdStressIdle();
  if (result == FR_OK)
  {
    result = f_unlink(SD_TEST_FILE_NAME);
  }
  (void)StorageService_Release(STORAGE_OWNER_SD_STRESS);
  if (result != FR_OK)
  {
    (void)snprintf(response, sizeof(response),
                   "ERR SD test passed but cleanup failed result=%s(%u); "
                   "use SD CLEAN\r\n",
                   StorageService_ResultName(result), (unsigned int)result);
    SdSend(response);
    return;
  }

  elapsed_ms = HAL_GetTick() - sd_stress_started_ms;
  if (elapsed_ms == 0U)
  {
    elapsed_ms = 1U;
  }
  io_bytes = (uint64_t)sd_stress_size_mib * 1024U * 1024U *
             sd_stress_passes * 2U;
  mib_per_second_x10 =
    (uint32_t)((io_bytes * 10000U) / elapsed_ms / (1024U * 1024U));
  (void)snprintf(response, sizeof(response),
                 "OK SD STRESS PASS size=%luMiB passes=%lu written=%lu "
                 "verified=%lu elapsed=%lums write-max=%lums read-max=%lums "
                 "aggregate=%lu.%luMiB/s file-removed=1\r\n",
                 (unsigned long)sd_stress_size_mib,
                 (unsigned long)sd_stress_passes,
                 (unsigned long)sd_stress_written,
                 (unsigned long)sd_stress_verified,
                 (unsigned long)elapsed_ms,
                 (unsigned long)sd_stress_write_max_ms,
                 (unsigned long)sd_stress_read_max_ms,
                 (unsigned long)(mib_per_second_x10 / 10U),
                 (unsigned long)(mib_per_second_x10 % 10U));
  SdSend(response);
}

static void SdStressAbort(bool announce)
{
  FRESULT result = FR_OK;
  uint32_t written = sd_stress_written;
  uint32_t verified = sd_stress_verified;
  char response[144];

  if (sd_stress_file_open)
  {
    result = f_close(&sd_stress_file);
  }
  SdStressIdle();
  if ((result == FR_OK) &&
      (StorageService_Owner() == STORAGE_OWNER_SD_STRESS))
  {
    result = f_unlink(SD_TEST_FILE_NAME);
  }
  if (StorageService_Owner() == STORAGE_OWNER_SD_STRESS)
  {
    (void)StorageService_Release(STORAGE_OWNER_SD_STRESS);
  }
  if (!announce)
  {
    if ((result != FR_OK) && (result != FR_NO_FILE))
    {
      printf("[sd] stress cleanup failed result=%s(%u)\r\n",
             StorageService_ResultName(result), (unsigned int)result);
    }
    return;
  }
  if ((result == FR_OK) || (result == FR_NO_FILE))
  {
    (void)snprintf(response, sizeof(response),
                   "OK SD STRESS STOP cleaned=1 written=%lu verified=%lu\r\n",
                   (unsigned long)written, (unsigned long)verified);
  }
  else
  {
    (void)snprintf(response, sizeof(response),
                   "ERR SD STRESS STOP cleanup failed result=%s(%u)\r\n",
                   StorageService_ResultName(result), (unsigned int)result);
  }
  SdSend(response);
}

static void SdStressStart(uint32_t size_mib, uint32_t passes)
{
  FILINFO info;
  FRESULT result;
  uint64_t free_bytes;
  uint64_t requested_bytes = (uint64_t)size_mib * 1024U * 1024U;
  char response[160];

  result = StorageService_Acquire(STORAGE_OWNER_SD_STRESS);
  if (result != FR_OK)
  {
    (void)snprintf(response, sizeof(response),
                   "ERR SD mount failed result=%s(%u) hal=0x%08lX\r\n",
                   StorageService_ResultName(result), (unsigned int)result,
                   (unsigned long)StorageService_HalError());
    SdSend(response);
    return;
  }
  result = f_stat(SD_TEST_FILE_NAME, &info);
  if (result == FR_OK)
  {
    SdSend("ERR SD SDTEST.BIN already exists; manual SD CLEAN required\r\n");
    (void)StorageService_Release(STORAGE_OWNER_SD_STRESS);
    return;
  }
  if (result != FR_NO_FILE)
  {
    (void)snprintf(response, sizeof(response),
                   "ERR SD file check failed result=%s(%u)\r\n",
                   StorageService_ResultName(result), (unsigned int)result);
    SdSend(response);
    (void)StorageService_Release(STORAGE_OWNER_SD_STRESS);
    return;
  }
  if (!StorageService_GetFreeBytes(STORAGE_OWNER_SD_STRESS, &free_bytes) ||
      (free_bytes < requested_bytes))
  {
    SdSend("ERR SD insufficient or unknown free space\r\n");
    (void)StorageService_Release(STORAGE_OWNER_SD_STRESS);
    return;
  }
  result = f_open(&sd_stress_file, SD_TEST_FILE_NAME,
                  FA_CREATE_NEW | FA_READ | FA_WRITE);
  if (result != FR_OK)
  {
    (void)snprintf(response, sizeof(response),
                   "ERR SD create failed result=%s(%u)\r\n",
                   StorageService_ResultName(result), (unsigned int)result);
    SdSend(response);
    (void)StorageService_Release(STORAGE_OWNER_SD_STRESS);
    return;
  }

  sd_stress_file_open = true;
  sd_stress_size_mib = size_mib;
  sd_stress_passes = passes;
  sd_stress_pass = 0U;
  sd_stress_remaining = size_mib * 1024U * 1024U;
  sd_stress_offset = 0U;
  sd_stress_chunk = 0U;
  sd_stress_written = 0U;
  sd_stress_verified = 0U;
  sd_stress_write_max_ms = 0U;
  sd_stress_read_max_ms = 0U;
  sd_stress_started_ms = HAL_GetTick();
  sd_stress_state = SD_STRESS_WRITE;
  (void)snprintf(response, sizeof(response),
                 "OK SD STRESS START size=%luMiB passes=%lu "
                 "file=%s chunk=%u; do not remove card\r\n",
                 (unsigned long)size_mib, (unsigned long)passes,
                 SD_TEST_FILE_NAME, (unsigned int)SD_IO_BUFFER_BYTES);
  SdSend(response);
  SdStressPhase("WRITE");
}

static void SdClean(void)
{
  FRESULT result = StorageService_Acquire(STORAGE_OWNER_STATUS);
  char response[112];

  if (result == FR_OK)
  {
    result = f_unlink(SD_TEST_FILE_NAME);
  }
  if (StorageService_Owner() == STORAGE_OWNER_STATUS)
  {
    (void)StorageService_Release(STORAGE_OWNER_STATUS);
  }
  if ((result == FR_OK) || (result == FR_NO_FILE))
  {
    SdSend("OK SD CLEAN; SDTEST.BIN absent\r\n");
    return;
  }

  (void)snprintf(response, sizeof(response),
                 "ERR SD CLEAN failed result=%s(%u)\r\n",
                 StorageService_ResultName(result), (unsigned int)result);
  SdSend(response);
}

void SdTest_Start(void)
{
  sd_card_was_present = StorageService_CardPresent();
  SdStressIdle();
  printf("[sd] CLI ready; card detect=%s, initialization deferred\r\n",
         sd_card_was_present ? "present" : "absent");
}

bool SdTest_HandleCommand(const char *command)
{
  const char *cursor;
  uint32_t size_mib = SD_TEST_DEFAULT_MIB;
  uint32_t passes = SD_TEST_DEFAULT_PASSES;

  if ((command == NULL) ||
      ((command[0] != 'S') || (command[1] != 'D') ||
       ((command[2] != '\0') && (command[2] != ' ') &&
        (command[2] != '\t'))))
  {
    return false;
  }
  if (strcmp(command, "SD STRESS STOP") == 0)
  {
    if (sd_stress_state == SD_STRESS_IDLE)
    {
      SdSend("OK SD STRESS already idle\r\n");
    }
    else
    {
      SdStressAbort(true);
    }
    return true;
  }
  if (sd_stress_state != SD_STRESS_IDLE)
  {
    SdSend("ERR SD stress active; use SD STRESS STOP\r\n");
    return true;
  }
  if ((strcmp(command, "SD") == 0) ||
      (strcmp(command, "SD STATUS") == 0))
  {
    SdStatus();
    return true;
  }
  if (strcmp(command, "SD REINIT") == 0)
  {
    SdStatus();
    return true;
  }
  if (strcmp(command, "SD CLEAN") == 0)
  {
    SdClean();
    return true;
  }
  if (strncmp(command, "SD STRESS", 9U) == 0)
  {
    cursor = &command[9];
    if (*cursor != '\0')
    {
      if (((*cursor != ' ') && (*cursor != '\t')) ||
          !SdParseUint(&cursor, &size_mib))
      {
        SdSend("ERR usage: SD STRESS [size-MiB] [passes]\r\n");
        return true;
      }
      while ((*cursor == ' ') || (*cursor == '\t'))
      {
        ++cursor;
      }
      if ((*cursor != '\0') && !SdParseUint(&cursor, &passes))
      {
        SdSend("ERR usage: SD STRESS [size-MiB] [passes]\r\n");
        return true;
      }
      while ((*cursor == ' ') || (*cursor == '\t'))
      {
        ++cursor;
      }
      if (*cursor != '\0')
      {
        SdSend("ERR usage: SD STRESS [size-MiB] [passes]\r\n");
        return true;
      }
    }
    if ((size_mib == 0U) || (size_mib > SD_TEST_MAX_MIB) ||
        (passes == 0U) || (passes > SD_TEST_MAX_PASSES) ||
        (((uint64_t)size_mib * passes) > SD_TEST_MAX_TOTAL_MIB))
    {
      SdSend("ERR SD STRESS limits: size=1..1024MiB passes=1..100 "
             "size*passes<=8192MiB\r\n");
      return true;
    }
    SdStressStart(size_mib, passes);
    return true;
  }

  SdSend("ERR usage: SD STATUS|REINIT|STRESS [size-MiB] [passes]|STRESS STOP|CLEAN\r\n");
  return true;
}

bool SdTest_IsActive(void)
{
  return sd_stress_state != SD_STRESS_IDLE;
}

void SdTest_Service(void)
{
  bool present = StorageService_CardPresent();
  FRESULT result;
  uint32_t started_ms;
  uint32_t elapsed_ms;
  UINT request;
  UINT transferred = 0U;

  if (!present && sd_card_was_present)
  {
    if (sd_stress_state != SD_STRESS_IDLE)
    {
      SdStressFailed(FR_NOT_READY, sd_stress_offset);
    }
    printf("[sd] card removed; filesystem unmounted\r\n");
  }
  sd_card_was_present = present;
  if ((sd_stress_state == SD_STRESS_IDLE) || !present)
  {
    return;
  }

  request = (sd_stress_remaining > sizeof(sd_io_buffer))
    ? (UINT)sizeof(sd_io_buffer) : (UINT)sd_stress_remaining;
  if (sd_stress_state == SD_STRESS_WRITE)
  {
    SdFillPattern(sd_stress_pass, sd_stress_chunk++);
    started_ms = HAL_GetTick();
    result = f_write(&sd_stress_file, sd_io_buffer, request, &transferred);
    elapsed_ms = HAL_GetTick() - started_ms;
    if (elapsed_ms > sd_stress_write_max_ms)
    {
      sd_stress_write_max_ms = elapsed_ms;
    }
    if ((result == FR_OK) && (transferred != request))
    {
      result = FR_DISK_ERR;
    }
    if (result != FR_OK)
    {
      SdStressFailed(result, sd_stress_offset + transferred);
      return;
    }
    sd_stress_remaining -= transferred;
    sd_stress_offset += transferred;
    sd_stress_written += transferred;
    if (sd_stress_remaining == 0U)
    {
      result = f_sync(&sd_stress_file);
      if (result == FR_OK)
      {
        result = f_lseek(&sd_stress_file, 0U);
      }
      if (result != FR_OK)
      {
        SdStressFailed(result, sd_stress_offset);
        return;
      }
      sd_stress_state = SD_STRESS_VERIFY;
      sd_stress_remaining = sd_stress_size_mib * 1024U * 1024U;
      sd_stress_offset = 0U;
      sd_stress_chunk = 0U;
      SdStressPhase("VERIFY");
    }
    return;
  }

  SdFillPattern(sd_stress_pass, sd_stress_chunk++);
  memcpy(sd_expected_buffer, sd_io_buffer, request);
  started_ms = HAL_GetTick();
  result = f_read(&sd_stress_file, sd_io_buffer, request, &transferred);
  elapsed_ms = HAL_GetTick() - started_ms;
  if (elapsed_ms > sd_stress_read_max_ms)
  {
    sd_stress_read_max_ms = elapsed_ms;
  }
  if ((result == FR_OK) && (transferred != request))
  {
    result = FR_DISK_ERR;
  }
  if ((result == FR_OK) &&
      (memcmp(sd_io_buffer, sd_expected_buffer, request) != 0))
  {
    for (UINT index = 0U; index < request; ++index)
    {
      if (sd_io_buffer[index] != sd_expected_buffer[index])
      {
        SdStressFailed(FR_INT_ERR, sd_stress_offset + index);
        return;
      }
    }
  }
  if (result != FR_OK)
  {
    SdStressFailed(result, sd_stress_offset + transferred);
    return;
  }
  sd_stress_remaining -= transferred;
  sd_stress_offset += transferred;
  sd_stress_verified += transferred;
  if (sd_stress_remaining != 0U)
  {
    return;
  }

  ++sd_stress_pass;
  if (sd_stress_pass >= sd_stress_passes)
  {
    SdStressPassed();
    return;
  }
  result = f_lseek(&sd_stress_file, 0U);
  if (result != FR_OK)
  {
    SdStressFailed(result, sd_stress_offset);
    return;
  }
  sd_stress_state = SD_STRESS_WRITE;
  sd_stress_remaining = sd_stress_size_mib * 1024U * 1024U;
  sd_stress_offset = 0U;
  sd_stress_chunk = 0U;
  SdStressPhase("WRITE");
}

void SdTest_Stop(void)
{
  if (sd_stress_state != SD_STRESS_IDLE)
  {
    SdStressAbort(false);
  }
  if (StorageService_Owner() == STORAGE_OWNER_SD_STRESS)
  {
    (void)StorageService_Release(STORAGE_OWNER_SD_STRESS);
  }
}
