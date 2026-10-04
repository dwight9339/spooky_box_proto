#include "reply_queue.h"

#include <stddef.h>
#include <string.h>

static uint8_t ReplyQueueSlot(const ReplyQueue *queue, uint8_t position)
{
  return (uint8_t)((queue->head + position) % REPLY_QUEUE_DEPTH);
}

/* Removes the line at a queue position, keeping the order of the rest. */
static void ReplyQueueRemove(ReplyQueue *queue, uint8_t position)
{
  for (uint8_t i = position; (uint8_t)(i + 1U) < queue->count; ++i)
  {
    uint8_t to = ReplyQueueSlot(queue, i);
    uint8_t from = ReplyQueueSlot(queue, (uint8_t)(i + 1U));
    (void)memcpy(queue->text[to], queue->text[from], REPLY_QUEUE_LINE_BYTES);
    queue->kind[to] = queue->kind[from];
  }
  --queue->count;
}

static bool ReplyQueueFindProgress(const ReplyQueue *queue, uint8_t *position)
{
  for (uint8_t i = 0U; i < queue->count; ++i)
  {
    if (queue->kind[ReplyQueueSlot(queue, i)] == (uint8_t)REPLY_KIND_PROGRESS)
    {
      *position = i;
      return true;
    }
  }
  return false;
}

static void ReplyQueueAppend(ReplyQueue *queue, ReplyKind kind, const char *text)
{
  uint8_t tail = ReplyQueueSlot(queue, queue->count);
  size_t length = strlen(text);

  if (length >= REPLY_QUEUE_LINE_BYTES)
  {
    length = REPLY_QUEUE_LINE_BYTES - 1U;
  }
  (void)memcpy(queue->text[tail], text, length);
  queue->text[tail][length] = '\0';
  queue->kind[tail] = (uint8_t)kind;
  ++queue->count;
}

void ReplyQueue_Init(ReplyQueue *queue)
{
  if (queue != NULL)
  {
    (void)memset(queue, 0, sizeof(*queue));
  }
}

ReplyPushResult ReplyQueue_Push(ReplyQueue *queue, ReplyKind kind, const char *text)
{
  ReplyPushResult result = REPLY_PUSH_QUEUED;
  uint8_t position;

  if ((queue == NULL) || (text == NULL))
  {
    return REPLY_PUSH_DROPPED;
  }
  if (kind == REPLY_KIND_PROGRESS)
  {
    if (ReplyQueueFindProgress(queue, &position))
    {
      /* The newer line moves to the tail so it follows any reply queued since. */
      ReplyQueueRemove(queue, position);
      ++queue->progress_superseded;
      result = REPLY_PUSH_COALESCED;
    }
    else if (queue->count == REPLY_QUEUE_DEPTH)
    {
      ++queue->progress_superseded;
      return REPLY_PUSH_DROPPED;
    }
  }
  else if (queue->count == REPLY_QUEUE_DEPTH)
  {
    if (ReplyQueueFindProgress(queue, &position))
    {
      ReplyQueueRemove(queue, position);
      ++queue->progress_superseded;
      result = REPLY_PUSH_EVICTED_PROGRESS;
    }
    else
    {
      queue->last_lost_length = (uint16_t)strlen(queue->text[queue->head]);
      ReplyQueue_Pop(queue);
      ++queue->replies_lost;
      result = REPLY_PUSH_EVICTED_REPLY;
    }
  }
  ReplyQueueAppend(queue, kind, text);
  return result;
}

const char *ReplyQueue_Peek(const ReplyQueue *queue)
{
  return ((queue == NULL) || (queue->count == 0U)) ? NULL : queue->text[queue->head];
}

void ReplyQueue_Pop(ReplyQueue *queue)
{
  if ((queue == NULL) || (queue->count == 0U))
  {
    return;
  }
  queue->head = (uint8_t)((queue->head + 1U) % REPLY_QUEUE_DEPTH);
  --queue->count;
}

uint8_t ReplyQueue_Clear(ReplyQueue *queue)
{
  uint8_t discarded;

  if (queue == NULL)
  {
    return 0U;
  }
  discarded = queue->count;
  queue->head = 0U;
  queue->count = 0U;
  return discarded;
}
