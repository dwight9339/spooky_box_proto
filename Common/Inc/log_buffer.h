#ifndef SPOOKY_LOG_BUFFER_H
#define SPOOKY_LOG_BUFFER_H
#include <stdbool.h>
#include <stdint.h>

#define LOG_BUFFER_CAPACITY 4096U
#define LOG_WRITE_MAX 512U

/* Caller serializes producers/consumer. Peeked storage remains owned by the
 * buffer until Consume, including while an asynchronous transport uses it. */
typedef struct
{
  uint8_t bytes[LOG_BUFFER_CAPACITY];
  uint32_t head;
  uint32_t count;
  uint32_t high_water;
  uint32_t dropped_writes;
  uint32_t dropped_bytes;
} LogBuffer;

bool LogBuffer_Write(LogBuffer *buffer, const void *data, uint32_t length);
const uint8_t *LogBuffer_Peek(const LogBuffer *buffer, uint32_t limit,
                             uint32_t *length);
void LogBuffer_Consume(LogBuffer *buffer, uint32_t length);
void LogBuffer_Reject(LogBuffer *buffer, uint32_t length);
#endif
