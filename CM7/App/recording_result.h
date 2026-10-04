#ifndef SPOOKY_RECORDING_RESULT_H
#define SPOOKY_RECORDING_RESULT_H

/* The outcome of the last finished recording, kept for RECORD RESULT (jjy.12).
 * A host that missed the pushed PASS/ABORT line (for example because its port
 * open discarded received data) reads it back on request. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RECORDING_RESULT_FILE_BYTES 13U
#define RECORDING_RESULT_REASON_BYTES 48U

typedef struct RecordingResult
{
  uint32_t sequence; /* Finished recordings since boot; 0 means none yet. */
  bool passed;       /* Not aborted and finalized: the pushed line was PASS. */
  bool finalized;
  char file[RECORDING_RESULT_FILE_BYTES];
  uint32_t frames;
  uint32_t bytes;
  uint32_t elapsed_ms;
  char reason[RECORDING_RESULT_REASON_BYTES];
} RecordingResult;

void RecordingResult_Init(RecordingResult *result);
/* Strings are copied and truncated to fit. */
void RecordingResult_Record(RecordingResult *result, bool aborted, bool finalized,
                            const char *file, uint32_t frames, uint32_t bytes,
                            uint32_t elapsed_ms, const char *reason);
/* Writes the CRLF-terminated RECORD RESULT reply; returns its length, or 0 if
 * it does not fit. */
size_t RecordingResult_Format(const RecordingResult *result, char *out, size_t size);

#endif /* SPOOKY_RECORDING_RESULT_H */
