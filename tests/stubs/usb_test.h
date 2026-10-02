#ifndef TEST_USB_H
#define TEST_USB_H
#include <stdbool.h>
#include <stdint.h>
typedef struct UsbTestRxStats
{
  uint32_t packets;
  uint32_t pauses;
  uint32_t overruns;
  uint32_t queued;
  bool paused;
} UsbTestRxStats;
bool UsbTest_SendText(const char *text);
void UsbTest_GetRxStats(UsbTestRxStats *stats);
#endif
