#include "command_policy.h"

#include <string.h>

#define ALLOW COMMAND_POLICY_ALLOWED
#define REJECT COMMAND_POLICY_REJECTED

static const CommandPolicyRule rules[COMMAND_ACTION_COUNT] = {
  [COMMAND_ACTION_NONE] = {ALLOW, ALLOW, ALLOW, NULL},
  [COMMAND_ACTION_SESSION_START] = {
    ALLOW, REJECT, REJECT, "ERR RECORD already active\r\n"},
  [COMMAND_ACTION_SESSION_STOP] = {ALLOW, ALLOW, ALLOW, NULL},
  [COMMAND_ACTION_SLEEP] = {
    ALLOW, REJECT, REJECT, "ERR SLEEP unavailable while recording\r\n"},
  [COMMAND_ACTION_MODE_CHANGE] = {
    ALLOW, REJECT, REJECT, "ERR MODE unavailable while recording\r\n"},
  [COMMAND_ACTION_FIELD_ENGINE_SWITCH] = {ALLOW, ALLOW, ALLOW, NULL},
  [COMMAND_ACTION_INSTRUMENT_ENGINE_SWITCH] = {
    ALLOW, REJECT, REJECT, "ERR ENGINE unavailable while recording\r\n"},
  [COMMAND_ACTION_INSTRUMENT_VIEW] = {ALLOW, ALLOW, ALLOW, NULL},
  [COMMAND_ACTION_PTT] = {ALLOW, ALLOW, ALLOW, NULL},
  [COMMAND_ACTION_CAPTURE_SAVE] = {ALLOW, ALLOW, ALLOW, NULL},
  [COMMAND_ACTION_PLAYBACK] = {
    ALLOW, REJECT, REJECT, "ERR PLAYBACK unavailable while recording\r\n"},
  [COMMAND_ACTION_SETTINGS_OPEN] = {ALLOW, ALLOW, ALLOW, NULL},
  [COMMAND_ACTION_SETTING_PRESENTATION] = {ALLOW, ALLOW, ALLOW, NULL},
  [COMMAND_ACTION_SETTING_PROTECTED] = {
    ALLOW, REJECT, REJECT, "ERR SETTING unavailable while recording\r\n"},
  [COMMAND_ACTION_SD_MAINTENANCE] = {
    ALLOW, REJECT, REJECT, "ERR SD unavailable while recording\r\n"},
  [COMMAND_ACTION_WAV_TRANSFER] = {
    ALLOW, REJECT, REJECT, "ERR WAV unavailable while recording\r\n"},
  [COMMAND_ACTION_STATUS_READ] = {ALLOW, ALLOW, ALLOW, NULL},
  [COMMAND_ACTION_UI_TEST_PATTERN] = {
    ALLOW, REJECT, REJECT, "ERR UI unavailable while recording\r\n"},
  [COMMAND_ACTION_EMF_ZERO] = {
    ALLOW, REJECT, REJECT, "ERR EMF unavailable while recording\r\n"},
  /* Decision 0003 guard: in-band tuning stays rejected while recording until
   * 54w.6 qualifies it on the bench. The opt-in qualification build allows it so
   * the bench can measure it (constitution, opt-in experiments). The demo image
   * lifts it too, so Classic scans during a session, on the strength of its own
   * bench run (decision 0011 item 13, full_spooky_proto-p04.4); that evidence
   * qualifies the demo image only. */
#if defined(SPOOKY_RADIO_TUNE_QUALIFICATION) || defined(SPOOKY_DEMO)
  [COMMAND_ACTION_RADIO_TUNE] = {ALLOW, ALLOW, ALLOW, NULL},
#else
  [COMMAND_ACTION_RADIO_TUNE] = {
    ALLOW, REJECT, REJECT, "ERR RADIO tuning disabled while recording\r\n"},
#endif
  /* Band changes stay rejected until 54w.12 qualifies them. */
  [COMMAND_ACTION_RADIO_BAND] = {
    ALLOW, REJECT, REJECT, "ERR RADIO tuning disabled while recording\r\n"},
  [COMMAND_ACTION_SCAN_PARAMETER] = {ALLOW, ALLOW, ALLOW, NULL}
};

_Static_assert((sizeof(rules) / sizeof(rules[0])) == COMMAND_ACTION_COUNT,
               "every command action needs a policy row");

static int HasWordPrefix(const char *command, const char *word)
{
  const size_t length = strlen(word);
  return (strncmp(command, word, length) == 0) &&
    ((command[length] == '\0') || (command[length] == ' ') ||
     (command[length] == '\t'));
}

const CommandPolicyRule *CommandPolicy_GetRule(CommandAction action)
{
  if ((action < COMMAND_ACTION_NONE) || (action >= COMMAND_ACTION_COUNT))
  {
    return &rules[COMMAND_ACTION_NONE];
  }
  return &rules[action];
}

CommandPolicyClass CommandPolicy_Evaluate(CommandAction action, SesState state)
{
  const CommandPolicyRule *rule = CommandPolicy_GetRule(action);
  switch (state)
  {
    /* Preparing a recording file holds the card and keeps the radio audio
     * path committed, so it follows the recording rules. */
    case SES_STATE_PREPARING:
    case SES_STATE_RECORDING:
      return rule->recording;
    case SES_STATE_FINALIZING:
      return rule->finalizing;
    case SES_STATE_IDLE:
    default:
      return rule->idle;
  }
}

const char *CommandPolicy_Rejection(CommandAction action)
{
  return CommandPolicy_GetRule(action)->rejection;
}

CommandAction CommandPolicy_ActionFromCli(const char *command)
{
  if (command == NULL)
  {
    return COMMAND_ACTION_NONE;
  }
  if (HasWordPrefix(command, "RECORD START"))
  {
    return COMMAND_ACTION_SESSION_START;
  }
  if (strcmp(command, "RECORD STOP") == 0)
  {
    return COMMAND_ACTION_SESSION_STOP;
  }
  if (HasWordPrefix(command, "RECORD"))
  {
    return COMMAND_ACTION_STATUS_READ;
  }
  if (strcmp(command, "SLEEP START") == 0)
  {
    return COMMAND_ACTION_SLEEP;
  }
  if (HasWordPrefix(command, "WAV"))
  {
    return COMMAND_ACTION_WAV_TRANSFER;
  }
  if ((strcmp(command, "SD") == 0) || (strcmp(command, "SD STATUS") == 0))
  {
    return COMMAND_ACTION_STATUS_READ;
  }
  if (HasWordPrefix(command, "SD"))
  {
    return COMMAND_ACTION_SD_MAINTENANCE;
  }
  if ((strcmp(command, "UI LEDS") == 0) ||
      (strcmp(command, "UI LEDS START") == 0) ||
      (strcmp(command, "UI MATRIX ANIMATE") == 0) ||
      (strcmp(command, "UI MATRIX DEMO") == 0) ||
      (strcmp(command, "UI MATRIX ORIENT") == 0) ||
      (strcmp(command, "UI MATRIX FEEDBACK ON") == 0) ||
      (strcmp(command, "UI DISPLAY") == 0) ||
      HasWordPrefix(command, "UI DISPLAY TEST"))
  {
    return COMMAND_ACTION_UI_TEST_PATTERN;
  }
  if (strcmp(command, "EMF ZERO") == 0)
  {
    return COMMAND_ACTION_EMF_ZERO;
  }
  if (strcmp(command, "CLASSIC") == 0)
  {
    return COMMAND_ACTION_STATUS_READ;
  }
  if (HasWordPrefix(command, "CLASSIC"))
  {
    return COMMAND_ACTION_SCAN_PARAMETER;
  }
  if (strcmp(command, "BAND") == 0)
  {
    return COMMAND_ACTION_STATUS_READ; /* reports the band and its range */
  }
  if (HasWordPrefix(command, "BAND"))
  {
    return COMMAND_ACTION_RADIO_BAND;
  }
  if (HasWordPrefix(command, "TUNE") || (strcmp(command, "UP") == 0) ||
      (strcmp(command, "DOWN") == 0))
  {
    return COMMAND_ACTION_RADIO_TUNE;
  }
  return COMMAND_ACTION_NONE;
}

#undef ALLOW
#undef REJECT
