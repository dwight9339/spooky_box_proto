/*
 * Host tests for monitor-only PTT (C-016, C-017; decision 0028; 54w.9): unity
 * leaves the monitor copy untouched, a press ramps the radio to silence and a
 * release back to unity within the ramp length, each step bounded so neither
 * clicks, a release mid-ramp turns back from where the gain is, and the ramp
 * carries across buffer boundaries. Host results only; not evidence of the
 * radio interrupt's timing or of what is heard.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "monitor_ptt.h"

static int failures;
static const char *current_test;

#define CHECK(cond)                                                                   \
    do {                                                                              \
        if (!(cond)) {                                                                \
            printf("FAIL %s (line %d): %s\n", current_test, __LINE__, #cond);        \
            ++failures;                                                               \
        }                                                                             \
    } while (0)

#define RAMP 240u   /* 5 ms at 48 kHz */
#define HALF 512u   /* frames per radio half */

static int16_t buffer[2u * HALF];

static void fill(int16_t left, int16_t right)
{
    uint32_t frame;
    for (frame = 0; frame < HALF; ++frame) {
        buffer[2u * frame] = left;
        buffer[(2u * frame) + 1u] = right;
    }
}

static void test_unity_is_untouched(void)
{
    MonitorPtt ptt;
    uint32_t frame;
    int ok = 1;
    current_test = "unity_is_untouched";
    MonitorPtt_Init(&ptt, RAMP);
    CHECK(!MonitorPtt_IsOn(&ptt));
    CHECK(MonitorPtt_GainQ15(&ptt) == MONITOR_PTT_UNITY_Q15);
    for (frame = 0; frame < HALF; ++frame) {
        buffer[2u * frame] = (int16_t)((int32_t)(frame * 64u) - 32768);
        buffer[(2u * frame) + 1u] = (int16_t)(32767 - (int32_t)frame);
    }
    MonitorPtt_Apply(&ptt, buffer, HALF);
    for (frame = 0; frame < HALF; ++frame) {
        ok &= buffer[2u * frame] == (int16_t)((int32_t)(frame * 64u) - 32768);
        ok &= buffer[(2u * frame) + 1u] == (int16_t)(32767 - (int32_t)frame);
    }
    CHECK(ok);
}

/* A press ramps down monotonically, silent by the ramp's end and after it. */
static void test_press_ramps_to_silence(void)
{
    MonitorPtt ptt;
    uint32_t frame;
    int32_t previous = 32767;
    int monotonic = 1;
    int silent = 1;
    current_test = "press_ramps_to_silence";
    MonitorPtt_Init(&ptt, RAMP);
    MonitorPtt_Set(&ptt, true);
    CHECK(MonitorPtt_IsOn(&ptt));
    fill(32767, -32768);
    MonitorPtt_Apply(&ptt, buffer, HALF);
    CHECK(buffer[0] < 32767 && buffer[0] > 32000); /* the first frame is one step down */
    for (frame = 0; frame < HALF; ++frame) {
        monotonic &= buffer[2u * frame] <= previous;
        monotonic &= buffer[(2u * frame) + 1u] <= 0 && buffer[(2u * frame) + 1u] >= -32768;
        /* One step is under 0.5 % of full scale: no click. */
        monotonic &= abs(previous - buffer[2u * frame]) <= 150;
        previous = buffer[2u * frame];
        if (frame >= RAMP - 1u) {
            silent &= (buffer[2u * frame] == 0) && (buffer[(2u * frame) + 1u] == 0);
        }
    }
    CHECK(monotonic);
    CHECK(silent);
    CHECK(MonitorPtt_GainQ15(&ptt) == 0u);

    /* Held: every later buffer is silent. */
    fill(12345, -12345);
    MonitorPtt_Apply(&ptt, buffer, HALF);
    silent = 1;
    for (frame = 0; frame < 2u * HALF; ++frame) {
        silent &= buffer[frame] == 0;
    }
    CHECK(silent);
}

/* A release ramps back to unity within the ramp, then leaves samples alone. */
static void test_release_ramps_to_unity(void)
{
    MonitorPtt ptt;
    uint32_t frame;
    int32_t previous = 0;
    int monotonic = 1;
    current_test = "release_ramps_to_unity";
    MonitorPtt_Init(&ptt, RAMP);
    MonitorPtt_Set(&ptt, true);
    fill(1000, 1000);
    MonitorPtt_Apply(&ptt, buffer, HALF);
    MonitorPtt_Set(&ptt, false);
    CHECK(!MonitorPtt_IsOn(&ptt));
    fill(30000, 30000);
    MonitorPtt_Apply(&ptt, buffer, HALF);
    for (frame = 0; frame < HALF; ++frame) {
        monotonic &= buffer[2u * frame] >= previous;
        monotonic &= (buffer[2u * frame] - previous) <= 150;
        previous = buffer[2u * frame];
    }
    CHECK(monotonic);
    CHECK(buffer[2u * (RAMP - 1u)] == 30000);
    CHECK(buffer[2u * (HALF - 1u)] == 30000);
    CHECK(MonitorPtt_GainQ15(&ptt) == MONITOR_PTT_UNITY_Q15);
}

/* A release in mid-ramp turns back from the gain reached, with no jump. */
static void test_release_mid_ramp(void)
{
    MonitorPtt ptt;
    uint32_t mid;
    current_test = "release_mid_ramp";
    MonitorPtt_Init(&ptt, RAMP);
    MonitorPtt_Set(&ptt, true);
    fill(20000, 20000);
    MonitorPtt_Apply(&ptt, buffer, RAMP / 2u);
    mid = MonitorPtt_GainQ15(&ptt);
    CHECK(mid > 0u && mid < MONITOR_PTT_UNITY_Q15);
    MonitorPtt_Set(&ptt, false);
    fill(20000, 20000);
    MonitorPtt_Apply(&ptt, buffer, 1u);
    CHECK(MonitorPtt_GainQ15(&ptt) > mid);
    CHECK(MonitorPtt_GainQ15(&ptt) - mid <= (MONITOR_PTT_UNITY_Q15 / RAMP) + 1u);
    MonitorPtt_Apply(&ptt, buffer, RAMP);
    CHECK(MonitorPtt_GainQ15(&ptt) == MONITOR_PTT_UNITY_Q15);
}

/* The ramp carries over buffer boundaries: four short buffers equal one long. */
static void test_ramp_spans_buffers(void)
{
    MonitorPtt split;
    MonitorPtt whole;
    int16_t reference[2u * HALF];
    uint32_t frame;
    int same = 1;
    current_test = "ramp_spans_buffers";
    MonitorPtt_Init(&split, RAMP);
    MonitorPtt_Init(&whole, RAMP);
    MonitorPtt_Set(&split, true);
    MonitorPtt_Set(&whole, true);
    fill(25000, -25000);
    memcpy(reference, buffer, sizeof(reference));
    MonitorPtt_Apply(&whole, reference, 4u * 60u);
    for (frame = 0; frame < 4u; ++frame) {
        MonitorPtt_Apply(&split, &buffer[2u * 60u * frame], 60u);
    }
    for (frame = 0; frame < 2u * 4u * 60u; ++frame) {
        same &= buffer[frame] == reference[frame];
    }
    CHECK(same);
    CHECK(MonitorPtt_GainQ15(&split) == MonitorPtt_GainQ15(&whole));
}

static void test_zero_ramp_and_null(void)
{
    MonitorPtt ptt;
    current_test = "zero_ramp_and_null";
    MonitorPtt_Init(&ptt, 0u);
    MonitorPtt_Set(&ptt, true);
    fill(30000, 30000);
    MonitorPtt_Apply(&ptt, buffer, 1u);
    CHECK(buffer[0] == 0 && buffer[1] == 0);
    MonitorPtt_Init(NULL, RAMP);
    MonitorPtt_Set(NULL, true);
    MonitorPtt_Apply(NULL, buffer, HALF);
    MonitorPtt_Apply(&ptt, NULL, HALF);
    CHECK(!MonitorPtt_IsOn(NULL));
    CHECK(MonitorPtt_GainQ15(NULL) == MONITOR_PTT_UNITY_Q15);
}

int main(void)
{
    test_unity_is_untouched();
    test_press_ramps_to_silence();
    test_release_ramps_to_unity();
    test_release_mid_ramp();
    test_ramp_spans_buffers();
    test_zero_ramp_and_null();

    if (failures != 0) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("monitor_ptt_test: all tests passed\n");
    return 0;
}
