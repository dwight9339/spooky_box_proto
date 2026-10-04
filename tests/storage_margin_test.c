/*
 * Host tests for the decision 0014 storage-margin warning: thresholds on queue
 * high-water and single-write duration, latching, and the first-crossing edge.
 * Host results only; not hardware evidence.
 */

#include <stdio.h>

#include "storage_margin.h"

static int failures;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            printf("FAIL line %d: %s\n", __LINE__, #cond);                       \
            ++failures;                                                          \
        }                                                                        \
    } while (0)

static const StorageMarginLimits limits = {4U, 341U};

static void TestBelowThresholds(void)
{
  StorageMargin margin;

  StorageMargin_Reset(&margin);
  /* The jjy.8 16 GB card: 129 ms and 2/8 stay OK. */
  CHECK(!StorageMargin_Update(&margin, &limits, 2U, 129U));
  CHECK(!StorageMargin_Update(&margin, &limits, 3U, 340U));
  CHECK(!margin.low);
}

static void TestQueueThreshold(void)
{
  StorageMargin margin;

  StorageMargin_Reset(&margin);
  CHECK(StorageMargin_Update(&margin, &limits, 4U, 20U));
  CHECK(margin.low && (margin.queue_high_water == 4U) && (margin.write_ms == 20U));
  /* Latched: later updates report no new edge and keep the first values. */
  CHECK(!StorageMargin_Update(&margin, &limits, 7U, 607U));
  CHECK(margin.low && (margin.queue_high_water == 4U));
}

static void TestWriteThreshold(void)
{
  StorageMargin margin;

  StorageMargin_Reset(&margin);
  /* The jjy.4 SDSC card: a 607 ms write at 7/8 is low on both counts. */
  CHECK(StorageMargin_Update(&margin, &limits, 1U, 341U));
  CHECK(margin.write_ms == 341U);
  StorageMargin_Reset(&margin);
  CHECK(!margin.low);
  CHECK(StorageMargin_Update(&margin, &limits, 7U, 607U));
}

static void TestNullArguments(void)
{
  StorageMargin margin;

  StorageMargin_Reset(&margin);
  StorageMargin_Reset(NULL);
  CHECK(!StorageMargin_Update(NULL, &limits, 8U, 1000U));
  CHECK(!StorageMargin_Update(&margin, NULL, 8U, 1000U));
  CHECK(!margin.low);
}

int main(void)
{
  TestBelowThresholds();
  TestQueueThreshold();
  TestWriteThreshold();
  TestNullArguments();
  if (failures != 0)
  {
    printf("storage_margin_test: %d failure(s)\n", failures);
    return 1;
  }
  puts("storage_margin_test: all tests passed");
  return 0;
}
