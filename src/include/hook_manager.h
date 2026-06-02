#pragma once

#include "common.h"
#include <vector>

static const size_t PATCH_SIZE = 14;

struct RemoteTrampoline {
    PVOID address;
    size_t patchSize;
};

struct InstalledHook {
    PVOID originalFunction;
    PVOID trampolineAddress;
    PVOID trampolineSlot;
    std::vector<BYTE> originalBytes;
};

// Hook management functions
bool HookFunction(
    HANDLE hProcess,
    PVOID ntdllBase,
    PVOID ntdllNBase,
    const char* functionName,
    InstalledHook* installedHook = nullptr);
bool UnhookFunction(HANDLE hProcess, const InstalledHook& installedHook);
size_t CalculatePatchSize(HANDLE hProcess, PVOID address);
bool CreateBypassTrampoline(HANDLE hProcess, PVOID originalFunction, RemoteTrampoline* trampoline);
bool CreateBypassTrampolineForFunction(
    HANDLE hProcess,
    PVOID ntdllBase,
    const char* functionName,
    RemoteTrampoline* trampoline);
