#pragma once

#include "common.h"

PVOID InjectDll(HANDLE hProcess, const wchar_t* dllPath);
PVOID GetProcAddressRemote(HANDLE hProcess, PBYTE dllBase, const char* functionName);
