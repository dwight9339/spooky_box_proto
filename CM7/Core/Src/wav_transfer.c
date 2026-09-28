#include "wav_transfer.h"

#include "ff.h"
#include "storage_service.h"
#include "usb_test.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define WAV_TRANSFER_PROTOCOL             1U
#define WAV_TRANSFER_FRAME_BYTES       1024U
#define WAV_TRANSFER_HEADER_BYTES        16U
#define WAV_TRANSFER_PAYLOAD_BYTES \
  (WAV_TRANSFER_FRAME_BYTES - WAV_TRANSFER_HEADER_BYTES)
#define WAV_TRANSFER_MAX_FILE_BYTES (256U * 1024U * 1024U)
#define WAV_TRANSFER_ACK_TIMEOUT_MS    10000U
#define WAV_FRAME_DATA                      1U
#define WAV_FRAME_END                       2U
#define WAV_FRAME_ERROR                     4U

static FIL wav_file;
static bool wav_file_open;
static bool wav_active;
static bool wav_frame_ready;
static bool wav_waiting_ack;
static bool wav_close_after_send;
static uint32_t wav_size;
static uint32_t wav_offset;
static uint32_t wav_crc_state;
static uint32_t wav_ack_deadline;
static uint16_t wav_frame_length;
static char wav_filename[13];
static uint8_t wav_frame[WAV_TRANSFER_FRAME_BYTES];

static void WavPutLe16(uint8_t *destination, uint16_t value)
{
  destination[0] = (uint8_t)value;
  destination[1] = (uint8_t)(value >> 8U);
}

static void WavPutLe32(uint8_t *destination, uint32_t value)
{
  destination[0] = (uint8_t)value;
  destination[1] = (uint8_t)(value >> 8U);
  destination[2] = (uint8_t)(value >> 16U);
  destination[3] = (uint8_t)(value >> 24U);
}

static uint32_t WavCrcUpdate(uint32_t crc, const uint8_t *data,
                             uint32_t length)
{
  for (uint32_t index = 0U; index < length; ++index)
  {
    crc ^= data[index];
    for (unsigned int bit = 0U; bit < 8U; ++bit)
    {
      crc = (crc >> 1U) ^ ((crc & 1U) ? 0xEDB88320U : 0U);
    }
  }
  return crc;
}

static void WavClose(void)
{
  if (wav_file_open)
  {
    (void)f_close(&wav_file);
    wav_file_open = false;
  }
  if (StorageService_Owner() == STORAGE_OWNER_WAV_TRANSFER)
  {
    (void)StorageService_Release(STORAGE_OWNER_WAV_TRANSFER);
  }
  wav_active = false;
  wav_frame_ready = false;
  wav_waiting_ack = false;
  wav_close_after_send = false;
}

static bool WavFilenameValid(const char *name)
{
  return (name != NULL) && (strlen(name) == 10U) &&
    (name[0] == 'R') && (name[1] == 'E') && (name[2] == 'C') &&
    (name[3] >= '0') && (name[3] <= '9') &&
    (name[4] >= '0') && (name[4] <= '9') &&
    (name[5] >= '0') && (name[5] <= '9') &&
    (strcmp(&name[6], ".WAV") == 0);
}

static bool WavParseUint(const char *text, uint32_t *value)
{
  uint32_t result = 0U;
  bool digit = false;

  if ((text == NULL) || (value == NULL)) return false;
  while ((*text == ' ') || (*text == '\t')) ++text;
  while ((*text >= '0') && (*text <= '9'))
  {
    uint32_t next = (uint32_t)(*text - '0');
    if (result > ((UINT32_MAX - next) / 10U)) return false;
    result = (result * 10U) + next;
    digit = true;
    ++text;
  }
  while ((*text == ' ') || (*text == '\t')) ++text;
  if (!digit || (*text != '\0')) return false;
  *value = result;
  return true;
}

static void WavPrepareFrame(uint16_t flags, uint32_t offset,
                            const uint8_t *payload, uint16_t length,
                            uint32_t crc)
{
  memcpy(&wav_frame[0], "WV01", 4U);
  WavPutLe32(&wav_frame[4], offset);
  WavPutLe16(&wav_frame[8], length);
  WavPutLe16(&wav_frame[10], flags);
  WavPutLe32(&wav_frame[12], crc);
  if ((payload != NULL) && (length != 0U) &&
      (payload != &wav_frame[WAV_TRANSFER_HEADER_BYTES]))
  {
    memcpy(&wav_frame[WAV_TRANSFER_HEADER_BYTES], payload, length);
  }
  wav_frame_length = (uint16_t)(WAV_TRANSFER_HEADER_BYTES + length);
  wav_frame_ready = true;
}

static void WavPrepareError(const char *message)
{
  size_t length = strlen(message);
  if (length > WAV_TRANSFER_PAYLOAD_BYTES)
  {
    length = WAV_TRANSFER_PAYLOAD_BYTES;
  }
  WavPrepareFrame(WAV_FRAME_ERROR, wav_offset,
                  (const uint8_t *)message, (uint16_t)length,
                  WavCrcUpdate(0xFFFFFFFFU, (const uint8_t *)message,
                               (uint32_t)length) ^ 0xFFFFFFFFU);
  wav_close_after_send = true;
}

static bool WavStart(const char *name)
{
  FRESULT result;
  char response[128];

  if (!StorageService_CardPresent())
  {
    (void)UsbTest_SendText("ERR WAV no SD card detected\r\n");
    return false;
  }
  result = StorageService_Acquire(STORAGE_OWNER_WAV_TRANSFER);
  if (result != FR_OK)
  {
    (void)snprintf(response, sizeof(response),
                   "ERR WAV mount failed result=%u\r\n", (unsigned int)result);
    (void)UsbTest_SendText(response);
    WavClose();
    return false;
  }
  result = f_open(&wav_file, name, FA_READ);
  if (result != FR_OK)
  {
    (void)snprintf(response, sizeof(response),
                   "ERR WAV open failed result=%u\r\n", (unsigned int)result);
    (void)UsbTest_SendText(response);
    WavClose();
    return false;
  }
  wav_file_open = true;
  wav_size = f_size(&wav_file);
  if ((wav_size < 44U) || (wav_size > WAV_TRANSFER_MAX_FILE_BYTES))
  {
    (void)UsbTest_SendText("ERR WAV file size outside 44..268435456 bytes\r\n");
    WavClose();
    return false;
  }
  (void)snprintf(wav_filename, sizeof(wav_filename), "%s", name);
  wav_offset = 0U;
  wav_crc_state = 0xFFFFFFFFU;
  wav_frame_ready = false;
  wav_waiting_ack = false;
  wav_close_after_send = false;
  wav_active = true;
  (void)snprintf(response, sizeof(response),
                 "OK WAV START file=%s bytes=%lu chunk=%u protocol=%u\r\n",
                 wav_filename, (unsigned long)wav_size,
                 WAV_TRANSFER_PAYLOAD_BYTES, WAV_TRANSFER_PROTOCOL);
  if (!UsbTest_SendText(response))
  {
    WavClose();
    return false;
  }
  printf("[wav] transfer start file=%s bytes=%lu\r\n",
         wav_filename, (unsigned long)wav_size);
  return true;
}

void WavTransfer_Init(void)
{
  wav_file_open = false;
  wav_active = false;
  wav_frame_ready = false;
  wav_waiting_ack = false;
  wav_close_after_send = false;
}

bool WavTransfer_HandleCommand(const char *command)
{
  const char *argument;
  uint32_t acknowledged;

  if ((command == NULL) || (strncmp(command, "WAV", 3U) != 0) ||
      ((command[3] != '\0') && (command[3] != ' ') &&
       (command[3] != '\t')))
  {
    return false;
  }
  if (strcmp(command, "WAV ABORT") == 0)
  {
    bool was_active = wav_active;
    WavClose();
    (void)UsbTest_SendText(was_active ? "OK WAV ABORT\r\n" :
                                        "OK WAV already idle\r\n");
    return true;
  }
  if (strncmp(command, "WAV ACK ", 8U) == 0)
  {
    if (!wav_active || !wav_waiting_ack ||
        !WavParseUint(&command[8], &acknowledged) ||
        (acknowledged != wav_offset))
    {
      if (wav_active && !wav_frame_ready)
      {
        wav_waiting_ack = false;
        WavPrepareError("invalid acknowledgement");
      }
      return true;
    }
    wav_waiting_ack = false;
    return true;
  }
  if (strncmp(command, "WAV FETCH ", 10U) == 0)
  {
    argument = &command[10];
    if (wav_active)
    {
      (void)UsbTest_SendText("ERR WAV transfer already active\r\n");
    }
    else if (!WavFilenameValid(argument))
    {
      (void)UsbTest_SendText("ERR usage: WAV FETCH REC###.WAV\r\n");
    }
    else
    {
      (void)WavStart(argument);
    }
    return true;
  }
  if (!wav_active)
  {
    (void)UsbTest_SendText("ERR usage: WAV FETCH REC###.WAV|ABORT\r\n");
  }
  return true;
}

void WavTransfer_Service(void)
{
  FRESULT result;
  UINT read = 0U;
  uint32_t chunk_crc;

  if (!wav_active) return;
  if (wav_waiting_ack)
  {
    if ((int32_t)(HAL_GetTick() - wav_ack_deadline) >= 0)
    {
      printf("[wav] transfer aborted: acknowledgement timeout at %lu\r\n",
             (unsigned long)wav_offset);
      WavClose();
    }
    return;
  }
  if (!wav_frame_ready)
  {
    if (!StorageService_CardPresent())
    {
      WavPrepareError("SD card removed");
    }
    else if (wav_offset == wav_size)
    {
      WavPrepareFrame(WAV_FRAME_END, wav_offset, NULL, 0U,
                      wav_crc_state ^ 0xFFFFFFFFU);
      wav_close_after_send = true;
    }
    else
    {
      uint32_t remaining = wav_size - wav_offset;
      UINT request = (remaining < WAV_TRANSFER_PAYLOAD_BYTES)
        ? (UINT)remaining : (UINT)WAV_TRANSFER_PAYLOAD_BYTES;
      result = f_read(&wav_file, &wav_frame[WAV_TRANSFER_HEADER_BYTES],
                      request, &read);
      if ((result != FR_OK) || (read != request))
      {
        WavPrepareError("file read failed");
      }
      else
      {
        chunk_crc = WavCrcUpdate(0xFFFFFFFFU,
          &wav_frame[WAV_TRANSFER_HEADER_BYTES], read) ^ 0xFFFFFFFFU;
        wav_crc_state = WavCrcUpdate(wav_crc_state,
          &wav_frame[WAV_TRANSFER_HEADER_BYTES], read);
        WavPrepareFrame(WAV_FRAME_DATA, wav_offset,
          &wav_frame[WAV_TRANSFER_HEADER_BYTES], (uint16_t)read, chunk_crc);
      }
    }
  }
  if (!UsbTest_SendData(wav_frame, wav_frame_length)) return;
  wav_frame_ready = false;
  if (wav_close_after_send)
  {
    printf("[wav] transfer %s at %lu bytes\r\n",
      (wav_offset == wav_size) ? "complete" : "failed",
      (unsigned long)wav_offset);
    WavClose();
    return;
  }
  wav_offset += (uint32_t)wav_frame_length - WAV_TRANSFER_HEADER_BYTES;
  wav_waiting_ack = true;
  wav_ack_deadline = HAL_GetTick() + WAV_TRANSFER_ACK_TIMEOUT_MS;
}

bool WavTransfer_IsActive(void)
{
  return wav_active;
}

void WavTransfer_Stop(void)
{
  WavClose();
}
