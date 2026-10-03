#ifndef SPOOKY_COMMAND_POLICY_H
#define SPOOKY_COMMAND_POLICY_H

/*
 * Recording-safe command policy from decision 0008. This module is portable:
 * it classifies semantic actions and CLI commands, but performs no I/O and
 * knows nothing about USB, the recorder, or physical input hardware.
 */

#include <stddef.h>

#include "sm/session_port.h"

typedef enum CommandPolicyClass {
  COMMAND_POLICY_ALLOWED = 0,
  COMMAND_POLICY_DEFERRED,
  COMMAND_POLICY_REJECTED
} CommandPolicyClass;

typedef enum CommandAction {
  COMMAND_ACTION_NONE = 0,
  COMMAND_ACTION_SESSION_START,
  COMMAND_ACTION_SESSION_STOP,
  COMMAND_ACTION_SLEEP,
  COMMAND_ACTION_MODE_CHANGE,
  COMMAND_ACTION_FIELD_ENGINE_SWITCH,
  COMMAND_ACTION_INSTRUMENT_ENGINE_SWITCH,
  COMMAND_ACTION_INSTRUMENT_VIEW,
  COMMAND_ACTION_PTT,
  COMMAND_ACTION_CAPTURE_SAVE,
  COMMAND_ACTION_PLAYBACK,
  COMMAND_ACTION_SETTINGS_OPEN,
  COMMAND_ACTION_SETTING_PRESENTATION,
  COMMAND_ACTION_SETTING_PROTECTED,
  COMMAND_ACTION_SD_MAINTENANCE,
  COMMAND_ACTION_WAV_TRANSFER,
  COMMAND_ACTION_STATUS_READ,
  COMMAND_ACTION_UI_TEST_PATTERN,
  COMMAND_ACTION_EMF_ZERO,
  COMMAND_ACTION_RADIO_TUNE, /* TUNE, UP, DOWN: in-band; keeps the former number */
  COMMAND_ACTION_RADIO_BAND, /* BAND <band>: receiver function change */
  /* Field engine parameters: run/pause, direction, rate, distance, edge
   * behavior (C-103 to C-106, C-110). The engine's tunes are checked separately
   * as COMMAND_ACTION_RADIO_TUNE. */
  COMMAND_ACTION_SCAN_PARAMETER,
  COMMAND_ACTION_COUNT
} CommandAction;

typedef struct CommandPolicyRule {
  CommandPolicyClass idle;
  CommandPolicyClass recording;
  CommandPolicyClass finalizing;
  const char *rejection;
} CommandPolicyRule;

const CommandPolicyRule *CommandPolicy_GetRule(CommandAction action);
CommandPolicyClass CommandPolicy_Evaluate(CommandAction action, SesState state);
const char *CommandPolicy_Rejection(CommandAction action);
CommandAction CommandPolicy_ActionFromCli(const char *command);

#endif /* SPOOKY_COMMAND_POLICY_H */
