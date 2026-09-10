#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
bool AEImportImage(const char *source, const char *destination, uint64_t limit, char *error, size_t errorCapacity);
bool AEHasGetTaskAllow(void);
bool AEIsDebugged(void);
bool AEIsDebuggerAttached(void);
// -1 means unknown. Never interpret unknown as absent.
int AETXMPresence(void);
int AESPTMPresence(void);
// Only call with universal script attached when needsProtocol is true.
bool AEPrepareJITArena(size_t bytes, bool needsProtocol, char *error, size_t errorCapacity);
bool AEJITArenaReady(void);
const void *AEJITExecutableBase(void);
void *AEJITWritableBase(void);
size_t AEJITArenaSize(void);
#ifdef __cplusplus
}
#endif

#ifdef __OBJC__
#import "VMController.h"
#endif
