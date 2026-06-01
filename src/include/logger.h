#pragma once

#include "common.h"

// Logging functions
int printfN(const char* format, ...);
void HexDump(const char* prefix, const void* data, size_t size);
void LogBuffer(const char* prefix, const void* data, size_t size);
bool InitializeVPrintf();
bool TryGetVPrintfFromMsvcrt();
bool TryLoadMsvcrtAndGetVPrintf();
