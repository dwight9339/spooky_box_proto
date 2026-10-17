/*
 * Host tests for the non-blocking in-band tune (full_spooky_proto-54w.28) and band
 * switch (54w.12.2) of the radio control service, against a fake Si4735 on a fake
 * I2C bus and tick. They check that a tune or switch returns before it completes,
 * that each poll is one bounded bus step that never waits, the proven band-switch
 * command order and delays, and the timeout and fault paths. Host results only;
 * not hardware evidence, and the timeouts they exercise are the firmware's
 * provisional bounds.
 */

#include <stdio.h>
#include <string.h>

#include "main.h"
#include "radio_control_service.h"

/* --- Fake Si4735 --------------------------------------------------------------- */

#define NEVER 0xFFFFFFFFu

#define LOG_CAPACITY 64u

typedef struct Written {
    uint8_t bytes[6];
    uint16_t length;
    uint32_t tick;
} Written;

typedef struct FakeRadio {
    uint32_t tune_ms;         /* tune completion delay after TUNE_FREQ */
    uint32_t power_up_ms;     /* CTS stays low this long after POWER_UP */
    uint32_t cts_at;          /* no CTS before this tick */
    bool cts_stuck;           /* never report CTS again */
    uint8_t err_after;        /* report ERR after this command (0: never) */
    bool err;
    bool fail_tune_status;    /* NACK the TUNE_STATUS response */
    bool stc;                 /* STCINT pending */
    uint32_t stc_at;          /* tick when STCINT sets */
    uint16_t tuned_units;     /* frequency of the last TUNE_FREQ, device units */
    uint8_t last_command;
    unsigned transmits;       /* command writes */
    unsigned tune_commands;   /* TUNE_FREQ writes */
    unsigned status_commands; /* GET_INT_STATUS and TUNE_STATUS writes */
    unsigned reads;           /* status and response reads */
    unsigned written;         /* entries in log */
    Written log[LOG_CAPACITY];
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
    if (radio.written < LOG_CAPACITY) {
        Written *w = &radio.log[radio.written++];
        memset(w, 0, sizeof(*w));
        memcpy(w->bytes, data, length < 6u ? length : 6u);
        w->length = length;
        w->tick = test_tick;
    }
    if (radio.err_after != 0u && data[0] == radio.err_after) {
        radio.err = true;
    }
    switch (data[0]) {
    case 0x01u: /* POWER_UP */
        radio.cts_at = test_tick + radio.power_up_ms;
        break;
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
    ++radio.reads;
    if (radio.cts_stuck || test_tick < radio.cts_at) {
        data[0] = 0u;
        return HAL_OK;
    }
    data[0] = (uint8_t)(0x80u | (radio.err ? 0x40u : 0u) | (radio.stc ? 0x01u : 0u));
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
    radio.written = 0u;
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

/* Polls the band switch once per simulated 1 ms foreground pass until it leaves
 * PENDING, recording the longest any one poll waited and the most commands any
 * one poll wrote. */
static uint32_t longest_poll_ms;
static unsigned most_writes_per_poll;

static RadioBandPoll poll_switch(RadioTuneStatus *result, uint32_t limit_ms)
{
    RadioBandPoll poll = RADIO_BAND_POLL_PENDING;
    longest_poll_ms = 0u;
    most_writes_per_poll = 0u;
    for (uint32_t i = 0u; i < limit_ms && poll == RADIO_BAND_POLL_PENDING; ++i) {
        const uint32_t before = test_tick;
        const unsigned writes = radio.transmits;
        poll = RadioControl_PollSwitchBand(result);
        if (test_tick - before > longest_poll_ms) {
            longest_poll_ms = test_tick - before;
        }
        if (radio.transmits - writes > most_writes_per_poll) {
            most_writes_per_poll = radio.transmits - writes;
        }
        if (poll == RADIO_BAND_POLL_PENDING) {
            ++test_tick;
        }
    }
    return poll;
}

static void a_band_switch_begins_without_touching_the_bus(void)
{
    start();
    const unsigned transmits = radio.transmits;
    const unsigned reads = radio.reads;
    const uint32_t before = test_tick;
    CHECK(RadioControl_BeginSwitchBand(RADIO_BAND_AM));
    CHECK(radio.transmits == transmits && radio.reads == reads);
    CHECK(test_tick == before);
    CHECK(status().band_switch_in_flight);
    /* The published band and tune stay the completed FM result (Principle II). */
    CHECK(status().band == RADIO_BAND_FM);
    CHECK(status().tune.frequency_khz == 99100u);
}

static uint16_t property_of(const Written *w)
{
    return (uint16_t)(((uint16_t)w->bytes[2] << 8) | w->bytes[3]);
}

static uint16_t value_of(const Written *w)
{
    return (uint16_t)(((uint16_t)w->bytes[4] << 8) | w->bytes[5]);
}

static void a_band_switch_performs_the_proven_sequence_in_steps(void)
{
    RadioTuneStatus result = {0};
    start();
    radio.power_up_ms = 120u;
    const uint32_t begun = test_tick;
    CHECK(RadioControl_BeginSwitchBand(RADIO_BAND_SW));
    CHECK(poll_switch(&result, 3000u) == RADIO_BAND_POLL_DONE);
    /* No poll waits, and none writes more than one command (two for the first
     * tune's status poll: GET_INT_STATUS, then TUNE_STATUS once STC is set). */
    CHECK(longest_poll_ms == 0u);
    CHECK(most_writes_per_poll <= 2u);
    CHECK(result.band == RADIO_BAND_SW && result.frequency_khz == 6000u && result.valid);
    CHECK(status().band == RADIO_BAND_SW);
    CHECK(status().tune.frequency_khz == 6000u);
    CHECK(!status().band_switch_in_flight);
    CHECK(status().last_fault == RADIO_CONTROL_FAULT_NONE);
    CHECK(RadioControl_PollSwitchBand(NULL) == RADIO_BAND_POLL_IDLE);
    /* At least the fixed delays, the power-up wait and the tune; and with 1 ms
     * passes, little more: a command's reply is read in the poll that wrote
     * it, and the poll that ends a delay writes the next command. */
    CHECK(test_tick - begun >= 71u + 120u + 40u);
    CHECK(test_tick - begun <= 71u + 120u + 40u + 15u);
    if (test_tick - begun > 71u + 120u + 40u + 15u) {
        printf("  switch took %lu ms\n", (unsigned long)(test_tick - begun));
    }

    /* The proven commands, in order, with the fixed delay after each. */
    static const struct {
        uint8_t command;
        uint16_t property;
        uint16_t value;
        uint32_t delay_ms;
    } expected[] = {
        {0x12u, 0x0104u, 0u, 10u},     /* DIGITAL_RATE 0 */
        {0x11u, 0u, 0u, 1u},           /* POWER_DOWN */
        {0x01u, 0u, 0u, 120u},         /* POWER_UP; CTS after the power-up time */
        {0x12u, 0x0201u, 32768u, 10u}, /* REFCLK_FREQ */
        {0x12u, 0x0202u, 1u, 10u},     /* REFCLK_PRESCALE */
        {0x12u, 0x4000u, 50u, 10u},    /* RX_VOLUME */
        {0x12u, 0x4001u, 0u, 10u},     /* RX_HARD_MUTE */
        {0x40u, 0u, 0u, 2u},           /* AM_TUNE_FREQ; status polls follow */
    };
    unsigned i = 0u;
    for (unsigned k = 0u; k < sizeof(expected) / sizeof(expected[0]); ++k, ++i) {
        CHECK(i < radio.written);
        const Written *w = &radio.log[i];
        CHECK(w->bytes[0] == expected[k].command);
        if (expected[k].command == 0x12u) {
            CHECK(property_of(w) == expected[k].property);
            CHECK(value_of(w) == expected[k].value);
        }
        if (expected[k].command == 0x01u) {
            CHECK(w->bytes[1] == 0x01u && w->bytes[2] == 0xB5u); /* AM function, digital */
        }
        if (expected[k].command == 0x40u) {
            CHECK(w->length == 6u);
            CHECK((((uint16_t)w->bytes[2] << 8) | w->bytes[3]) == 6000u);
            CHECK(w->bytes[5] == 1u); /* SW antenna capacitance */
        }
        if (i + 1u < radio.written) {
            CHECK(radio.log[i + 1u].tick - w->tick >= expected[k].delay_ms);
        }
    }
    /* Status polls of the first tune, then digital rate and format. */
    while (i < radio.written &&
           (radio.log[i].bytes[0] == 0x14u || radio.log[i].bytes[0] == 0x42u)) {
        ++i;
    }
    CHECK(i + 2u == radio.written);
    if (i + 2u == radio.written) {
        CHECK(radio.log[i].bytes[0] == 0x12u && property_of(&radio.log[i]) == 0x0104u &&
              value_of(&radio.log[i]) == 48000u);
        CHECK(radio.log[i + 1u].tick - radio.log[i].tick >= 10u);
        CHECK(radio.log[i + 1u].bytes[0] == 0x12u &&
              property_of(&radio.log[i + 1u]) == 0x0102u && value_of(&radio.log[i + 1u]) == 0u);
        CHECK(test_tick - radio.log[i + 1u].tick >= 10u); /* format settles before DONE */
    }
}

static void a_stuck_receiver_fails_the_switch_at_the_step_timeout(void)
{
    start();
    radio.power_up_ms = 100000u; /* POWER_UP never reports CTS */
    const uint32_t begun = test_tick;
    CHECK(RadioControl_BeginSwitchBand(RADIO_BAND_AM));
    CHECK(poll_switch(NULL, 5000u) == RADIO_BAND_POLL_FAILED);
    CHECK(longest_poll_ms == 0u); /* the 2 s wait spans passes */
    CHECK(test_tick - begun >= 2000u);
    CHECK(test_tick - begun <= 2100u);
    CHECK(!status().band_switch_in_flight);
    CHECK(status().last_fault == RADIO_CONTROL_FAULT_BAND_SWITCH);
    CHECK(status().band == RADIO_BAND_FM);
}

static void an_error_status_fails_the_switch(void)
{
    start();
    radio.err_after = 0x01u; /* POWER_UP answers ERR */
    CHECK(RadioControl_BeginSwitchBand(RADIO_BAND_LW));
    CHECK(poll_switch(NULL, 500u) == RADIO_BAND_POLL_FAILED);
    CHECK(radio.written > 0u && radio.log[radio.written - 1u].bytes[0] == 0x01u);
    CHECK(status().last_fault == RADIO_CONTROL_FAULT_BAND_SWITCH);
}

static void a_first_tune_that_never_completes_fails_the_switch(void)
{
    start();
    radio.tune_ms = 5000u;
    CHECK(RadioControl_BeginSwitchBand(RADIO_BAND_AM));
    CHECK(poll_switch(NULL, 5000u) == RADIO_BAND_POLL_FAILED);
    CHECK(longest_poll_ms == 0u);
    /* Digital output was never restarted. */
    CHECK(radio.written > 0u && radio.log[radio.written - 1u].bytes[0] == 0x14u);
    CHECK(status().last_fault == RADIO_CONTROL_FAULT_BAND_SWITCH);
    CHECK(status().tune.frequency_khz == 99100u);
}

static void a_stuck_receiver_during_the_first_tune_fails_within_its_bound(void)
{
    start();
    radio.tune_ms = 1000u;
    const unsigned status_before = radio.status_commands; /* from the start tune */
    CHECK(RadioControl_BeginSwitchBand(RADIO_BAND_AM));
    /* Run the switch into its first tune's status polls, then stop answering. */
    for (uint32_t i = 0u; i < 300u && radio.status_commands == status_before; ++i) {
        CHECK(RadioControl_PollSwitchBand(NULL) == RADIO_BAND_POLL_PENDING);
        ++test_tick;
    }
    CHECK(radio.status_commands > status_before);
    radio.cts_stuck = true;
    test_tick += 2u;
    const uint32_t before = test_tick;
    CHECK(RadioControl_PollSwitchBand(NULL) == RADIO_BAND_POLL_FAILED);
    /* One device-ready wait of the 5 ms fast bound, as for an in-band tune. */
    CHECK(test_tick - before <= 6u);
    CHECK(status().last_fault == RADIO_CONTROL_FAULT_BAND_SWITCH);
}

static void begin_switch_refuses_invalid_unpowered_and_busy(void)
{
    start();
    CHECK(!RadioControl_BeginSwitchBand(RADIO_BAND_COUNT));
    CHECK(RadioControl_BeginSwitchBand(RADIO_BAND_AM));
    CHECK(!RadioControl_BeginSwitchBand(RADIO_BAND_LW)); /* one switch at a time */
    RadioControl_HoldReset();
    CHECK(!status().band_switch_in_flight);
    CHECK(RadioControl_PollSwitchBand(NULL) == RADIO_BAND_POLL_IDLE);
    CHECK(!RadioControl_BeginSwitchBand(RADIO_BAND_AM));
}

static void a_band_switch_abandons_the_tune_in_flight(void)
{
    RadioTuneStatus result = {0};
    start();
    CHECK(RadioControl_BeginTune(95000u));
    CHECK(RadioControl_BeginSwitchBand(RADIO_BAND_AM));
    CHECK(!status().tune_in_flight);
    CHECK(RadioControl_PollTune(NULL) == RADIO_TUNE_POLL_IDLE);
    CHECK(poll_switch(&result, 3000u) == RADIO_BAND_POLL_DONE);
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
    RUN(a_band_switch_begins_without_touching_the_bus);
    RUN(a_band_switch_performs_the_proven_sequence_in_steps);
    RUN(a_stuck_receiver_fails_the_switch_at_the_step_timeout);
    RUN(an_error_status_fails_the_switch);
    RUN(a_first_tune_that_never_completes_fails_the_switch);
    RUN(a_stuck_receiver_during_the_first_tune_fails_within_its_bound);
    RUN(begin_switch_refuses_invalid_unpowered_and_busy);
    RUN(a_band_switch_abandons_the_tune_in_flight);
    RUN(step_targets_stop_or_wrap_at_the_band_edges);
    if (failures != 0) {
        printf("radio_control_test: %d failure(s)\n", failures);
        return 1;
    }
    puts("radio_control_test: all scenarios passed");
    return 0;
}
