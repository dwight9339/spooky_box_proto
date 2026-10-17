/*
 * Host tests for decision 0004 gap bookkeeping (full_spooky_proto-54w.12.1):
 * silenced blocks, closing, and reporting each edge once with exact frames.
 * Host results only; not hardware evidence.
 */

#include <stdio.h>

#include "radio_gap.h"

static int failures;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            printf("FAIL line %d: %s\n", __LINE__, #cond);                       \
            ++failures;                                                          \
        }                                                                        \
    } while (0)

static RadioGap gap;
static RadioGapReporter reporter;

static RadioGapEdges Poll(void)
{
  RadioGapSnapshot now;

  RadioGap_Snapshot(&gap, &now);
  return RadioGap_NewEdges(&reporter, &now);
}

static void Start(void)
{
  RadioGapSnapshot now;

  RadioGap_Reset(&gap);
  RadioGap_Snapshot(&gap, &now);
  RadioGap_ResetReporter(&reporter, &now);
}

static void TestEdgesAreExactAndReportedOnce(void)
{
  RadioGapEdges edges;

  Start();
  edges = Poll();
  CHECK(!edges.start && !edges.end);
  RadioGap_OnSilencedBlock(&gap, 100U);
  RadioGap_OnSilencedBlock(&gap, 101U);
  edges = Poll();
  CHECK(edges.start && !edges.end && edges.first_frame == 51200U);
  RadioGap_OnSilencedBlock(&gap, 102U);
  edges = Poll();
  CHECK(!edges.start && !edges.end);
  RadioGap_Close(&gap);
  edges = Poll();
  CHECK(!edges.start && edges.end);
  CHECK(edges.first_frame == 51200U && edges.last_frame == (103U * 512U) - 1U);
  edges = Poll();
  CHECK(!edges.start && !edges.end && reporter.missed == 0U);
}

/* A gap that begins and ends between two polls reports both edges at once. */
static void TestWholeGapBetweenPolls(void)
{
  RadioGapEdges edges;

  Start();
  RadioGap_OnSilencedBlock(&gap, 7U);
  RadioGap_Close(&gap);
  edges = Poll();
  CHECK(edges.start && edges.end);
  CHECK(edges.first_frame == 3584U && edges.last_frame == 4095U);
  /* Closing again with no gap open changes nothing. */
  RadioGap_Close(&gap);
  edges = Poll();
  CHECK(!edges.start && !edges.end);
}

static void TestOlderUnseenGapsAreCountedMissed(void)
{
  RadioGapEdges edges;

  Start();
  RadioGap_OnSilencedBlock(&gap, 1U);
  RadioGap_Close(&gap);
  RadioGap_OnSilencedBlock(&gap, 9U);
  edges = Poll();
  CHECK(edges.start && !edges.end && edges.first_frame == 9U * 512U);
  CHECK(reporter.missed == 2U); /* the first gap's start and end */
  RadioGap_Close(&gap);
  edges = Poll();
  CHECK(edges.end && edges.last_frame == (10U * 512U) - 1U && reporter.missed == 2U);
}

static void TestNullArguments(void)
{
  RadioGapEdges edges = RadioGap_NewEdges(NULL, NULL);

  CHECK(!edges.start && !edges.end);
  RadioGap_Reset(NULL);
  RadioGap_OnSilencedBlock(NULL, 1U);
  RadioGap_Close(NULL);
  RadioGap_Snapshot(NULL, NULL);
  RadioGap_ResetReporter(NULL, NULL);
}

int main(void)
{
  TestEdgesAreExactAndReportedOnce();
  TestWholeGapBetweenPolls();
  TestOlderUnseenGapsAreCountedMissed();
  TestNullArguments();
  if (failures != 0)
  {
    printf("%d failure(s)\n", failures);
    return 1;
  }
  printf("radio_gap_test: all checks passed\n");
  return 0;
}
