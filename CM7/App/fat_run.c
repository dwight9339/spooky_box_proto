#include "fat_run.h"

#include <stddef.h>

static uint32_t FatRunEntry(const uint8_t *sector, uint32_t index)
{
  const uint8_t *entry = &sector[index * 4U];

  return ((uint32_t)entry[0] | ((uint32_t)entry[1] << 8) |
          ((uint32_t)entry[2] << 16) | ((uint32_t)entry[3] << 24)) & 0x0FFFFFFFU;
}

void FatRun_Init(FatRunSearch *search, uint32_t n_fatent, uint32_t start_cluster,
                 uint32_t needed)
{
  if (search == NULL)
  {
    return;
  }
  search->n_fatent = n_fatent;
  search->needed = needed;
  search->next = ((start_cluster < 2U) || (start_cluster >= n_fatent)) ? 2U : start_cluster;
  search->examined = 0U;
  search->run_start = search->next;
  search->run_length = 0U;
  search->longest = 0U;
  search->state = ((n_fatent <= 2U) || (needed == 0U) || (needed > (n_fatent - 2U)))
    ? FAT_RUN_NOT_FOUND : FAT_RUN_SEARCHING;
}

uint32_t FatRun_NextSector(const FatRunSearch *search)
{
  return (search != NULL) ? (search->next / FAT_RUN_ENTRIES_PER_SECTOR) : 0U;
}

FatRunState FatRun_Feed(FatRunSearch *search, const uint8_t *sector,
                        uint32_t sector_index)
{
  if ((search == NULL) || (sector == NULL))
  {
    return FAT_RUN_NOT_FOUND;
  }
  while ((search->state == FAT_RUN_SEARCHING) &&
         ((search->next / FAT_RUN_ENTRIES_PER_SECTOR) == sector_index))
  {
    const uint32_t cluster = search->next;

    if (FatRunEntry(sector, cluster % FAT_RUN_ENTRIES_PER_SECTOR) == 0U)
    {
      if (search->run_length == 0U)
      {
        search->run_start = cluster;
      }
      if (++search->run_length > search->longest)
      {
        search->longest = search->run_length;
      }
      if (search->run_length >= search->needed)
      {
        search->state = FAT_RUN_FOUND;
      }
    }
    else
    {
      search->run_length = 0U;
    }
    ++search->examined;
    if (search->state != FAT_RUN_SEARCHING)
    {
      break;
    }
    if (search->examined >= (search->n_fatent - 2U))
    {
      search->state = FAT_RUN_NOT_FOUND;
      break;
    }
    if (++search->next >= search->n_fatent)
    {
      search->next = 2U;        /* Wrap; a run cannot continue across it. */
      search->run_length = 0U;
    }
  }
  return search->state;
}
