/* Demo-only Instrument pages (demo_instrument.c, full_spooky_proto-p04.7). */
#include "demo_instrument.h"

#include <stdio.h>
#include <string.h>

static unsigned failures;

#define CHECK(condition) do {                                               \
    if (!(condition)) {                                                     \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);       \
        ++failures;                                                         \
    }                                                                       \
} while (0)

static DemoInstrument instrument;

static void format(DemoParam param, char *text)
{
    DemoInstrument_FormatValue(&instrument.params, param, text, 16u);
}

/* Decision 0011 item 16: page 1 position, size, density, pitch; page 2 spray,
 * nothing (slices removed, 0020 item 6), envelope, level; Encoder 3 click wraps
 * (C-024). */
static void test_pages(void)
{
    DemoInstrument_Init(&instrument);
    CHECK(instrument.page == 0u);
    CHECK(DemoInstrument_Param(0u, 0u) == DEMO_PARAM_POSITION);
    CHECK(DemoInstrument_Param(0u, 1u) == DEMO_PARAM_SIZE);
    CHECK(DemoInstrument_Param(0u, 2u) == DEMO_PARAM_DENSITY);
    CHECK(DemoInstrument_Param(0u, 3u) == DEMO_PARAM_PITCH);
    CHECK(DemoInstrument_Param(1u, 0u) == DEMO_PARAM_SPRAY);
    CHECK(DemoInstrument_Param(1u, 1u) == DEMO_PARAM_COUNT);
    CHECK(DemoInstrument_Param(1u, 2u) == DEMO_PARAM_ENVELOPE);
    CHECK(DemoInstrument_Param(1u, 3u) == DEMO_PARAM_LEVEL);
    CHECK(DemoInstrument_Param(2u, 0u) == DEMO_PARAM_COUNT);
    CHECK(DemoInstrument_Param(0u, 4u) == DEMO_PARAM_COUNT);
    DemoInstrument_NextPage(&instrument);
    CHECK(instrument.page == 1u);
    DemoInstrument_NextPage(&instrument);
    CHECK(instrument.page == 0u);
    for (unsigned param = 0u; param < DEMO_PARAM_COUNT; ++param) {
        CHECK(strlen(DemoInstrument_Name((DemoParam)param)) <= 5u);
    }
}

/* The same encoder moves a different parameter on each page. */
static void test_turns_follow_the_page(void)
{
    char text[16];

    DemoInstrument_Init(&instrument);
    CHECK(DemoInstrument_Turn(&instrument, 0u, 3));
    CHECK(instrument.params.position_permille == 530u);
    CHECK(instrument.params.spray_permille == 0u);
    DemoInstrument_NextPage(&instrument);
    CHECK(DemoInstrument_Turn(&instrument, 0u, 3));
    CHECK(instrument.params.position_permille == 530u);
    CHECK(instrument.params.spray_permille == 30u);
    format(DEMO_PARAM_SPRAY, text);
    CHECK(strcmp(text, "3%") == 0);
    CHECK(!DemoInstrument_Turn(&instrument, 0u, 0));
    CHECK(!DemoInstrument_Turn(&instrument, 9u, 1));
    CHECK(!DemoInstrument_Turn(NULL, 0u, 1));
}

/* Ranges hold at their ends; a turn that changes nothing reports so. */
static void test_ranges(void)
{
    char text[16];

    DemoInstrument_Init(&instrument);
    CHECK(DemoInstrument_Turn(&instrument, 0u, 100));
    CHECK(instrument.params.position_permille == 1000u);
    CHECK(!DemoInstrument_Turn(&instrument, 0u, 1));
    CHECK(DemoInstrument_Turn(&instrument, 0u, -127));
    CHECK(instrument.params.position_permille == 0u);
    CHECK(DemoInstrument_Turn(&instrument, 3u, 100));
    CHECK(instrument.params.pitch_semitones == GRANULAR_PITCH_MAX);
    format(DEMO_PARAM_PITCH, text);
    CHECK(strcmp(text, "+24ST") == 0);
    CHECK(DemoInstrument_Turn(&instrument, 3u, -100));
    CHECK(instrument.params.pitch_semitones == GRANULAR_PITCH_MIN);
    format(DEMO_PARAM_PITCH, text);
    CHECK(strcmp(text, "-24ST") == 0);
    DemoInstrument_NextPage(&instrument);
    CHECK(DemoInstrument_Turn(&instrument, 3u, 10));
    CHECK(instrument.params.level_percent == 100u);
    CHECK(DemoInstrument_Turn(&instrument, 3u, -30));
    CHECK(instrument.params.level_percent == 0u);
    CHECK(strcmp(DemoInstrument_Name(DEMO_PARAM_COUNT), "-") == 0);
    CHECK(!DemoInstrument_Turn(&instrument, 1u, 5)); /* the unassigned slot */
    CHECK(DemoInstrument_Turn(&instrument, 2u, 30));
    CHECK(instrument.params.envelope_percent == 100u);
}

/* Sizes and densities step through their tables. */
static void test_steps(void)
{
    char text[16];

    DemoInstrument_Init(&instrument);
    CHECK(instrument.params.size_ms == 80u);
    CHECK(DemoInstrument_Turn(&instrument, 1u, 1));
    CHECK(instrument.params.size_ms == 100u);
    CHECK(DemoInstrument_Turn(&instrument, 1u, -3));
    CHECK(instrument.params.size_ms == 50u);
    CHECK(DemoInstrument_Turn(&instrument, 1u, -100));
    CHECK(instrument.params.size_ms == GRANULAR_SIZE_MS_MIN);
    CHECK(DemoInstrument_Turn(&instrument, 1u, 100));
    CHECK(instrument.params.size_ms == GRANULAR_SIZE_MS_MAX);
    format(DEMO_PARAM_SIZE, text);
    CHECK(strcmp(text, "500MS") == 0);
    CHECK(DemoInstrument_Turn(&instrument, 2u, 2));
    CHECK(instrument.params.density == 30u);
    format(DEMO_PARAM_DENSITY, text);
    CHECK(strcmp(text, "30/S") == 0);
    CHECK(DemoInstrument_Turn(&instrument, 2u, -100));
    CHECK(instrument.params.density == GRANULAR_DENSITY_MIN);
    CHECK(DemoInstrument_Turn(&instrument, 2u, 100));
    CHECK(instrument.params.density == GRANULAR_DENSITY_MAX);
    /* A value between steps moves from the step at or above it. */
    instrument.params.density = 7u;
    CHECK(DemoInstrument_Turn(&instrument, 2u, 1));
    CHECK(instrument.params.density == 10u);
}

/* Every value fits beside its name on one 21-character line. */
static void test_formats_fit(void)
{
    char text[16];

    DemoInstrument_Init(&instrument);
    instrument.params.position_permille = 1000u;
    instrument.params.size_ms = 500u;
    instrument.params.density = 100u;
    instrument.params.pitch_semitones = -24;
    instrument.params.spray_permille = 1000u;
    instrument.params.envelope_percent = 100u;
    instrument.params.level_percent = 100u;
    for (unsigned param = 0u; param < DEMO_PARAM_COUNT; ++param) {
        format((DemoParam)param, text);
        CHECK(strlen(text) <= 7u);
    }
    format(DEMO_PARAM_COUNT, text);
    CHECK(text[0] == '\0');
    DemoInstrument_FormatValue(NULL, DEMO_PARAM_SIZE, text, sizeof(text));
    CHECK(text[0] == '\0');
}

/* The pages only produce parameters the engine accepts unchanged. */
static void test_values_are_in_range(void)
{
    DemoInstrument_Init(&instrument);
    for (int pass = 0; pass < 2; ++pass) {
        for (uint8_t page = 0u; page < DEMO_INSTRUMENT_PAGES; ++page) {
            instrument.page = page;
            for (uint8_t encoder = 0u; encoder < DEMO_INSTRUMENT_ENCODERS; ++encoder) {
                GranularParams copy;

                DemoInstrument_Turn(&instrument, encoder, pass ? 127 : -127);
                copy = instrument.params;
                CHECK(Granular_ClampParams(&copy));
            }
        }
    }
}

int main(void)
{
    test_pages();
    test_turns_follow_the_page();
    test_ranges();
    test_steps();
    test_formats_fit();
    test_values_are_in_range();
    if (failures != 0u) {
        printf("%u failure(s)\n", failures);
        return 1;
    }
    printf("demo_instrument_test passed\n");
    return 0;
}
