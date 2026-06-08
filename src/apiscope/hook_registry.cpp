#include "hook_registry.h"
#include "api_hook_descriptor.h"
#include <algorithm>
#include <cctype>
#include <set>
#include <stdio.h>
#include <string.h>
#include <windows.h>

static bool HasPrefix(const std::string& value, const char* prefix) {
    size_t prefixLength = strlen(prefix);
    return value.size() >= prefixLength &&
        value.compare(0, prefixLength, prefix) == 0;
}

template <size_t Capacity>
static bool ReadFixedString(const char (&value)[Capacity], std::string* result) {
    size_t length = 0;
    while (length < Capacity && value[length]) {
        ++length;
    }
    if (length == 0 || length == Capacity) {
        return false;
    }
    result->assign(value, length);
    return true;
}

std::string NormalizeModuleName(const std::string& moduleName) {
    size_t separator = moduleName.find_last_of("\\/");
    std::string normalized = separator == std::string::npos
        ? moduleName
        : moduleName.substr(separator + 1);
    std::transform(
        normalized.begin(),
        normalized.end(),
        normalized.begin(),
        [](unsigned char value) { return (char)std::tolower(value); });
    return normalized;
}

std::vector<HookDefinition> DiscoverHooks(const wchar_t* dllPath) {
    std::vector<HookDefinition> hooks;
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
    IMAGE_DATA_DIRECTORY exportDirectory =
        ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (ntHeaders->Signature != IMAGE_NT_SIGNATURE || exportDirectory.VirtualAddress == 0) {
        FreeLibrary(module);
        return hooks;
    }

    PIMAGE_EXPORT_DIRECTORY exports =
        (PIMAGE_EXPORT_DIRECTORY)(base + exportDirectory.VirtualAddress);
    PDWORD nameAddresses = (PDWORD)(base + exports->AddressOfNames);
    std::set<std::string> canonicalNames;
    static const char* descriptorPrefix = "ApiScopeHook_";

    for (DWORD index = 0; index < exports->NumberOfNames; ++index) {
        const char* exportName = (const char*)(base + nameAddresses[index]);
        if (!HasPrefix(exportName, descriptorPrefix)) {
            continue;
        }

        const ApiHookDescriptor* descriptor =
            (const ApiHookDescriptor*)GetProcAddress(module, exportName);
        HookDefinition hook;
        if (!descriptor ||
            descriptor->magic != API_HOOK_DESCRIPTOR_MAGIC ||
            descriptor->version != API_HOOK_DESCRIPTOR_VERSION ||
            descriptor->size != sizeof(ApiHookDescriptor) ||
            !ReadFixedString(descriptor->moduleName, &hook.moduleName) ||
            !ReadFixedString(descriptor->exportName, &hook.exportName) ||
            !ReadFixedString(descriptor->handlerExport, &hook.handlerExport) ||
            !ReadFixedString(descriptor->trampolineExport, &hook.trampolineExport)) {
            fprintf(stderr, "[!] Invalid hook descriptor export: %s\n", exportName);
            hooks.clear();
            break;
        }

        hook.moduleName = NormalizeModuleName(hook.moduleName);
        hook.canonicalName = hook.moduleName + "!" + hook.exportName;
        if (!canonicalNames.insert(hook.canonicalName).second ||
            !GetProcAddress(module, hook.handlerExport.c_str()) ||
            !GetProcAddress(module, hook.trampolineExport.c_str())) {
            fprintf(stderr, "[!] Duplicate or incomplete hook descriptor: %s\n", exportName);
            hooks.clear();
            break;
        }
        hooks.push_back(hook);
    }

    FreeLibrary(module);
    std::sort(
        hooks.begin(),
        hooks.end(),
        [](const HookDefinition& left, const HookDefinition& right) {
            return left.canonicalName < right.canonicalName;
        });
    return hooks;
}

const HookDefinition* FindSupportedHook(
    const std::vector<HookDefinition>& hooks,
    const std::string& canonicalName) {
    for (const HookDefinition& hook : hooks) {
        if (hook.canonicalName == canonicalName) {
            return &hook;
        }
    }
    return nullptr;
}

void PrintSupportedHooks(const std::vector<HookDefinition>& hooks) {
    for (const HookDefinition& hook : hooks) {
        printf("%s\n", hook.canonicalName.c_str());
    }
}
