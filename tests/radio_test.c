/*
 * Host scenario tests for the Radio machine (docs/design/behavior/RadioSm.puml;
 * decisions 0003 and 0008 items 11-15). The generated machine and the real port
 * run against a fake radio service. Host results only; not hardware evidence.
 */

#include <stdio.h>
#include <string.h>

#include "radio_port.h"

/* --- Fake radio service -------------------------------------------------------- */

#define LOG_CAPACITY 1024u

typedef struct Answer {
    RadPublished event;
    RadCommand command;
    bool has_command;
} Answer;

typedef struct Fake {
    uint32_t minimum_khz;
    uint32_t maximum_khz;
    uint32_t step_khz;
    uint32_t current_khz;
    bool issue_ok;
    bool switch_ok;
    bool tune_in_flight;
    unsigned tunes_issued;
    uint32_t last_target;
    unsigned band_switches;
    unsigned count;
    Answer log[LOG_CAPACITY];
} Fake;

static Fake fake;

bool rad_integration_in_range(uint32_t frequency_khz)
{
    return frequency_khz >= fake.minimum_khz && frequency_khz <= fake.maximum_khz;
}

uint32_t rad_integration_step_target(bool up, bool wrap)
{
    if (up) {
        if (fake.current_khz + fake.step_khz <= fake.maximum_khz) {
            return fake.current_khz + fake.step_khz;
        }
        return wrap && fake.current_khz >= fake.maximum_khz ? fake.minimum_khz : fake.maximum_khz;
    }
    if (fake.current_khz >= fake.minimum_khz + fake.step_khz) {
        return fake.current_khz - fake.step_khz;
    }
    return wrap && fake.current_khz <= fake.minimum_khz ? fake.maximum_khz : fake.minimum_khz;
}

bool rad_integration_begin_tune(uint32_t frequency_khz)
{
    fake.last_target = frequency_khz;
    if (!fake.issue_ok) {
        return false;
    }
    ++fake.tunes_issued;
    fake.tune_in_flight = true;
    return true;
}

bool rad_integration_switch_band(uint32_t band)
{
    (void)band;
    ++fake.band_switches;
    return fake.switch_ok;
}

void rad_integration_publish(RadPublished event, const RadCommand *command)
{
    if (fake.count < LOG_CAPACITY) {
        fake.log[fake.count].event = event;
        fake.log[fake.count].has_command = command != NULL;
        if (command != NULL) {
            fake.log[fake.count].command = *command;
        }
    }
    ++fake.count;
}

/* --- Helpers ------------------------------------------------------------------- */

static int failures;
static const char *current_test;

#define CHECK(cond)                                                                   \
    do {                                                                              \
        if (!(cond)) {                                                                \
            printf("FAIL %s (line %d): %s\n", current_test, __LINE__, #cond);        \
            ++failures;                                                               \
        }                                                                             \
    } while (0)

static void reset(void)
{
    memset(&fake, 0, sizeof(fake));
    fake.minimum_khz = 87500u;
    fake.maximum_khz = 108000u;
    fake.step_khz = 100u;
    fake.current_khz = 99100u;
    fake.issue_ok = true;
    fake.switch_ok = true;
    Radio_Init();
    Radio_OnStarted(true);
    fake.count = 0;
}

/* Commands carry a tag in `arg` bits the tests can recognise in answers. */
static void tune(uint32_t khz)
{
    const RadCommand c = {RAD_CMD_TUNE, RAD_SOURCE_CLI, 0u, 0u, khz, 0u};
    Radio_OnCommand(&c);
}

static void step(bool up, bool wrap)
{
    const RadCommand c = {RAD_CMD_STEP, RAD_SOURCE_INTERNAL, up ? 1u : 0u, wrap ? 1u : 0u, 0u, 0u};
    Radio_OnCommand(&c);
}

static void band(uint32_t b)
{
    const RadCommand c = {RAD_CMD_BAND, RAD_SOURCE_CLI, 0u, 0u, b, 0u};
    Radio_OnCommand(&c);
}

static void complete(void)
{
    fake.tune_in_flight = false;
    fake.current_khz = fake.last_target;
    Radio_OnTuneDone();
}

static bool answered(unsigned index, RadPublished event)
{
    return index < fake.count && fake.log[index].event == event;
}

static unsigned count_of(RadPublished event)
{
    unsigned n = 0;
    for (unsigned i = 0; i < fake.count && i < LOG_CAPACITY; ++i) {
        n += fake.log[i].event == event ? 1u : 0u;
    }
    return n;
}

/* --- Scenarios ----------------------------------------------------------------- */

static void start_outcome_selects_settled_or_faulted(void)
{
    Radio_Init();
    CHECK(Radio_GetState() == RAD_STATE_STOPPED);
    tune(99500u);
    CHECK(Radio_GetState() == RAD_STATE_STOPPED);
    Radio_OnStarted(true);
    CHECK(Radio_GetState() == RAD_STATE_SETTLED);
    Radio_Init();
    Radio_OnStarted(false);
    CHECK(Radio_GetState() == RAD_STATE_FAULTED);
}

static void a_tune_waits_for_completion_without_blocking(void)
{
    reset();
    tune(99500u);
    CHECK(Radio_GetState() == RAD_STATE_TUNING);
    CHECK(fake.tunes_issued == 1 && fake.last_target == 99500u);
    CHECK(fake.count == 1 && answered(0, RAD_PUB_TUNE_STARTED));
    complete();
    CHECK(Radio_GetState() == RAD_STATE_SETTLED);
    CHECK(fake.count == 2 && answered(1, RAD_PUB_TUNED));
    CHECK(fake.log[1].command.arg == 99500u && fake.log[1].command.source == RAD_SOURCE_CLI);
}

static void an_out_of_range_tune_is_rejected(void)
{
    reset();
    tune(120000u);
    CHECK(Radio_GetState() == RAD_STATE_SETTLED);
    CHECK(fake.count == 1 && answered(0, RAD_PUB_REJECTED_RANGE));
    CHECK(fake.tunes_issued == 0);
}

static void a_tune_that_cannot_be_issued_fails_and_settles(void)
{
    reset();
    fake.issue_ok = false;
    tune(99500u);
    CHECK(Radio_GetState() == RAD_STATE_SETTLED);
    CHECK(fake.count == 1 && answered(0, RAD_PUB_TUNE_FAILED));
}

static void a_failed_tune_keeps_the_radio_operational(void)
{
    /* Decision 0008 item 14: a tune failure does not fault the radio. */
    reset();
    tune(99500u);
    Radio_OnTuneFailed();
    CHECK(Radio_GetState() == RAD_STATE_SETTLED);
    CHECK(answered(1, RAD_PUB_TUNE_FAILED));
    tune(99700u);
    CHECK(Radio_GetState() == RAD_STATE_TUNING);
}

static void the_newest_command_replaces_the_pending_one(void)
{
    /* Decision 0008 item 15: latest wins; replaced commands are answered. */
    reset();
    tune(99500u);
    tune(100100u);
    tune(100300u);
    step(true, false);
    CHECK(Radio_GetState() == RAD_STATE_TUNING);
    CHECK(fake.tunes_issued == 1);
    CHECK(count_of(RAD_PUB_SUPERSEDED) == 2);
    complete(); /* 99500 done; the pending step starts from it */
    CHECK(fake.tunes_issued == 2 && fake.last_target == 99600u);
    CHECK(Radio_GetState() == RAD_STATE_TUNING);
    complete();
    CHECK(Radio_GetState() == RAD_STATE_SETTLED);
    CHECK(count_of(RAD_PUB_TUNED) == 2);
    /* Every command got exactly one final answer. */
    const unsigned finals = count_of(RAD_PUB_TUNED) + count_of(RAD_PUB_SUPERSEDED);
    CHECK(finals == 4);
}

static void a_band_command_waits_behind_a_tune(void)
{
    reset();
    tune(99500u);
    band(1u);
    CHECK(fake.band_switches == 0);
    complete();
    CHECK(fake.band_switches == 1);
    CHECK(Radio_GetState() == RAD_STATE_SETTLED);
    CHECK(count_of(RAD_PUB_BAND_CHANGED) == 1);
}

static void a_failed_band_switch_faults_the_radio(void)
{
    reset();
    fake.switch_ok = false;
    band(2u);
    CHECK(Radio_GetState() == RAD_STATE_FAULTED);
    CHECK(count_of(RAD_PUB_FAULT_BAND) == 1);
    tune(99500u);
    step(false, false);
    band(0u);
    CHECK(count_of(RAD_PUB_REJECTED_UNAVAILABLE) == 3);
    CHECK(fake.tunes_issued == 0);
}

static void an_audio_fault_abandons_work_in_progress(void)
{
    reset();
    tune(99500u);
    tune(100100u);
    Radio_OnAudioFault();
    CHECK(Radio_GetState() == RAD_STATE_FAULTED);
    CHECK(count_of(RAD_PUB_ABANDONED) == 2);
    CHECK(count_of(RAD_PUB_FAULT_AUDIO) == 1);
    /* A late completion from the service changes nothing. */
    Radio_OnTuneDone();
    CHECK(Radio_GetState() == RAD_STATE_FAULTED && count_of(RAD_PUB_TUNED) == 0);
}

static void steps_stop_or_wrap_at_the_band_edge(void)
{
    reset();
    fake.current_khz = 108000u;
    step(true, false);
    CHECK(fake.last_target == 108000u);
    complete();
    step(true, true);
    CHECK(fake.last_target == 87500u);
    complete();
    step(false, true);
    CHECK(fake.last_target == 108000u);
}

static void stale_completions_are_ignored(void)
{
    reset();
    Radio_OnTuneDone();
    Radio_OnTuneFailed();
    CHECK(Radio_GetState() == RAD_STATE_SETTLED && fake.count == 0);
}

/* Random sequences: every command is answered exactly once, and at most one tune
 * is in flight. */
static void every_command_is_answered_exactly_once(void)
{
    uint32_t rng = 2024u;
    for (int run = 0; run < 200; ++run) {
        reset();
        unsigned commands = 0;
        for (int i = 0; i < 300; ++i) {
            rng = rng * 1103515245u + 12345u;
            const uint32_t r = rng >> 8;
            fake.issue_ok = (r & 0x70u) != 0u;
            fake.switch_ok = (r & 0x380u) != 0u;
            switch (r % 9u) {
            case 0: case 1: tune(87000u + (r >> 4) % 22000u); ++commands; break;
            case 2: step((r & 0x10u) != 0u, (r & 0x20u) != 0u); ++commands; break;
            case 3: if (r % 11u == 0u) { band((r >> 4) % 4u); ++commands; } break;
            case 4: case 5: if (fake.tune_in_flight) { complete(); } break;
            case 6: if (fake.tune_in_flight) { fake.tune_in_flight = false; Radio_OnTuneFailed(); } break;
            case 7: if (r % 97u == 0u) { Radio_OnAudioFault(); } break;
            default: break;
            }
            if (Radio_GetState() != RAD_STATE_TUNING) {
                fake.tune_in_flight = false;
            }
        }
        /* Drain: finish anything still in flight. */
        for (int i = 0; i < 4 && Radio_GetState() == RAD_STATE_TUNING; ++i) {
            complete();
        }
        unsigned finals = 0;
        for (unsigned i = 0; i < fake.count && i < LOG_CAPACITY; ++i) {
            const Answer *a = &fake.log[i];
            if (a->has_command && a->event != RAD_PUB_TUNE_STARTED) {
                ++finals;
            }
        }
        if (fake.count >= LOG_CAPACITY || finals != commands) {
            printf("FAIL %s: run %d: %u commands, %u final answers (log %u)\n", current_test,
                   run, commands, finals, fake.count);
            ++failures;
            return;
        }
    }
}

#define RUN(test)              \
    do {                       \
        current_test = #test;  \
        test();                \
    } while (0)

int main(void)
{
    RUN(start_outcome_selects_settled_or_faulted);
    RUN(a_tune_waits_for_completion_without_blocking);
    RUN(an_out_of_range_tune_is_rejected);
    RUN(a_tune_that_cannot_be_issued_fails_and_settles);
    RUN(a_failed_tune_keeps_the_radio_operational);
    RUN(the_newest_command_replaces_the_pending_one);
    RUN(a_band_command_waits_behind_a_tune);
    RUN(a_failed_band_switch_faults_the_radio);
    RUN(an_audio_fault_abandons_work_in_progress);
    RUN(steps_stop_or_wrap_at_the_band_edge);
    RUN(stale_completions_are_ignored);
    RUN(every_command_is_answered_exactly_once);
    if (failures != 0) {
        printf("radio_test: %d failure(s)\n", failures);
        return 1;
    }
    puts("radio_test: all scenarios passed");
    return 0;
}
