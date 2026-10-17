/*
 * Host tests for Field session names, the folder index and the manifest text
 * (decision 0030, full_spooky_proto-hpq.10). Host results only; not hardware
 * evidence.
 */

#include <stdio.h>
#include <string.h>

#include "session_files.h"

static int failures;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            printf("FAIL line %d: %s\n", __LINE__, #cond);                       \
            ++failures;                                                          \
        }                                                                        \
    } while (0)

static void TestNames(void)
{
  char text[SESSION_FILES_PATH_BYTES];

  CHECK(SessionFiles_Folder(7U, text, sizeof(text)));
  CHECK(strcmp(text, "FIELD/F0007") == 0);
  CHECK(SessionFiles_Path(9999U, SESSION_FILES_MANIFEST, text, sizeof(text)));
  CHECK(strcmp(text, "FIELD/F9999/MANIFEST.TXT") == 0);
  CHECK(SessionFiles_Path(1U, SESSION_FILES_FIRST_AUDIO, text, sizeof(text)));
  CHECK(strcmp(text, "FIELD/F0001/AUDIO000.WAV") == 0);
  CHECK(!SessionFiles_Folder(0U, text, sizeof(text)));
  CHECK(!SessionFiles_Folder(10000U, text, sizeof(text)));
  CHECK(!SessionFiles_Path(1U, SESSION_FILES_MANIFEST, text, 24U)); /* needs 25 */
}

static void TestIndex(void)
{
  char text[SESSION_FILES_INDEX_BYTES];
  uint32_t next = 0U;
  size_t length = SessionFiles_FormatIndex(42U, text, sizeof(text));

  CHECK(length == strlen("next=42\r\n"));
  CHECK(strcmp(text, "next=42\r\n") == 0);
  CHECK(SessionFiles_ParseIndex(text, length, &next) && (next == 42U));
  CHECK(SessionFiles_ParseIndex("next=9999", 9U, &next) && (next == 9999U));
  CHECK(!SessionFiles_ParseIndex("next=0\r\n", 8U, &next));
  CHECK(!SessionFiles_ParseIndex("next=10000\r\n", 12U, &next));
  CHECK(!SessionFiles_ParseIndex("next=\r\n", 7U, &next));
  CHECK(!SessionFiles_ParseIndex("next=12x\r\n", 10U, &next));
  CHECK(!SessionFiles_ParseIndex("nxt=12\r\n", 8U, &next));
  CHECK(SessionFiles_FormatIndex(0U, text, sizeof(text)) == 0U);
  CHECK(SessionFiles_FormatIndex(10000U, text, sizeof(text)) == 0U);
}

static void TestFetchPaths(void)
{
  CHECK(SessionFiles_FetchPathValid("REC000.WAV"));
  CHECK(SessionFiles_FetchPathValid("FIELD/F0007/AUDIO000.WAV"));
  CHECK(SessionFiles_FetchPathValid("FIELD/F0007/AUDIO123.WAV"));
  CHECK(SessionFiles_FetchPathValid("FIELD/F0007/MANIFEST.TXT"));
  CHECK(SessionFiles_FetchPathValid("FIELD/F0007/EVENTS.BIN"));
  CHECK(!SessionFiles_FetchPathValid("FIELD/F007/AUDIO000.WAV"));
  CHECK(!SessionFiles_FetchPathValid("FIELD/F0007/AUDIO00.WAV"));
  CHECK(!SessionFiles_FetchPathValid("FIELD/F0007/../SDTEST.BIN"));
  CHECK(!SessionFiles_FetchPathValid("FIELD/F0007/"));
  CHECK(!SessionFiles_FetchPathValid("FIELD/INDEX.TXT"));
  CHECK(!SessionFiles_FetchPathValid("SDTEST.BIN"));
  CHECK(!SessionFiles_FetchPathValid("rec000.wav"));
  CHECK(!SessionFiles_FetchPathValid(NULL));
  CHECK(SessionFiles_PathIsWav("REC001.WAV"));
  CHECK(SessionFiles_PathIsWav("FIELD/F0002/AUDIO000.WAV"));
  CHECK(!SessionFiles_PathIsWav("FIELD/F0002/MANIFEST.TXT"));
}

static void Base(SessionManifest *manifest)
{
  memset(manifest, 0, sizeof(*manifest));
  strcpy(manifest->build, "54w61-retune-20261009a");
  manifest->boot = 12U;
  manifest->start_uptime_ms = 184022U;
  manifest->audio_parts = 1U;
  manifest->part_frames = 715827712U;
  manifest->retune_settle_frames[2] = 512U;
  manifest->retune_settle_frames[3] = 512U;
}

static void TestManifestRecording(void)
{
  SessionManifest manifest;
  char text[SESSION_FILES_MANIFEST_BYTES + 1U];
  static const char expected[] =
    "format=spooky-session\r\nformat_version=1\r\ntype=field\r\n"
    "state=recording\r\nend_reason=none\r\nbuild=54w61-retune-20261009a\r\n"
    "boot=12\r\nstart_uptime_ms=184022\r\nstart_time=unknown\r\n"
    "sample_rate_hz=48000\r\nchannels=radio-L,radio-R,mic\r\n"
    "frames=0\r\naudio_parts=1\r\npart_frames=715827712\r\nevents=none\r\n"
    "retune_settle_frames=FM:0,AM:0,SW:512,LW:512\r\norigin=unknown\r\n"
    "mic_compensation_frames=0\r\n";

  Base(&manifest);
  CHECK(SessionManifest_Format(&manifest, text, sizeof(text)) == strlen(expected));
  CHECK(strcmp(text, expected) == 0);
}

static void TestManifestEnded(void)
{
  SessionManifest manifest;
  char text[SESSION_FILES_MANIFEST_BYTES + 1U];

  Base(&manifest);
  manifest.state = SESSION_STATE_ENDED;
  strcpy(manifest.end_reason, "stopped");
  manifest.frames = 2883584U;
  manifest.origin_known = true;
  manifest.origin_epoch = 1U;
  manifest.origin_frame = 2245835U;
  manifest.events_present = true;
  manifest.events_written = 963U;
  CHECK(SessionManifest_Format(&manifest, text, sizeof(text)) != 0U);
  CHECK(strstr(text, "state=ended\r\nend_reason=stopped\r\n") != NULL);
  CHECK(strstr(text, "frames=2883584\r\n") != NULL);
  CHECK(strstr(text, "origin=1:2245835\r\n") != NULL);
  CHECK(strstr(text, "events=EVENTS.BIN\r\nevent_format_version=1\r\n"
                     "events_written=963\r\nevents_lost=0\r\n") != NULL);
}

/* docs/design/session-format.md: the worst case stays under 1,024 bytes. */
static void TestManifestWorstCaseFits(void)
{
  SessionManifest manifest;
  char text[SESSION_FILES_MANIFEST_BYTES + 1U];
  size_t length;

  memset(&manifest, 0, sizeof(manifest));
  manifest.state = SESSION_STATE_RECORDING;
  memset(manifest.end_reason, 'r', sizeof(manifest.end_reason) - 1U);
  memset(manifest.build, 'b', sizeof(manifest.build) - 1U);
  manifest.boot = 4294967295U;
  manifest.start_uptime_ms = 4294967295U;
  manifest.frames = UINT64_MAX;
  manifest.audio_parts = 1000U;
  manifest.part_frames = 4294967295U;
  manifest.events_present = true;
  manifest.events_written = 4294967295U;
  manifest.events_lost = 4294967295U;
  for (unsigned band = 0U; band < SESSION_FILES_BANDS; ++band)
  {
    manifest.retune_settle_frames[band] = 4294967295U;
  }
  manifest.origin_known = true;
  manifest.origin_epoch = 4294967295U;
  manifest.origin_frame = UINT64_MAX;
  manifest.mic_compensation_frames = 4294967295U;
  length = SessionManifest_Format(&manifest, text, sizeof(text));
  CHECK((length != 0U) && (length <= SESSION_FILES_MANIFEST_BYTES));
  CHECK(strstr(text, "frames=18446744073709551615\r\n") != NULL);
  /* A buffer too small yields nothing rather than a truncated manifest. */
  CHECK(SessionManifest_Format(&manifest, text, 100U) == 0U);
  CHECK(text[0] == '\0');
}

int main(void)
{
  TestNames();
  TestIndex();
  TestFetchPaths();
  TestManifestRecording();
  TestManifestEnded();
  TestManifestWorstCaseFits();
  if (failures != 0)
  {
    printf("%d failure(s)\n", failures);
    return 1;
  }
  printf("session_files_test: all checks passed\n");
  return 0;
}
