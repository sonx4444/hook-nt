#pragma once

#include "common.h"

PVOID FindNtdllBase(HANDLE hProcess);
PVOID InjectDll(HANDLE hProcess, const wchar_t* dllPath);
PVOID GetProcAddressRemote(HANDLE hProcess, PBYTE dllBase, const char* functionName);
