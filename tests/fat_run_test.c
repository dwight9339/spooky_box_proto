/*
 * Host tests for the bounded FAT32 free-run search (jjy.9): runs found within and
 * across sectors, runs broken by used clusters and by the wrap, the not-found
 * end of a full pass, and the clusters examined per sector (the storage service
 * budgets sector reads). Host results only; not hardware evidence.
 */

#include <stdio.h>
#include <string.h>

#include "fat_run.h"

static int failures;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            printf("FAIL line %d: %s\n", __LINE__, #cond);                       \
            ++failures;                                                          \
        }                                                                        \
    } while (0)

#define FAT_SECTORS 8U
#define FAT_ENTRIES (FAT_SECTORS * FAT_RUN_ENTRIES_PER_SECTOR)

static uint8_t fat[FAT_SECTORS][FAT_RUN_SECTOR_BYTES];

static void SetEntry(uint32_t cluster, uint32_t value)
{
  uint8_t *entry = &fat[cluster / FAT_RUN_ENTRIES_PER_SECTOR]
                       [(cluster % FAT_RUN_ENTRIES_PER_SECTOR) * 4U];
  entry[0] = (uint8_t)value;
  entry[1] = (uint8_t)(value >> 8);
  entry[2] = (uint8_t)(value >> 16);
  entry[3] = (uint8_t)(value >> 24);
}

/* Everything used except [first, first + count). */
static void FillUsedExcept(uint32_t first, uint32_t count)
{
  for (uint32_t c = 0U; c < FAT_ENTRIES; ++c)
  {
    SetEntry(c, ((c >= first) && (c < (first + count))) ? 0U : 0x0FFFFFFFU);
  }
}

/* Feeds sectors as the storage service does; returns sectors read. */
static uint32_t Run(FatRunSearch *search, uint32_t n_fatent, uint32_t start,
                    uint32_t needed, uint32_t budget)
{
  uint32_t reads = 0U;

  FatRun_Init(search, n_fatent, start, needed);
  while ((search->state == FAT_RUN_SEARCHING) && (reads < budget))
  {
    uint32_t index = FatRun_NextSector(search);
    (void)FatRun_Feed(search, fat[index], index);
    ++reads;
  }
  return reads;
}

static void TestFoundAtHint(void)
{
  FatRunSearch search;

  FillUsedExcept(300U, 600U); /* Free run spans sectors 2..7. */
  CHECK(Run(&search, FAT_ENTRIES, 300U, 527U, 64U) == 5U);
  CHECK(search.state == FAT_RUN_FOUND);
  CHECK(search.run_start == 300U);
  CHECK(search.run_length == 527U);
}

static void TestUsedClusterBreaksRun(void)
{
  FatRunSearch search;

  FillUsedExcept(10U, 900U);
  SetEntry(100U, 0x00000123U); /* A chain link in the middle. */
  CHECK(Run(&search, FAT_ENTRIES, 2U, 500U, 64U) != 0U);
  CHECK(search.state == FAT_RUN_FOUND);
  CHECK(search.run_start == 101U);

  /* The top four bits of a FAT32 entry are reserved: 0xF0000000 is free. */
  FillUsedExcept(0U, 0U);
  for (uint32_t c = 40U; c < 60U; ++c)
  {
    SetEntry(c, 0xF0000000U);
  }
  CHECK(Run(&search, FAT_ENTRIES, 2U, 20U, 64U) == 1U);
  CHECK((search.state == FAT_RUN_FOUND) && (search.run_start == 40U));
}

static void TestNotFoundAfterOnePass(void)
{
  FatRunSearch search;
  uint32_t reads;

  /* 8 MiB-style holes: free runs of 100 clusters, never 200. */
  FillUsedExcept(0U, 0U);
  for (uint32_t c = 50U; c < 150U; ++c) SetEntry(c, 0U);
  for (uint32_t c = 400U; c < 500U; ++c) SetEntry(c, 0U);
  reads = Run(&search, FAT_ENTRIES, 700U, 200U, 1000U);
  CHECK(search.state == FAT_RUN_NOT_FOUND);
  CHECK(search.examined == (FAT_ENTRIES - 2U));
  CHECK(reads == (FAT_SECTORS + 1U)); /* Start sector is visited twice: before and after the wrap. */
}

static void TestBudgetStopsSearch(void)
{
  FatRunSearch search;

  FillUsedExcept(0U, 0U);
  CHECK(Run(&search, FAT_ENTRIES, 2U, 10U, 3U) == 3U);
  CHECK(search.state == FAT_RUN_SEARCHING); /* Caller gives up: no preallocation. */
  CHECK(search.examined == ((3U * FAT_RUN_ENTRIES_PER_SECTOR) - 2U));
}

static void TestRunDoesNotSpanWrap(void)
{
  FatRunSearch search;
  const uint32_t n_fatent = FAT_ENTRIES - 50U; /* Last FAT sector partly unused. */

  /* Free at the end of the FAT and at its start, used in between. */
  FillUsedExcept(0U, 0U);
  for (uint32_t c = n_fatent - 30U; c < n_fatent; ++c) SetEntry(c, 0U);
  for (uint32_t c = 2U; c < 32U; ++c) SetEntry(c, 0U);
  (void)Run(&search, n_fatent, n_fatent - 30U, 40U, 100U);
  CHECK(search.state == FAT_RUN_NOT_FOUND);
  (void)Run(&search, n_fatent, n_fatent - 30U, 30U, 100U);
  CHECK((search.state == FAT_RUN_FOUND) && (search.run_start == (n_fatent - 30U)));
}

static void TestInitEdges(void)
{
  FatRunSearch search;

  FatRun_Init(&search, FAT_ENTRIES, 0U, 4U);
  CHECK(search.next == 2U); /* Unknown hint (0) starts at the first data cluster. */
  FatRun_Init(&search, FAT_ENTRIES, 0xFFFFFFFFU, 4U);
  CHECK(search.next == 2U);
  FatRun_Init(&search, FAT_ENTRIES, 5U, 0U);
  CHECK(search.state == FAT_RUN_NOT_FOUND);
  FatRun_Init(&search, FAT_ENTRIES, 5U, FAT_ENTRIES);
  CHECK(search.state == FAT_RUN_NOT_FOUND); /* Larger than the volume. */
  CHECK(FatRun_Feed(NULL, fat[0], 0U) == FAT_RUN_NOT_FOUND);
}

int main(void)
{
  TestFoundAtHint();
  TestUsedClusterBreaksRun();
  TestNotFoundAfterOnePass();
  TestBudgetStopsSearch();
  TestRunDoesNotSpanWrap();
  TestInitEdges();
  if (failures != 0)
  {
    printf("fat_run_test: %d failure(s)\n", failures);
    return 1;
  }
  puts("fat_run_test: all tests passed");
  return 0;
}
