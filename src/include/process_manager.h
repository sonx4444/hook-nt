#pragma once

#include "common.h"

// Process management functions
HANDLE CreateSuspendedProcess(const wchar_t* programPath, DWORD* pidOut);
PVOID FindNtdllBase(HANDLE hProcess);
PVOID InjectDll(HANDLE hProcess, const wchar_t* dllPath);
PVOID GetProcAddressRemote(HANDLE hProcess, PBYTE dllBase, const char* functionName);

// Memory utility functions
DWORD GetMemoryProtection(DWORD characteristics);
PVOID ReflectiveDLLInject(HANDLE hProcess, PBYTE lpDllBuffer, SIZE_T dllFileSize);
