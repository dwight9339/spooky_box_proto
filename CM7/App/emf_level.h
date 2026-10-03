#ifndef SPOOKY_EMF_LEVEL_H
#define SPOOKY_EMF_LEVEL_H

#include <stdbool.h>
#include <stdint.h>

/* Semantic EMF level for presentation (decision 0013 items 3 to 5). The M7
 * feeds it the magnetometer's change from baseline; it answers with a bucket
 * only while that measurement is current, and says why when it is not. */

#define EMF_LEVEL_BUCKET_COUNT 4U
#define EMF_LEVEL_BOUNDARY_COUNT (EMF_LEVEL_BUCKET_COUNT - 1U)

typedef struct
{
  /* Lower bound of buckets 1..3 in uT, strictly increasing. Bench trial
   * values 150/400/1200 until bench trials fix them. */
  uint32_t boundary_uT[EMF_LEVEL_BOUNDARY_COUNT];
  /* A sample older than this is stale (500 ms). */
  uint32_t stale_ms;
} EmfLevelConfig;

typedef enum
{
  EMF_LEVEL_VALID = 0,
  EMF_LEVEL_NO_SAMPLE,     /* nothing since init */
  EMF_LEVEL_NO_BASELINE,   /* baseline being captured (boot, EMF ZERO) */
  EMF_LEVEL_STALE,         /* newest sample older than stale_ms */
  EMF_LEVEL_SENSOR_FAULT   /* read failed; cleared by the next sample */
} EmfLevelState;

typedef struct
{
  EmfLevelState state;
  uint8_t bucket;          /* meaningful only when state is EMF_LEVEL_VALID */
  uint32_t emf_uT;         /* newest sample, 0 before the first */
  uint32_t age_ms;         /* age of the newest sample, 0 before the first */
} EmfLevelReading;

void EmfLevel_DefaultConfig(EmfLevelConfig *config);
/* Rejects a NULL config, zero stale time or boundaries that do not increase. */
bool EmfLevel_Init(const EmfLevelConfig *config);
void EmfLevel_OnSample(uint32_t now_ms, uint32_t emf_uT, bool baseline_valid);
void EmfLevel_OnSensorFault(void);
bool EmfLevel_Get(uint32_t now_ms, EmfLevelReading *reading);
uint8_t EmfLevel_Bucket(const EmfLevelConfig *config, uint32_t emf_uT);

#endif /* SPOOKY_EMF_LEVEL_H */
