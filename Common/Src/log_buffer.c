#include "log_buffer.h"
#include <string.h>

void LogBuffer_Reject(LogBuffer *buffer, uint32_t length)
{
  ++buffer->dropped_writes;
  buffer->dropped_bytes += length;
}

bool LogBuffer_Write(LogBuffer *buffer, const void *data, uint32_t length)
{
  uint32_t tail;
  uint32_t first;
  if (length == 0U) return true;
  if ((data == NULL) || (length > LOG_WRITE_MAX) ||
      (length > LOG_BUFFER_CAPACITY - buffer->count))
  {
    LogBuffer_Reject(buffer, length);
    return false;
  }
  tail = (buffer->head + buffer->count) % LOG_BUFFER_CAPACITY;
  first = LOG_BUFFER_CAPACITY - tail;
  if (first > length) first = length;
  memcpy(&buffer->bytes[tail], data, first);
  memcpy(buffer->bytes, (const uint8_t *)data + first, length - first);
  buffer->count += length;
  if (buffer->count > buffer->high_water) buffer->high_water = buffer->count;
  return true;
}

const uint8_t *LogBuffer_Peek(const LogBuffer *buffer, uint32_t limit,
                             uint32_t *length)
{
  uint32_t available = LOG_BUFFER_CAPACITY - buffer->head;
  if (available > buffer->count) available = buffer->count;
  if (available > limit) available = limit;
  *length = available;
  return &buffer->bytes[buffer->head];
}

void LogBuffer_Consume(LogBuffer *buffer, uint32_t length)
{
  if (length > buffer->count) length = buffer->count;
  buffer->head = (buffer->head + length) % LOG_BUFFER_CAPACITY;
  buffer->count -= length;
}
