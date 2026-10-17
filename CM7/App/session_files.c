#include "session_files.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static bool IsDigits(const char *text, size_t count)
{
  for (size_t index = 0U; index < count; ++index)
  {
    if ((text[index] < '0') || (text[index] > '9'))
    {
      return false;
    }
  }
  return true;
}

bool SessionFiles_Folder(uint32_t number, char *out, size_t size)
{
  int length;

  if ((out == NULL) || (number < SESSION_FILES_NUMBER_MIN) ||
      (number > SESSION_FILES_NUMBER_MAX))
  {
    return false;
  }
  length = snprintf(out, size, SESSION_FILES_ROOT "/F%04lu", (unsigned long)number);
  return (length > 0) && ((size_t)length < size);
}

bool SessionFiles_Path(uint32_t number, const char *file, char *out, size_t size)
{
  int length;

  if ((out == NULL) || (file == NULL) || (number < SESSION_FILES_NUMBER_MIN) ||
      (number > SESSION_FILES_NUMBER_MAX))
  {
    return false;
  }
  length = snprintf(out, size, SESSION_FILES_ROOT "/F%04lu/%s",
                    (unsigned long)number, file);
  return (length > 0) && ((size_t)length < size);
}

bool SessionFiles_ParseIndex(const char *text, size_t length, uint32_t *next)
{
  uint32_t value = 0U;
  size_t index = 5U;
  bool digit = false;

  if ((text == NULL) || (next == NULL) || (length < 6U) ||
      (strncmp(text, "next=", 5U) != 0))
  {
    return false;
  }
  while ((index < length) && (text[index] >= '0') && (text[index] <= '9'))
  {
    value = (value * 10U) + (uint32_t)(text[index] - '0');
    digit = true;
    ++index;
    if (value > SESSION_FILES_NUMBER_MAX)
    {
      return false;
    }
  }
  if (!digit || (value < SESSION_FILES_NUMBER_MIN))
  {
    return false;
  }
  /* Only a line ending may follow. */
  while (index < length)
  {
    if ((text[index] != '\r') && (text[index] != '\n'))
    {
      return false;
    }
    ++index;
  }
  *next = value;
  return true;
}

size_t SessionFiles_FormatIndex(uint32_t next, char *out, size_t size)
{
  int length;

  if ((out == NULL) || (next < SESSION_FILES_NUMBER_MIN) ||
      (next > SESSION_FILES_NUMBER_MAX))
  {
    return 0U;
  }
  length = snprintf(out, size, "next=%lu\r\n", (unsigned long)next);
  return ((length > 0) && ((size_t)length < size)) ? (size_t)length : 0U;
}

static bool IsLegacyWav(const char *path)
{
  return (strlen(path) == 10U) && (strncmp(path, "REC", 3U) == 0) &&
         IsDigits(&path[3], 3U) && (strcmp(&path[6], ".WAV") == 0);
}

/* The file name after "FIELD/Fnnnn/", or NULL. */
static const char *SessionFile(const char *path)
{
  static const char prefix[] = SESSION_FILES_ROOT "/F";
  const size_t prefix_length = sizeof(prefix) - 1U;

  if ((strlen(path) <= (prefix_length + 5U)) ||
      (strncmp(path, prefix, prefix_length) != 0) ||
      !IsDigits(&path[prefix_length], 4U) || (path[prefix_length + 4U] != '/'))
  {
    return NULL;
  }
  return &path[prefix_length + 5U];
}

static bool IsAudioPart(const char *file)
{
  return (strlen(file) == 12U) && (strncmp(file, "AUDIO", 5U) == 0) &&
         IsDigits(&file[5], 3U) && (strcmp(&file[8], ".WAV") == 0);
}

bool SessionFiles_FetchPathValid(const char *path)
{
  const char *file;

  if (path == NULL)
  {
    return false;
  }
  if (IsLegacyWav(path))
  {
    return true;
  }
  file = SessionFile(path);
  return (file != NULL) &&
         (IsAudioPart(file) || (strcmp(file, SESSION_FILES_MANIFEST) == 0) ||
          (strcmp(file, "EVENTS.BIN") == 0));
}

bool SessionFiles_PathIsWav(const char *path)
{
  const char *file;

  if (path == NULL)
  {
    return false;
  }
  if (IsLegacyWav(path))
  {
    return true;
  }
  file = SessionFile(path);
  return (file != NULL) && IsAudioPart(file);
}

/* Decimal text of a 64-bit count; newlib-nano printf has no %llu. */
static void FormatU64(uint64_t value, char text[21])
{
  char digits[21];
  uint32_t count = 0U;

  do
  {
    digits[count++] = (char)('0' + (uint32_t)(value % 10U));
    value /= 10U;
  } while ((value != 0U) && (count < 20U));
  for (uint32_t index = 0U; index < count; ++index)
  {
    text[index] = digits[count - 1U - index];
  }
  text[count] = '\0';
}

typedef struct
{
  char *out;
  size_t size;
  size_t used;
  bool overflow;
} Writer;

static void Put(Writer *writer, const char *format, ...)
{
  va_list args;
  int length;

  if (writer->overflow)
  {
    return;
  }
  va_start(args, format);
  length = vsnprintf(&writer->out[writer->used], writer->size - writer->used,
                     format, args);
  va_end(args);
  if ((length < 0) || ((size_t)length >= (writer->size - writer->used)))
  {
    writer->overflow = true;
    return;
  }
  writer->used += (size_t)length;
}

size_t SessionManifest_Format(const SessionManifest *manifest, char *out, size_t size)
{
  Writer writer;
  char frames[21];
  char origin[21];
  const size_t limit = (size < (SESSION_FILES_MANIFEST_BYTES + 1U)) ?
                       size : (SESSION_FILES_MANIFEST_BYTES + 1U);

  if ((manifest == NULL) || (out == NULL) || (size == 0U))
  {
    return 0U;
  }
  writer.out = out;
  writer.size = limit;
  writer.used = 0U;
  writer.overflow = false;
  out[0] = '\0';
  FormatU64(manifest->frames, frames);
  FormatU64(manifest->origin_frame, origin);

  Put(&writer, "format=spooky-session\r\nformat_version=1\r\ntype=field\r\n");
  Put(&writer, "state=%s\r\n",
      (manifest->state == SESSION_STATE_ENDED) ? "ended" : "recording");
  Put(&writer, "end_reason=%s\r\n",
      (manifest->end_reason[0] != '\0') ? manifest->end_reason : "none");
  Put(&writer, "build=%s\r\n", manifest->build);
  Put(&writer, "boot=%lu\r\nstart_uptime_ms=%lu\r\nstart_time=unknown\r\n",
      (unsigned long)manifest->boot, (unsigned long)manifest->start_uptime_ms);
  Put(&writer, "sample_rate_hz=48000\r\nchannels=radio-L,radio-R,mic\r\n");
  Put(&writer, "frames=%s\r\naudio_parts=%lu\r\npart_frames=%lu\r\n", frames,
      (unsigned long)manifest->audio_parts, (unsigned long)manifest->part_frames);
  if (manifest->events_present)
  {
    Put(&writer, "events=EVENTS.BIN\r\nevent_format_version=1\r\n"
        "events_written=%lu\r\nevents_lost=%lu\r\n",
        (unsigned long)manifest->events_written, (unsigned long)manifest->events_lost);
  }
  else
  {
    Put(&writer, "events=none\r\n");
  }
  Put(&writer, "retune_settle_frames=FM:%lu,AM:%lu,SW:%lu,LW:%lu\r\n",
      (unsigned long)manifest->retune_settle_frames[0],
      (unsigned long)manifest->retune_settle_frames[1],
      (unsigned long)manifest->retune_settle_frames[2],
      (unsigned long)manifest->retune_settle_frames[3]);
  if (manifest->origin_known)
  {
    Put(&writer, "origin=%lu:%s\r\n", (unsigned long)manifest->origin_epoch, origin);
  }
  else
  {
    Put(&writer, "origin=unknown\r\n");
  }
  Put(&writer, "mic_compensation_frames=%lu\r\n",
      (unsigned long)manifest->mic_compensation_frames);
  if (writer.overflow)
  {
    out[0] = '\0';
    return 0U;
  }
  return writer.used;
}
