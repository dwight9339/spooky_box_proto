#include <stdio.h>
#include <string.h>

#include "command_policy.h"

static int failures;
static const char *current_test;

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            printf("FAIL %s (line %d): %s\n", current_test, __LINE__,          \
                   #condition);                                                 \
            ++failures;                                                         \
        }                                                                       \
    } while (0)

static void every_policy_row_has_the_decision_0008_class(void)
{
    static const CommandPolicyClass active[COMMAND_ACTION_COUNT] = {
        COMMAND_POLICY_ALLOWED,  /* none */
        COMMAND_POLICY_REJECTED, /* start another session */
        COMMAND_POLICY_ALLOWED,  /* stop */
        COMMAND_POLICY_REJECTED, /* sleep */
        COMMAND_POLICY_REJECTED, /* operating mode */
        COMMAND_POLICY_ALLOWED,  /* Field engine */
        COMMAND_POLICY_REJECTED, /* Instrument engine */
        COMMAND_POLICY_ALLOWED,  /* Instrument view/edit */
        COMMAND_POLICY_ALLOWED,  /* PTT */
        COMMAND_POLICY_ALLOWED,  /* capture save */
        COMMAND_POLICY_REJECTED, /* Playback */
        COMMAND_POLICY_ALLOWED,  /* Settings */
        COMMAND_POLICY_ALLOWED,  /* allow-listed setting */
        COMMAND_POLICY_REJECTED, /* protected setting */
        COMMAND_POLICY_REJECTED, /* SD maintenance */
        COMMAND_POLICY_REJECTED, /* WAV transfer */
        COMMAND_POLICY_ALLOWED,  /* status/diagnostic read */
        COMMAND_POLICY_REJECTED, /* UI test pattern */
        COMMAND_POLICY_REJECTED, /* EMF calibration */
#if defined(SPOOKY_RADIO_TUNE_QUALIFICATION)
        COMMAND_POLICY_ALLOWED,  /* in-band tuning: opt-in qualification build */
#else
        COMMAND_POLICY_REJECTED, /* in-band tuning: guard until 54w.6 qualifies it */
#endif
        COMMAND_POLICY_REJECTED  /* band change: guard until 54w.12 qualifies it */
    };
    for (int action = COMMAND_ACTION_NONE; action < COMMAND_ACTION_COUNT; ++action) {
        const CommandPolicyRule *rule = CommandPolicy_GetRule((CommandAction)action);
        CHECK(CommandPolicy_Evaluate((CommandAction)action, SES_STATE_IDLE) ==
              COMMAND_POLICY_ALLOWED);
        CHECK(CommandPolicy_Evaluate((CommandAction)action, SES_STATE_RECORDING) ==
              active[action]);
        CHECK(CommandPolicy_Evaluate((CommandAction)action, SES_STATE_FINALIZING) ==
              active[action]);
        CHECK(rule->recording == active[action]);
    }
}

static void every_rejection_has_one_stable_acknowledgement(void)
{
    for (int action = COMMAND_ACTION_NONE; action < COMMAND_ACTION_COUNT; ++action) {
        const CommandPolicyClass outcome =
            CommandPolicy_Evaluate((CommandAction)action, SES_STATE_RECORDING);
        const char *reply = CommandPolicy_Rejection((CommandAction)action);
        if (outcome == COMMAND_POLICY_REJECTED) {
            CHECK(reply != NULL);
            CHECK(strncmp(reply, "ERR ", 4u) == 0);
            CHECK(strstr(reply, "recording") != NULL ||
                  (action == COMMAND_ACTION_SESSION_START));
        } else {
            CHECK(reply == NULL);
        }
        CHECK(outcome != COMMAND_POLICY_DEFERRED);
    }
}

static void current_cli_commands_map_to_the_shared_actions(void)
{
    CHECK(CommandPolicy_ActionFromCli("RECORD START 60") ==
          COMMAND_ACTION_SESSION_START);
    CHECK(CommandPolicy_ActionFromCli("RECORD STOP") == COMMAND_ACTION_SESSION_STOP);
    CHECK(CommandPolicy_ActionFromCli("RECORD STATUS") == COMMAND_ACTION_STATUS_READ);
    CHECK(CommandPolicy_ActionFromCli("RECORD RESULT") == COMMAND_ACTION_STATUS_READ);
    CHECK(CommandPolicy_ActionFromCli("SLEEP START") == COMMAND_ACTION_SLEEP);
    CHECK(CommandPolicy_ActionFromCli("WAV FETCH REC000.WAV") ==
          COMMAND_ACTION_WAV_TRANSFER);
    CHECK(CommandPolicy_ActionFromCli("SD STATUS") == COMMAND_ACTION_STATUS_READ);
    CHECK(CommandPolicy_ActionFromCli("SD REINIT") == COMMAND_ACTION_SD_MAINTENANCE);
    CHECK(CommandPolicy_ActionFromCli("SD INFO") == COMMAND_ACTION_SD_MAINTENANCE);
    CHECK(CommandPolicy_ActionFromCli("SD FORMAT") == COMMAND_ACTION_SD_MAINTENANCE);
    CHECK(CommandPolicy_ActionFromCli("SD FORMAT CONFIRM") == COMMAND_ACTION_SD_MAINTENANCE);
    CHECK(CommandPolicy_ActionFromCli("UI LEDS") == COMMAND_ACTION_UI_TEST_PATTERN);
    CHECK(CommandPolicy_ActionFromCli("UI MATRIX ANIMATE") ==
          COMMAND_ACTION_UI_TEST_PATTERN);
    CHECK(CommandPolicy_ActionFromCli("UI DISPLAY TEST 2") ==
          COMMAND_ACTION_UI_TEST_PATTERN);
    CHECK(CommandPolicy_ActionFromCli("UI STATUS") == COMMAND_ACTION_NONE);
    CHECK(CommandPolicy_ActionFromCli("EMF ZERO") == COMMAND_ACTION_EMF_ZERO);
    CHECK(CommandPolicy_ActionFromCli("EMF READ") == COMMAND_ACTION_NONE);
    CHECK(CommandPolicy_ActionFromCli("BAND FM") == COMMAND_ACTION_RADIO_BAND);
    CHECK(CommandPolicy_ActionFromCli("BAND") == COMMAND_ACTION_STATUS_READ);
    CHECK(CommandPolicy_ActionFromCli("TUNE 99100") == COMMAND_ACTION_RADIO_TUNE);
    CHECK(CommandPolicy_ActionFromCli("UP") == COMMAND_ACTION_RADIO_TUNE);
    CHECK(CommandPolicy_ActionFromCli("DOWN") == COMMAND_ACTION_RADIO_TUNE);
    CHECK(CommandPolicy_ActionFromCli("STATUS") == COMMAND_ACTION_NONE);
}

#define RUN(test)              \
    do {                       \
        current_test = #test;  \
        test();                \
    } while (0)

int main(void)
{
    RUN(every_policy_row_has_the_decision_0008_class);
    RUN(every_rejection_has_one_stable_acknowledgement);
    RUN(current_cli_commands_map_to_the_shared_actions);
    if (failures != 0) {
        printf("command_policy_test: %d failure(s)\n", failures);
        return 1;
    }
    puts("command_policy_test: all tests passed");
    return 0;
}
