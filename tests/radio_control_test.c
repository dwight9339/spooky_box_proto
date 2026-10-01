/*
 * Host tests for the non-blocking in-band tune of the radio control service
 * (full_spooky_proto-54w.28): RadioControl_BeginTune and RadioControl_PollTune
 * against a fake Si4735 on a fake I2C bus and tick. They check that a tune returns
 * before it completes, that each poll is one bounded status transaction, and the
 * timeout and fault paths. Host results only; not hardware evidence, and the
 * timeouts they exercise are the firmware's provisional bounds.
 */

#include <stdio.h>
#include <string.h>

#include "main.h"
#include "radio_control_service.h"

/* --- Fake Si4735 --------------------------------------------------------------- */

#define NEVER 0xFFFFFFFFu

typedef struct FakeRadio {
    uint32_t tune_ms;         /* tune completion delay after TUNE_FREQ */
    bool cts_stuck;           /* never report CTS again */
    bool fail_tune_status;    /* NACK the TUNE_STATUS response */
    bool stc;                 /* STCINT pending */
    uint32_t stc_at;          /* tick when STCINT sets */
    uint16_t tuned_units;     /* frequency of the last TUNE_FREQ, device units */
    uint8_t last_command;
    unsigned transmits;       /* command writes */
    unsigned tune_commands;   /* TUNE_FREQ writes */
    unsigned status_commands; /* GET_INT_STATUS and TUNE_STATUS writes */
} FakeRadio;

static FakeRadio radio;
static I2C_HandleTypeDef i2c1;
GPIO_TypeDef test_gpiob;
uint32_t test_tick;

uint32_t HAL_GetTick(void)
{
    return test_tick;
}

void HAL_Delay(uint32_t ms)
{
    test_tick += ms;
}

static void settle_stc(void)
{
    if (radio.stc_at != NEVER && test_tick >= radio.stc_at) {
        radio.stc = true;
        radio.stc_at = NEVER;
    }
}

HAL_StatusTypeDef HAL_I2C_Master_Transmit(I2C_HandleTypeDef *i2c, uint16_t address,
                                          uint8_t *data, uint16_t length, uint32_t timeout)
{
    (void)i2c;
    (void)address;
    (void)timeout;
    if (length == 0u) {
        return HAL_ERROR;
    }
    ++radio.transmits;
    radio.last_command = data[0];
    switch (data[0]) {
    case 0x20u: /* FM_TUNE_FREQ */
    case 0x40u: /* AM_TUNE_FREQ */
        ++radio.tune_commands;
        radio.tuned_units = (uint16_t)(((uint16_t)data[2] << 8) | data[3]);
        radio.stc = false;
        radio.stc_at = test_tick + radio.tune_ms;
        break;
    case 0x14u: /* GET_INT_STATUS */
        ++radio.status_commands;
        break;
    case 0x22u: /* FM_TUNE_STATUS */
    case 0x42u: /* AM_TUNE_STATUS */
        ++radio.status_commands;
        break;
    default:
        break;
    }
    return HAL_OK;
}

HAL_StatusTypeDef HAL_I2C_Master_Receive(I2C_HandleTypeDef *i2c, uint16_t address,
                                         uint8_t *data, uint16_t length, uint32_t timeout)
{
    (void)i2c;
    (void)address;
    (void)timeout;
    settle_stc();
    if (radio.cts_stuck) {
        data[0] = 0u;
        return HAL_OK;
    }
    data[0] = (uint8_t)(0x80u | (radio.stc ? 0x01u : 0u));
    if (length >= 8u) {
        if (radio.fail_tune_status) {
            return HAL_ERROR;
        }
        if ((radio.last_command == 0x22u || radio.last_command == 0x42u)) {
            radio.stc = false; /* INTACK */
        }
        data[1] = 1u; /* valid channel */
        data[2] = (uint8_t)(radio.tuned_units >> 8);
        data[3] = (uint8_t)radio.tuned_units;
        data[4] = 30u; /* RSSI */
        data[5] = 12u; /* SNR */
        for (uint16_t i = 6u; i < length; ++i) {
            data[i] = 0u;
        }
    }
    return HAL_OK;
}

HAL_StatusTypeDef HAL_I2C_IsDeviceReady(I2C_HandleTypeDef *i2c, uint16_t address,
                                        uint32_t trials, uint32_t timeout)
{
    (void)i2c;
    (void)address;
    (void)trials;
    (void)timeout;
    return HAL_OK;
}

uint32_t HAL_I2C_GetError(I2C_HandleTypeDef *i2c)
{
    (void)i2c;
    return 0u;
}

void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state)
{
    (void)port;
    (void)pin;
    (void)state;
}

GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin)
{
    (void)port;
    (void)pin;
    return GPIO_PIN_SET; /* I2C1 pull-ups present */
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

/* A started receiver in FM at the 99.1 MHz default. */
static void start(void)
{
    memset(&radio, 0, sizeof(radio));
    radio.stc_at = NEVER;
    radio.tune_ms = 40u;
    test_tick = 1000u;
    RadioControl_HoldReset(); /* drop any tune an earlier scenario left in flight */
    CHECK(RadioControl_Start(&i2c1));
}

static RadioControlStatus status(void)
{
    RadioControlStatus s;
    CHECK(RadioControl_GetStatus(&s));
    return s;
}

/* Polls once per simulated 1 ms foreground pass until the tune leaves PENDING. */
static RadioTunePoll poll_until_settled(RadioTuneStatus *result, uint32_t limit_ms)
{
    RadioTunePoll poll = RADIO_TUNE_POLL_PENDING;
    for (uint32_t i = 0u; i < limit_ms && poll == RADIO_TUNE_POLL_PENDING; ++i) {
        poll = RadioControl_PollTune(result);
        if (poll == RADIO_TUNE_POLL_PENDING) {
            ++test_tick;
        }
    }
    return poll;
}

/* --- Scenarios ----------------------------------------------------------------- */

static void a_tune_returns_before_it_completes(void)
{
    start();
    const uint32_t before = test_tick;
    CHECK(RadioControl_BeginTune(101500u));
    CHECK(test_tick - before <= 2u);
    CHECK(radio.tune_commands == 2u); /* the start tune and this one */
    CHECK(status().tune_in_flight);
    /* The published result stays the last completed tune (Principle II). */
    CHECK(status().tune.frequency_khz == 99100u);
    CHECK(status().target_khz == 101500u);
}

static void completion_publishes_the_result_the_receiver_reports(void)
{
    RadioTuneStatus result = {0};
    start();
    CHECK(RadioControl_BeginTune(101504u)); /* FM rounds to the 10 kHz unit */
    CHECK(radio.tuned_units == 10150u);
    CHECK(poll_until_settled(&result, 100u) == RADIO_TUNE_POLL_DONE);
    CHECK(result.frequency_khz == 101500u);
    CHECK(result.band == RADIO_BAND_FM);
    CHECK(result.valid);
    CHECK(status().tune.frequency_khz == 101500u);
    CHECK(!status().tune_in_flight);
    CHECK(status().last_fault == RADIO_CONTROL_FAULT_NONE);
    CHECK(RadioControl_PollTune(NULL) == RADIO_TUNE_POLL_IDLE);
}

static void each_poll_is_one_bounded_status_transaction(void)
{
    start();
    CHECK(RadioControl_BeginTune(95000u));
    ++test_tick;
    ++test_tick;
    const unsigned status_before = radio.status_commands;
    const uint32_t before = test_tick;
    CHECK(RadioControl_PollTune(NULL) == RADIO_TUNE_POLL_PENDING);
    CHECK(radio.status_commands == status_before + 1u); /* GET_INT_STATUS only */
    CHECK(test_tick == before); /* a ready receiver costs no delay */
    /* Polls closer together than the 2 ms interval touch the bus not at all. */
    CHECK(RadioControl_PollTune(NULL) == RADIO_TUNE_POLL_PENDING);
    CHECK(radio.status_commands == status_before + 1u);
}

static void a_tune_that_never_completes_fails_at_the_device_timeout(void)
{
    start();
    radio.tune_ms = 5000u;
    const uint32_t begun = test_tick;
    CHECK(RadioControl_BeginTune(95000u));
    CHECK(poll_until_settled(NULL, 3000u) == RADIO_TUNE_POLL_FAILED);
    CHECK(test_tick - begun >= 2000u);
    CHECK(test_tick - begun <= 2003u);
    CHECK(!status().tune_in_flight);
    CHECK(status().last_fault == RADIO_CONTROL_FAULT_TUNE);
    CHECK(status().tune.frequency_khz == 99100u);
}

static void a_stuck_receiver_fails_the_poll_within_its_bound(void)
{
    start();
    CHECK(RadioControl_BeginTune(95000u));
    test_tick += 2u;
    radio.cts_stuck = true;
    const uint32_t before = test_tick;
    CHECK(RadioControl_PollTune(NULL) == RADIO_TUNE_POLL_FAILED);
    /* One device-ready wait of the 5 ms fast bound, not the 2 s blocking one. */
    CHECK(test_tick - before <= 6u);
    CHECK(status().last_fault == RADIO_CONTROL_FAULT_TUNE);
    CHECK(status().tune.frequency_khz == 99100u);
}

static void a_stuck_receiver_fails_the_issue_within_its_bound(void)
{
    start();
    radio.cts_stuck = true;
    const uint32_t before = test_tick;
    CHECK(!RadioControl_BeginTune(95000u));
    CHECK(test_tick - before <= 6u);
    CHECK(!status().tune_in_flight);
    CHECK(status().last_fault == RADIO_CONTROL_FAULT_TUNE);
}

static void a_failed_status_read_fails_the_tune(void)
{
    start();
    radio.tune_ms = 3u;
    CHECK(RadioControl_BeginTune(95000u));
    radio.fail_tune_status = true;
    CHECK(poll_until_settled(NULL, 100u) == RADIO_TUNE_POLL_FAILED);
    CHECK(status().tune.frequency_khz == 99100u);
    CHECK(status().last_fault == RADIO_CONTROL_FAULT_TUNE);
}

static void begin_refuses_out_of_range_busy_and_unpowered(void)
{
    start();
    const unsigned transmits = radio.transmits;
    CHECK(!RadioControl_BeginTune(108100u));
    CHECK(radio.transmits == transmits);
    CHECK(status().last_fault == RADIO_CONTROL_FAULT_TUNE);

    CHECK(RadioControl_BeginTune(95000u));
    CHECK(!RadioControl_BeginTune(96000u)); /* one tune in flight at a time */
    CHECK(radio.tuned_units == 9500u);

    RadioControl_HoldReset();
    CHECK(!status().tune_in_flight);
    CHECK(RadioControl_PollTune(NULL) == RADIO_TUNE_POLL_IDLE);
    CHECK(!RadioControl_BeginTune(95000u));
}

static void a_band_switch_abandons_the_tune_in_flight(void)
{
    RadioTuneStatus result = {0};
    start();
    CHECK(RadioControl_BeginTune(95000u));
    CHECK(RadioControl_SwitchBand(RADIO_BAND_AM, &result));
    CHECK(!status().tune_in_flight);
    CHECK(RadioControl_PollTune(NULL) == RADIO_TUNE_POLL_IDLE);
    CHECK(result.band == RADIO_BAND_AM);
    CHECK(result.frequency_khz == 1000u);
    /* In-band tunes now use the AM range and 1 kHz units. */
    CHECK(RadioControl_TuneInRange(1500u));
    CHECK(!RadioControl_TuneInRange(95000u));
    CHECK(RadioControl_BeginTune(1500u));
    CHECK(radio.tuned_units == 1500u);
    CHECK(poll_until_settled(&result, 100u) == RADIO_TUNE_POLL_DONE);
    CHECK(result.frequency_khz == 1500u);
}

static void step_targets_stop_or_wrap_at_the_band_edges(void)
{
    RadioTuneStatus result = {0};
    start();
    CHECK(RadioControl_StepTarget(true, false) == 99200u);
    CHECK(RadioControl_StepTarget(false, false) == 99000u);

    CHECK(RadioControl_BeginTune(108000u));
    CHECK(poll_until_settled(&result, 100u) == RADIO_TUNE_POLL_DONE);
    CHECK(RadioControl_StepTarget(true, false) == 108000u);
    CHECK(RadioControl_StepTarget(true, true) == 87500u);
    CHECK(RadioControl_StepTarget(false, true) == 107900u);

    CHECK(RadioControl_BeginTune(87500u));
    CHECK(poll_until_settled(&result, 100u) == RADIO_TUNE_POLL_DONE);
    CHECK(RadioControl_StepTarget(false, false) == 87500u);
    CHECK(RadioControl_StepTarget(false, true) == 108000u);
}

#define RUN(test)              \
    do {                       \
        current_test = #test;  \
        test();                \
    } while (0)

int main(void)
{
    RUN(a_tune_returns_before_it_completes);
    RUN(completion_publishes_the_result_the_receiver_reports);
    RUN(each_poll_is_one_bounded_status_transaction);
    RUN(a_tune_that_never_completes_fails_at_the_device_timeout);
    RUN(a_stuck_receiver_fails_the_poll_within_its_bound);
    RUN(a_stuck_receiver_fails_the_issue_within_its_bound);
    RUN(a_failed_status_read_fails_the_tune);
    RUN(begin_refuses_out_of_range_busy_and_unpowered);
    RUN(a_band_switch_abandons_the_tune_in_flight);
    RUN(step_targets_stop_or_wrap_at_the_band_edges);
    if (failures != 0) {
        printf("radio_control_test: %d failure(s)\n", failures);
        return 1;
    }
    puts("radio_control_test: all scenarios passed");
    return 0;
}
