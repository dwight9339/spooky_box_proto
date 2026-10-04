#ifndef USB_TEST_H
#define USB_TEST_H

#include <stdbool.h>
#include <stdint.h>

typedef void (*UsbTestLineHandler)(const char *line);

/* Command receive counters since the last enumeration (8lw.24). */
typedef struct UsbTestRxStats
{
  uint32_t packets;  /* OUT packets received. */
  uint32_t pauses;   /* Times reception paused for flow control. */
  uint32_t overruns; /* Packets truncated; zero unless flow control failed. */
  uint32_t queued;   /* Bytes waiting for the command parser. */
  bool paused;
} UsbTestRxStats;

void UsbTest_SetLineHandler(UsbTestLineHandler handler);
bool UsbTest_SendData(const uint8_t *data, uint16_t length);
bool UsbTest_SendText(const char *message);
bool UsbTest_Start(void);
void UsbTest_Stop(void);
void UsbTest_Service(void);
void UsbTest_GetRxStats(UsbTestRxStats *stats);
/* True while a host has the CDC interface configured (8lw.20). */
bool UsbTest_HostAttached(void);

#endif /* USB_TEST_H */
