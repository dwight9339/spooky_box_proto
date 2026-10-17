#ifndef SPOOKY_SESSION_FILES_H
#define SPOOKY_SESSION_FILES_H

/* Field session folder names, the folder index and the manifest text
 * (decision 0030; byte layouts in docs/design/session-format.md). Portable:
 * no FatFs or HAL, so the host tests cover it. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SESSION_FILES_ROOT            "FIELD"
#define SESSION_FILES_INDEX           "FIELD/INDEX.TXT"
#define SESSION_FILES_MANIFEST        "MANIFEST.TXT"
#define SESSION_FILES_FIRST_AUDIO     "AUDIO000.WAV"
#define SESSION_FILES_NUMBER_MIN      1U
#define SESSION_FILES_NUMBER_MAX      9999U
/* "FIELD/F9999/MANIFEST.TXT" and a terminator. */
#define SESSION_FILES_PATH_BYTES      32U
#define SESSION_FILES_INDEX_BYTES     32U
#define SESSION_FILES_MANIFEST_BYTES  1024U
#define SESSION_FILES_BUILD_BYTES     129U
#define SESSION_FILES_REASON_BYTES    33U
#define SESSION_FILES_BANDS           4U

typedef enum
{
  SESSION_STATE_RECORDING = 0,
  SESSION_STATE_ENDED
} SessionState;

typedef struct
{
  SessionState state;
  char end_reason[SESSION_FILES_REASON_BYTES]; /* empty: "none" */
  char build[SESSION_FILES_BUILD_BYTES];
  uint32_t boot;
  uint32_t start_uptime_ms;
  uint64_t frames;
  uint32_t audio_parts;
  uint32_t part_frames;
  bool events_present;           /* false until the event log exists (hpq.11) */
  uint32_t events_written;
  uint32_t events_lost;
  uint32_t retune_settle_frames[SESSION_FILES_BANDS]; /* FM, AM, SW, LW */
  bool origin_known;
  uint32_t origin_epoch;
  uint64_t origin_frame;
  uint32_t mic_compensation_frames;
} SessionManifest;

/* "FIELD/Fnnnn/<file>"; false if the number or the result does not fit. */
bool SessionFiles_Path(uint32_t number, const char *file, char *out, size_t size);
/* "FIELD/Fnnnn"; false if the number or the result does not fit. */
bool SessionFiles_Folder(uint32_t number, char *out, size_t size);
/* INDEX.TXT holds "next=<n>\r\n". False on anything else or an out-of-range n. */
bool SessionFiles_ParseIndex(const char *text, size_t length, uint32_t *next);
/* Returns the length written, or 0 if it does not fit. */
size_t SessionFiles_FormatIndex(uint32_t next, char *out, size_t size);
/* A path WAV FETCH may read: a root REC###.WAV, or FIELD/Fnnnn/ followed by
 * AUDIOnnn.WAV, MANIFEST.TXT or EVENTS.BIN. */
bool SessionFiles_FetchPathValid(const char *path);
/* True for a fetchable path that names a WAV file. */
bool SessionFiles_PathIsWav(const char *path);
/* Writes MANIFEST.TXT's text; returns its length, or 0 if it would not fit in
 * size or in SESSION_FILES_MANIFEST_BYTES. */
size_t SessionManifest_Format(const SessionManifest *manifest, char *out, size_t size);

#endif /* SPOOKY_SESSION_FILES_H */
