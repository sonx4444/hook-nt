#pragma once

#include "common.h"
#include <optional>
#include <string>

struct RemoteExport {
    PVOID address;
    std::string forwarder;
};

PVOID InjectDll(HANDLE hProcess, const wchar_t* dllPath);
std::optional<RemoteExport> GetRemoteExport(
    HANDLE hProcess,
    PBYTE dllBase,
    const char* functionName);
std::string GetRemoteModuleName(HANDLE hProcess, PBYTE dllBase);
SIZE_T GetRemoteImageSize(HANDLE hProcess, PBYTE dllBase);
