/*
 * Host tests for the radio onset flash of the encoder and button lights
 * (decision 0031 items 5 to 8; 54w.8.1): each onset size raises the boost by
 * its third of the headroom, the boost falls one third per 80 ms continuously,
 * a smaller onset than the boost in progress is ignored and an equal or larger
 * one restarts the flash, the boost raises a light toward full without passing
 * it, and bad configurations are refused. Host results only; not evidence of
 * how a flash looks on the LEDs or on camera.
 */

#include <stdio.h>
#include <stdlib.h>

#include "light_flash.h"

static int failures;
static const char *current_test;

#define CHECK(cond)                                                                   \
    do {                                                                              \
        if (!(cond)) {                                                                \
            printf("FAIL %s (line %d): %s\n", current_test, __LINE__, #cond);        \
            ++failures;                                                               \
        }                                                                             \
    } while (0)

static LightFlash flash;

static void start(const char *name)
{
    LightFlashConfig config;

    current_test = name;
    LightFlash_DefaultConfig(&config);
    CHECK(LightFlash_Init(&flash, &config));
}

static void no_flash_without_an_onset(void)
{
    start("no_flash_without_an_onset");
    CHECK(LightFlash_Boost(&flash, 0u) == 0u);
    CHECK(LightFlash_Boost(&flash, 12345u) == 0u);
}

static void each_size_raises_its_third(void)
{
    start("each_size_raises_its_third");
    LightFlash_OnOnset(&flash, 1u, 1000u);
    CHECK(LightFlash_Boost(&flash, 1000u) == 333u);
    start("each_size_raises_its_third");
    LightFlash_OnOnset(&flash, 2u, 1000u);
    CHECK(LightFlash_Boost(&flash, 1000u) == 667u);
    start("each_size_raises_its_third");
    LightFlash_OnOnset(&flash, 3u, 1000u);
    CHECK(LightFlash_Boost(&flash, 1000u) == 1000u);
}

static void unknown_sizes_are_ignored(void)
{
    start("unknown_sizes_are_ignored");
    LightFlash_OnOnset(&flash, 0u, 1000u);
    CHECK(LightFlash_Boost(&flash, 1000u) == 0u);
    LightFlash_OnOnset(&flash, 4u, 1000u);
    CHECK(LightFlash_Boost(&flash, 1000u) == 0u);
}

/* One third per 80 ms, continuously: a large flash is gone after 240 ms, a
 * medium after 160 ms and a small after 80 ms, like a matrix kick. */
static void the_boost_falls_one_third_per_80_ms(void)
{
    start("the_boost_falls_one_third_per_80_ms");
    LightFlash_OnOnset(&flash, 3u, 1000u);
    CHECK(LightFlash_Boost(&flash, 1040u) == 833u);   /* halfway through a step */
    CHECK(LightFlash_Boost(&flash, 1080u) == 667u);
    CHECK(LightFlash_Boost(&flash, 1160u) == 333u);
    CHECK(LightFlash_Boost(&flash, 1239u) > 0u);
    CHECK(LightFlash_Boost(&flash, 1240u) == 0u);
    CHECK(LightFlash_Boost(&flash, 1100u) == 0u);     /* stays over */

    start("the_boost_falls_one_third_per_80_ms");
    LightFlash_OnOnset(&flash, 2u, 1000u);
    CHECK(LightFlash_Boost(&flash, 1159u) > 0u);
    CHECK(LightFlash_Boost(&flash, 1160u) == 0u);

    start("the_boost_falls_one_third_per_80_ms");
    LightFlash_OnOnset(&flash, 1u, 1000u);
    CHECK(LightFlash_Boost(&flash, 1079u) > 0u);
    CHECK(LightFlash_Boost(&flash, 1080u) == 0u);
}

static void a_smaller_onset_is_ignored(void)
{
    start("a_smaller_onset_is_ignored");
    LightFlash_OnOnset(&flash, 3u, 1000u);
    LightFlash_OnOnset(&flash, 1u, 1020u);            /* 333 below 917 in progress */
    CHECK(LightFlash_Boost(&flash, 1080u) == 667u);   /* the large flash continues */
}

static void an_equal_or_larger_onset_restarts(void)
{
    start("an_equal_or_larger_onset_restarts");
    LightFlash_OnOnset(&flash, 1u, 1000u);
    LightFlash_OnOnset(&flash, 3u, 1040u);
    CHECK(LightFlash_Boost(&flash, 1040u) == 1000u);

    /* Once a large flash has decayed below a small one, the small one wins. */
    start("an_equal_or_larger_onset_restarts");
    LightFlash_OnOnset(&flash, 3u, 1000u);
    LightFlash_OnOnset(&flash, 1u, 1170u);            /* 333 against 292 left */
    CHECK(LightFlash_Boost(&flash, 1170u) == 333u);
    CHECK(LightFlash_Boost(&flash, 1250u) == 0u);

    start("an_equal_or_larger_onset_restarts");
    LightFlash_OnOnset(&flash, 2u, 1000u);
    LightFlash_OnOnset(&flash, 2u, 1000u);
    CHECK(LightFlash_Boost(&flash, 1000u) == 667u);
}

static void the_clock_may_wrap(void)
{
    start("the_clock_may_wrap");
    LightFlash_OnOnset(&flash, 3u, 0xFFFFFFF0u);
    CHECK(LightFlash_Boost(&flash, 0x00000040u) == 667u);   /* 80 ms later */
}

static void the_boost_raises_toward_full(void)
{
    current_test = "the_boost_raises_toward_full";
    CHECK(LightFlash_Apply(400u, 0u) == 400u);
    CHECK(LightFlash_Apply(400u, 1000u) == 1000u);
    CHECK(LightFlash_Apply(400u, 333u) == 600u);
    CHECK(LightFlash_Apply(0u, 667u) == 667u);
    CHECK(LightFlash_Apply(1000u, 1000u) == 1000u);
    CHECK(LightFlash_Apply(1200u, 500u) == 1000u);   /* clamped */
    CHECK(LightFlash_Apply(400u, 1500u) == 1000u);
}

static void bad_configurations_are_refused(void)
{
    LightFlashConfig config;

    current_test = "bad_configurations_are_refused";
    CHECK(!LightFlash_Init(NULL, NULL));
    LightFlash_DefaultConfig(&config);
    config.full_decay_ms = 0u;
    CHECK(!LightFlash_Init(&flash, &config));
    LightFlash_DefaultConfig(&config);
    config.boost_permille[0] = 0u;
    CHECK(!LightFlash_Init(&flash, &config));
    LightFlash_DefaultConfig(&config);
    config.boost_permille[1] = 200u;                  /* below small */
    CHECK(!LightFlash_Init(&flash, &config));
    LightFlash_DefaultConfig(&config);
    config.boost_permille[2] = 1001u;
    CHECK(!LightFlash_Init(&flash, &config));
    LightFlash_DefaultConfig(&config);
    config.boost_permille[0] = 500u;
    config.boost_permille[1] = 500u;                  /* equal sizes are allowed */
    CHECK(LightFlash_Init(&flash, &config));
}

int main(void)
{
    no_flash_without_an_onset();
    each_size_raises_its_third();
    unknown_sizes_are_ignored();
    the_boost_falls_one_third_per_80_ms();
    a_smaller_onset_is_ignored();
    an_equal_or_larger_onset_restarts();
    the_clock_may_wrap();
    the_boost_raises_toward_full();
    bad_configurations_are_refused();

    if (failures != 0) {
        printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    printf("light_flash_test passed\n");
    return EXIT_SUCCESS;
}
