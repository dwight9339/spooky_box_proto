#ifndef SPOOKY_APP_DISPATCH_H
#define SPOOKY_APP_DISPATCH_H

/* Drains the M7 event queue through the static routing table (decision 0007).
 * Foreground only: call once per loop pass, after every producer. */
void AppDispatch_Init(void);
void AppDispatch_Service(void);

#endif /* SPOOKY_APP_DISPATCH_H */
