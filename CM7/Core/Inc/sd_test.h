#ifndef SD_TEST_H
#define SD_TEST_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void SdTest_Start(void);
bool SdTest_HandleCommand(const char *command);
bool SdTest_IsActive(void);
void SdTest_Service(void);
void SdTest_Stop(void);

#ifdef __cplusplus
}
#endif

#endif
