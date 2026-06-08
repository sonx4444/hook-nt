#pragma once

#include <string>
#include <vector>

struct HookDefinition {
    std::string moduleName;
    std::string exportName;
    std::string handlerExport;
    std::string trampolineExport;
    std::string canonicalName;
};

std::string NormalizeModuleName(const std::string& moduleName);
std::vector<HookDefinition> DiscoverHooks(const wchar_t* dllPath);
const HookDefinition* FindSupportedHook(
    const std::vector<HookDefinition>& hooks,
    const std::string& canonicalName);
void PrintSupportedHooks(const std::vector<HookDefinition>& hooks);
