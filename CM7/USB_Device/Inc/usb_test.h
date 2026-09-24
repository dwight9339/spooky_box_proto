#ifndef USB_TEST_H
#define USB_TEST_H

#include <stdbool.h>
#include <stdint.h>

typedef void (*UsbTestLineHandler)(const char *line);

void UsbTest_SetLineHandler(UsbTestLineHandler handler);
bool UsbTest_SendData(const uint8_t *data, uint16_t length);
bool UsbTest_SendText(const char *message);
bool UsbTest_Start(void);
void UsbTest_Stop(void);
void UsbTest_Service(void);

#endif /* USB_TEST_H */
