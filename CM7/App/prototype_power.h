#ifndef SPOOKY_PROTOTYPE_POWER_H
#define SPOOKY_PROTOTYPE_POWER_H

void PrototypePower_RequestSleep(void);
/* Foreground only. The callback quiesces radio/audio before rail shutdown.
 * Once sleep starts this service does not return; USER/RESET reboots. */
void PrototypePower_Service(void (*stop_radio_audio)(void));
/* Bounded interrupt entry points. */
void PrototypePower_OnRtcWake(void);
void PrototypePower_OnUserButton(void);
#endif
