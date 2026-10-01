#include "recording_result.h"

#include <stdio.h>
#include <string.h>

static void CopyText(char *destination, size_t size, const char *source)
{
  size_t length = (source != NULL) ? strlen(source) : 0U;

  if (length >= size)
  {
    length = size - 1U;
  }
  if (length != 0U)
  {
    (void)memcpy(destination, source, length);
  }
  destination[length] = '\0';
}

void RecordingResult_Init(RecordingResult *result)
{
  if (result != NULL)
  {
    (void)memset(result, 0, sizeof(*result));
  }
}

void RecordingResult_Record(RecordingResult *result, bool aborted, bool finalized,
                            const char *file, uint32_t frames, uint32_t bytes,
                            uint32_t elapsed_ms, const char *reason)
{
  if (result == NULL)
  {
    return;
  }
  ++result->sequence;
  if (result->sequence == 0U)
  {
    result->sequence = 1U; /* 0 is reserved for "none". */
  }
  result->passed = !aborted && finalized;
  result->finalized = finalized;
  CopyText(result->file, sizeof(result->file), file);
  result->frames = frames;
  result->bytes = bytes;
  result->elapsed_ms = elapsed_ms;
  CopyText(result->reason, sizeof(result->reason), reason);
}

size_t RecordingResult_Format(const RecordingResult *result, char *out, size_t size)
{
  int length;

  if ((result == NULL) || (out == NULL) || (size == 0U))
  {
    return 0U;
  }
  if (result->sequence == 0U)
  {
    length = snprintf(out, size, "OK RECORD RESULT NONE\r\n");
  }
  else
  {
    length = snprintf(out, size,
      "OK RECORD RESULT seq=%lu outcome=%s file=%s frames=%lu bytes=%lu "
      "elapsed=%lums finalized=%u reason=%s\r\n",
      (unsigned long)result->sequence, result->passed ? "PASS" : "ABORT",
      result->file, (unsigned long)result->frames, (unsigned long)result->bytes,
      (unsigned long)result->elapsed_ms, result->finalized ? 1U : 0U,
      result->reason);
  }
  if ((length <= 0) || ((size_t)length >= size))
  {
    out[0] = '\0';
    return 0U;
  }
  return (size_t)length;
}
