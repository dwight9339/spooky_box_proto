/*
 * Host tests for the recorder USB reply queue policy (jjy.10): outcome replies
 * survive a closed CDC port, progress lines coalesce, and order is kept.
 * Host results only; not hardware evidence.
 */

#include <stdio.h>
#include <string.h>

#include "reply_queue.h"

static int failures;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            printf("FAIL line %d: %s\n", __LINE__, #cond);                       \
            ++failures;                                                          \
        }                                                                        \
    } while (0)

static void ExpectLines(ReplyQueue *queue, const char *const *lines, unsigned int count)
{
  for (unsigned int i = 0U; i < count; ++i)
  {
    const char *line = ReplyQueue_Peek(queue);
    CHECK((line != NULL) && (strcmp(line, lines[i]) == 0));
    ReplyQueue_Pop(queue);
  }
  CHECK(ReplyQueue_Peek(queue) == NULL);
  CHECK(queue->count == 0U);
}

/* jjy.6 backpressure case: CDC closed for a whole recording. */
static void TestOutcomeSurvivesClosedPort(void)
{
  static const char *const expected[] = {
    "OK RECORD START\r\n", "RECORD progress=55.0s\r\n",
    "RECORD DIAG\r\n", "OK RECORD PASS\r\n"
  };
  ReplyQueue queue;
  char line[32];

  ReplyQueue_Init(&queue);
  CHECK(ReplyQueue_Push(&queue, REPLY_KIND_KEEP, "OK RECORD START\r\n") ==
         REPLY_PUSH_QUEUED);
  CHECK(ReplyQueue_Push(&queue, REPLY_KIND_PROGRESS, "RECORD progress=5.0s\r\n") ==
         REPLY_PUSH_QUEUED);
  for (unsigned int second = 10U; second <= 55U; second += 5U)
  {
    (void)strcpy(line, "RECORD progress=");
    line[16] = (char)('0' + (second / 10U));
    line[17] = (char)('0' + (second % 10U));
    (void)strcpy(&line[18], ".0s\r\n");
    CHECK(ReplyQueue_Push(&queue, REPLY_KIND_PROGRESS, line) == REPLY_PUSH_COALESCED);
  }
  CHECK(queue.count == 2U);
  CHECK(ReplyQueue_Push(&queue, REPLY_KIND_KEEP, "RECORD DIAG\r\n") == REPLY_PUSH_QUEUED);
  CHECK(ReplyQueue_Push(&queue, REPLY_KIND_KEEP, "OK RECORD PASS\r\n") ==
         REPLY_PUSH_QUEUED);
  CHECK(queue.progress_superseded == 10U);
  CHECK(queue.replies_lost == 0U);
  ExpectLines(&queue, expected, 4U);
}

static void TestReplyEvictsProgressWhenFull(void)
{
  static const char *const expected[] = { "A\r\n", "B\r\n", "C\r\n", "D\r\n" };
  ReplyQueue queue;

  ReplyQueue_Init(&queue);
  CHECK(ReplyQueue_Push(&queue, REPLY_KIND_KEEP, "A\r\n") == REPLY_PUSH_QUEUED);
  CHECK(ReplyQueue_Push(&queue, REPLY_KIND_PROGRESS, "P\r\n") == REPLY_PUSH_QUEUED);
  CHECK(ReplyQueue_Push(&queue, REPLY_KIND_KEEP, "B\r\n") == REPLY_PUSH_QUEUED);
  CHECK(ReplyQueue_Push(&queue, REPLY_KIND_KEEP, "C\r\n") == REPLY_PUSH_QUEUED);
  CHECK(ReplyQueue_Push(&queue, REPLY_KIND_KEEP, "D\r\n") ==
         REPLY_PUSH_EVICTED_PROGRESS);
  CHECK(queue.progress_superseded == 1U);
  CHECK(queue.replies_lost == 0U);
  /* A queue full of replies rejects progress without losing a reply. */
  CHECK(ReplyQueue_Push(&queue, REPLY_KIND_PROGRESS, "Q\r\n") == REPLY_PUSH_DROPPED);
  CHECK(queue.progress_superseded == 2U);
  ExpectLines(&queue, expected, 4U);
}

static void TestNewestReplyWinsWhenFullOfReplies(void)
{
  static const char *const expected[] = {
    "second\r\n", "third\r\n", "fourth\r\n", "OK RECORD PASS\r\n"
  };
  ReplyQueue queue;

  ReplyQueue_Init(&queue);
  CHECK(ReplyQueue_Push(&queue, REPLY_KIND_KEEP, "first\r\n") == REPLY_PUSH_QUEUED);
  CHECK(ReplyQueue_Push(&queue, REPLY_KIND_KEEP, "second\r\n") == REPLY_PUSH_QUEUED);
  CHECK(ReplyQueue_Push(&queue, REPLY_KIND_KEEP, "third\r\n") == REPLY_PUSH_QUEUED);
  CHECK(ReplyQueue_Push(&queue, REPLY_KIND_KEEP, "fourth\r\n") == REPLY_PUSH_QUEUED);
  CHECK(ReplyQueue_Push(&queue, REPLY_KIND_KEEP, "OK RECORD PASS\r\n") ==
         REPLY_PUSH_EVICTED_REPLY);
  CHECK(queue.replies_lost == 1U);
  CHECK(queue.last_lost_length == 7U);
  ExpectLines(&queue, expected, 4U);
}

/* Order survives head wrap-around, and a coalesced progress line follows the
 * replies queued after the line it replaces. */
static void TestOrderAcrossWrap(void)
{
  static const char *const expected[] = { "B\r\n", "C\r\n", "P2\r\n" };
  ReplyQueue queue;

  ReplyQueue_Init(&queue);
  for (unsigned int i = 0U; i < 3U; ++i)
  {
    CHECK(ReplyQueue_Push(&queue, REPLY_KIND_KEEP, "x\r\n") == REPLY_PUSH_QUEUED);
    ReplyQueue_Pop(&queue);
  }
  CHECK(ReplyQueue_Push(&queue, REPLY_KIND_PROGRESS, "P1\r\n") == REPLY_PUSH_QUEUED);
  CHECK(ReplyQueue_Push(&queue, REPLY_KIND_KEEP, "B\r\n") == REPLY_PUSH_QUEUED);
  CHECK(ReplyQueue_Push(&queue, REPLY_KIND_KEEP, "C\r\n") == REPLY_PUSH_QUEUED);
  CHECK(ReplyQueue_Push(&queue, REPLY_KIND_PROGRESS, "P2\r\n") == REPLY_PUSH_COALESCED);
  ExpectLines(&queue, expected, 3U);
}

static void TestTruncationAndNullInputs(void)
{
  ReplyQueue queue;
  char line[REPLY_QUEUE_LINE_BYTES + 16U];

  ReplyQueue_Init(&queue);
  (void)memset(line, 'x', sizeof(line) - 1U);
  line[sizeof(line) - 1U] = '\0';
  CHECK(ReplyQueue_Push(&queue, REPLY_KIND_KEEP, line) == REPLY_PUSH_QUEUED);
  CHECK((ReplyQueue_Peek(&queue) != NULL) &&
        (strlen(ReplyQueue_Peek(&queue)) == REPLY_QUEUE_LINE_BYTES - 1U));
  CHECK(ReplyQueue_Push(&queue, REPLY_KIND_KEEP, NULL) == REPLY_PUSH_DROPPED);
  CHECK(ReplyQueue_Push(NULL, REPLY_KIND_KEEP, "x") == REPLY_PUSH_DROPPED);
  CHECK(ReplyQueue_Peek(NULL) == NULL);
  ReplyQueue_Pop(NULL);
  CHECK(queue.count == 1U);
}

int main(void)
{
  TestOutcomeSurvivesClosedPort();
  TestReplyEvictsProgressWhenFull();
  TestNewestReplyWinsWhenFullOfReplies();
  TestOrderAcrossWrap();
  TestTruncationAndNullInputs();
  if (failures != 0)
  {
    printf("reply_queue_test: %d failure(s)\n", failures);
    return 1;
  }
  puts("reply_queue_test: all tests passed");
  return 0;
}
