#pragma once

#include "common.h"
#include "hook_registry.h"
#include <string>
#include <vector>

static const size_t PATCH_SIZE = 14;

struct RemoteTrampoline {
    PVOID address;
    size_t patchSize;
};

struct InstalledHook {
    std::string canonicalName;
    PVOID sourceModuleBase;
    PVOID targetModuleBase;
    PVOID originalFunction;
    PVOID trampolineAddress;
    PVOID trampolineSlot;
    std::vector<BYTE> originalBytes;
};

// Hook management functions
bool HookFunction(
    HANDLE hProcess,
    PVOID originalFunction,
    PVOID hookImageBase,
    const HookDefinition& definition,
    InstalledHook* installedHook = nullptr);
bool UnhookFunction(HANDLE hProcess, const InstalledHook& installedHook);
bool ReleaseUnloadedHook(HANDLE hProcess, const InstalledHook& installedHook);
size_t CalculatePatchSize(HANDLE hProcess, PVOID address);
bool CreateBypassTrampoline(HANDLE hProcess, PVOID originalFunction, RemoteTrampoline* trampoline);
bool CreateBypassTrampolineForFunction(
    HANDLE hProcess,
    PVOID moduleBase,
    const char* functionName,
    RemoteTrampoline* trampoline);
