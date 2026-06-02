#include "hook_registry.h"
#include <algorithm>
#include <set>
#include <stdio.h>
#include <string.h>
#include <windows.h>

static bool HasSuffix(const std::string& value, const char* suffix) {
    size_t suffixLength = strlen(suffix);
    return value.size() >= suffixLength &&
        value.compare(value.size() - suffixLength, suffixLength, suffix) == 0;
}

std::vector<std::string> DiscoverHooks(const wchar_t* dllPath) {
    std::vector<std::string> hooks;
    HMODULE module = LoadLibraryExW(dllPath, nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (!module) {
        fprintf(stderr, "[!] Failed to inspect hook DLL (%lu)\n", GetLastError());
        return hooks;
    }

    PBYTE base = (PBYTE)module;
    PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)base;
    if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
        FreeLibrary(module);
        return hooks;
    }

    PIMAGE_NT_HEADERS ntHeaders = (PIMAGE_NT_HEADERS)(base + dosHeader->e_lfanew);
    IMAGE_DATA_DIRECTORY exportDirectory = ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (ntHeaders->Signature != IMAGE_NT_SIGNATURE || exportDirectory.VirtualAddress == 0) {
        FreeLibrary(module);
        return hooks;
    }

    PIMAGE_EXPORT_DIRECTORY exports = (PIMAGE_EXPORT_DIRECTORY)(base + exportDirectory.VirtualAddress);
    PDWORD nameAddresses = (PDWORD)(base + exports->AddressOfNames);
    std::set<std::string> exportedNames;
    for (DWORD index = 0; index < exports->NumberOfNames; ++index) {
        exportedNames.insert((const char*)(base + nameAddresses[index]));
    }

    static const char* trampolineSuffix = "Trampoline";
    for (const std::string& exportName : exportedNames) {
        if (!HasSuffix(exportName, trampolineSuffix)) {
            continue;
        }

        std::string hookName = exportName.substr(0, exportName.size() - strlen(trampolineSuffix));
        if (exportedNames.count(hookName + "N") != 0) {
            hooks.push_back(hookName);
        }
    }

    FreeLibrary(module);
    return hooks;
}

bool IsSupportedHook(const std::vector<std::string>& hooks, const std::string& functionName) {
    return std::find(hooks.begin(), hooks.end(), functionName) != hooks.end();
}

void PrintSupportedHooks(const std::vector<std::string>& hooks) {
    for (const std::string& hook : hooks) {
        printf("%s\n", hook.c_str());
    }
}
