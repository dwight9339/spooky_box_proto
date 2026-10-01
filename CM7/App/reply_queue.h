#ifndef SPOOKY_REPLY_QUEUE_H
#define SPOOKY_REPLY_QUEUE_H

/* Bounded queue of text replies awaiting USB CDC transmission (jjy.10).
 *
 * Outcome and command replies are kept; periodic progress lines are not. Only
 * the newest progress line stays queued, and a reply that finds the queue full
 * evicts a queued progress line before it evicts the oldest reply. A new reply
 * is therefore never the line that is lost. */

#include <stdbool.h>
#include <stdint.h>

#define REPLY_QUEUE_DEPTH 4U
#define REPLY_QUEUE_LINE_BYTES 240U

typedef enum ReplyKind
{
  REPLY_KIND_KEEP = 0, /* Outcome or command reply; lost only to a newer reply. */
  REPLY_KIND_PROGRESS  /* Superseded by the next progress line. */
} ReplyKind;

typedef enum ReplyPushResult
{
  REPLY_PUSH_QUEUED = 0,
  REPLY_PUSH_COALESCED,        /* Replaced a queued, older progress line. */
  REPLY_PUSH_EVICTED_PROGRESS, /* Queue full; a queued progress line was removed. */
  REPLY_PUSH_EVICTED_REPLY,    /* Queue full of replies; the oldest one was removed. */
  REPLY_PUSH_DROPPED           /* Rejected: progress into a queue full of replies,
                                  or a NULL argument. */
} ReplyPushResult;

typedef struct ReplyQueue
{
  char text[REPLY_QUEUE_DEPTH][REPLY_QUEUE_LINE_BYTES];
  uint8_t kind[REPLY_QUEUE_DEPTH];
  uint8_t head;
  uint8_t count;
  uint32_t progress_superseded; /* Coalesced, evicted or dropped progress lines. */
  uint32_t replies_lost;        /* Evicted keep lines; each is a fault. */
  uint16_t last_lost_length;
} ReplyQueue;

void ReplyQueue_Init(ReplyQueue *queue);
/* Copies text, truncated to REPLY_QUEUE_LINE_BYTES - 1 characters. */
ReplyPushResult ReplyQueue_Push(ReplyQueue *queue, ReplyKind kind, const char *text);
/* Oldest queued line, or NULL when empty. */
const char *ReplyQueue_Peek(const ReplyQueue *queue);
void ReplyQueue_Pop(ReplyQueue *queue);

#endif /* SPOOKY_REPLY_QUEUE_H */
