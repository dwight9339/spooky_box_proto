#include "emf_level.h"

#include <stdio.h>

static unsigned failures;

#define CHECK(condition) do {                                               \
    if (!(condition)) {                                                     \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);       \
        ++failures;                                                         \
    }                                                                       \
} while (0)

static void buckets_follow_decision_0013_boundaries(void)
{
    EmfLevelConfig config;

    EmfLevel_DefaultConfig(&config);
    CHECK(EmfLevel_Bucket(&config, 0u) == 0u);
    CHECK(EmfLevel_Bucket(&config, 31u) == 0u);
    CHECK(EmfLevel_Bucket(&config, 32u) == 1u);
    CHECK(EmfLevel_Bucket(&config, 127u) == 1u);
    CHECK(EmfLevel_Bucket(&config, 128u) == 2u);
    CHECK(EmfLevel_Bucket(&config, 511u) == 2u);
    CHECK(EmfLevel_Bucket(&config, 512u) == 3u);
    CHECK(EmfLevel_Bucket(&config, 2047u) == 3u);
    CHECK(EmfLevel_Bucket(&config, 2048u) == 4u);
    CHECK(EmfLevel_Bucket(&config, 0xFFFFFFFFu) == 4u);
}

static void config_is_validated(void)
{
    EmfLevelConfig config;

    CHECK(!EmfLevel_Init(NULL));
    EmfLevel_DefaultConfig(&config);
    config.stale_ms = 0u;
    CHECK(!EmfLevel_Init(&config));
    EmfLevel_DefaultConfig(&config);
    config.boundary_uT[2] = config.boundary_uT[1];
    CHECK(!EmfLevel_Init(&config));
    EmfLevel_DefaultConfig(&config);
    config.boundary_uT[0] = 0u;
    CHECK(!EmfLevel_Init(&config));
    EmfLevel_DefaultConfig(&config);
    CHECK(EmfLevel_Init(&config));
}

static void unknown_until_measured_with_a_baseline(void)
{
    EmfLevelConfig config;
    EmfLevelReading reading;

    EmfLevel_DefaultConfig(&config);
    CHECK(EmfLevel_Init(&config));
    CHECK(!EmfLevel_Get(0u, NULL));
    CHECK(EmfLevel_Get(1000u, &reading));
    CHECK(reading.state == EMF_LEVEL_NO_SAMPLE);

    EmfLevel_OnSample(1000u, 300u, false);
    CHECK(EmfLevel_Get(1000u, &reading));
    CHECK(reading.state == EMF_LEVEL_NO_BASELINE);
    CHECK(reading.bucket == 0u);
    CHECK(reading.emf_uT == 300u);

    EmfLevel_OnSample(1100u, 300u, true);
    CHECK(EmfLevel_Get(1100u, &reading));
    CHECK(reading.state == EMF_LEVEL_VALID);
    CHECK(reading.bucket == 2u);
}

static void stale_and_faulted_samples_are_not_shown_as_measured(void)
{
    EmfLevelConfig config;
    EmfLevelReading reading;

    EmfLevel_DefaultConfig(&config);
    CHECK(EmfLevel_Init(&config));
    EmfLevel_OnSample(0xFFFFFF00u, 40u, true); /* across tick wrap */
    CHECK(EmfLevel_Get(0xFFFFFF00u + 500u, &reading));
    CHECK(reading.state == EMF_LEVEL_VALID);
    CHECK(reading.age_ms == 500u);
    CHECK(reading.bucket == 1u);
    CHECK(EmfLevel_Get(0xFFFFFF00u + 501u, &reading));
    CHECK(reading.state == EMF_LEVEL_STALE);
    CHECK(reading.bucket == 0u);

    EmfLevel_OnSample(1000u, 3000u, true);
    EmfLevel_OnSensorFault();
    CHECK(EmfLevel_Get(1000u, &reading));
    CHECK(reading.state == EMF_LEVEL_SENSOR_FAULT);
    CHECK(reading.bucket == 0u);
    EmfLevel_OnSample(1050u, 3000u, true);
    CHECK(EmfLevel_Get(1050u, &reading));
    CHECK(reading.state == EMF_LEVEL_VALID);
    CHECK(reading.bucket == 4u);
}

int main(void)
{
    buckets_follow_decision_0013_boundaries();
    config_is_validated();
    unknown_until_measured_with_a_baseline();
    stale_and_faulted_samples_are_not_shown_as_measured();
    if (failures != 0u) {
        printf("%u failure(s)\n", failures);
        return 1;
    }
    printf("emf_level_test: pass\n");
    return 0;
}
