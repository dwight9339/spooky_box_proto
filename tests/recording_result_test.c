/*
 * Host tests for the RECORD RESULT outcome record (jjy.12): none before the
 * first recording, PASS versus ABORT, sequence, truncation and format bounds.
 * Host results only; not hardware evidence.
 */

#include <stdio.h>
#include <string.h>

#include "recording_result.h"

static int failures;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            printf("FAIL line %d: %s\n", __LINE__, #cond);                       \
            ++failures;                                                          \
        }                                                                        \
    } while (0)

static void TestNoneBeforeFirstRecording(void)
{
  RecordingResult result;
  char line[240];

  RecordingResult_Init(&result);
  CHECK(RecordingResult_Format(&result, line, sizeof(line)) == strlen(line));
  CHECK(strcmp(line, "OK RECORD RESULT NONE\r\n") == 0);
}

static void TestPassAndAbort(void)
{
  RecordingResult result;
  char line[240];

  RecordingResult_Init(&result);
  RecordingResult_Record(&result, false, true, "REC070.WAV", 2883584U, 17301504U,
                         60120U, "duration complete");
  CHECK(RecordingResult_Format(&result, line, sizeof(line)) != 0U);
  CHECK(strcmp(line, "OK RECORD RESULT seq=1 outcome=PASS file=REC070.WAV "
                     "frames=2883584 bytes=17301504 elapsed=60120ms finalized=1 "
                     "reason=duration complete\r\n") == 0);

  /* An abort replaces the previous outcome even when the file was finalized. */
  RecordingResult_Record(&result, true, true, "REC071.WAV", 1744896U, 10469376U,
                         36400U, "card full");
  CHECK(RecordingResult_Format(&result, line, sizeof(line)) != 0U);
  CHECK(strcmp(line, "OK RECORD RESULT seq=2 outcome=ABORT file=REC071.WAV "
                     "frames=1744896 bytes=10469376 elapsed=36400ms finalized=1 "
                     "reason=card full\r\n") == 0);

  /* A clean stop whose finalization failed is an ABORT, as the pushed line is. */
  RecordingResult_Record(&result, false, false, "REC072.WAV", 0U, 0U, 5U, "stopped");
  CHECK(!result.passed);
  CHECK(RecordingResult_Format(&result, line, sizeof(line)) != 0U);
  CHECK(strstr(line, "seq=3 outcome=ABORT ") != NULL);
  CHECK(strstr(line, " finalized=0 reason=stopped\r\n") != NULL);
}

static void TestTruncationAndBounds(void)
{
  RecordingResult result;
  char reason[RECORDING_RESULT_REASON_BYTES + 20U];
  char line[240];
  char small[16];

  (void)memset(reason, 'r', sizeof(reason) - 1U);
  reason[sizeof(reason) - 1U] = '\0';
  RecordingResult_Init(&result);
  RecordingResult_Record(&result, false, true, "REC000.WAV-TOO-LONG", 4294967295U,
                         4294967295U, 4294967295U, reason);
  CHECK(strlen(result.file) == RECORDING_RESULT_FILE_BYTES - 1U);
  CHECK(strlen(result.reason) == RECORDING_RESULT_REASON_BYTES - 1U);
  /* The longest reply still fits the recorder's 240-byte reply line. */
  CHECK(RecordingResult_Format(&result, line, sizeof(line)) != 0U);
  CHECK(strlen(line) < sizeof(line));
  CHECK(RecordingResult_Format(&result, small, sizeof(small)) == 0U);
  CHECK(small[0] == '\0');

  RecordingResult_Record(&result, false, true, NULL, 0U, 0U, 0U, NULL);
  CHECK(RecordingResult_Format(&result, line, sizeof(line)) != 0U);
  CHECK(strstr(line, " file= ") != NULL);
  CHECK(RecordingResult_Format(NULL, line, sizeof(line)) == 0U);
  CHECK(RecordingResult_Format(&result, NULL, sizeof(line)) == 0U);
  RecordingResult_Record(NULL, false, true, "x", 0U, 0U, 0U, "x");
}

static void TestSequenceSkipsZeroOnWrap(void)
{
  RecordingResult result;

  RecordingResult_Init(&result);
  result.sequence = 0xFFFFFFFFU;
  RecordingResult_Record(&result, false, true, "REC001.WAV", 1U, 6U, 1U, "stopped");
  CHECK(result.sequence == 1U);
}

int main(void)
{
  TestNoneBeforeFirstRecording();
  TestPassAndAbort();
  TestTruncationAndBounds();
  TestSequenceSkipsZeroOnWrap();
  if (failures != 0)
  {
    printf("recording_result_test: %d failure(s)\n", failures);
    return 1;
  }
  puts("recording_result_test: all tests passed");
  return 0;
}
