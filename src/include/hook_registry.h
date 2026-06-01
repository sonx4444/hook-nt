#pragma once

#include <string>
#include <vector>

std::vector<std::string> DiscoverHooks(const wchar_t* dllPath);
bool IsSupportedHook(const std::vector<std::string>& hooks, const std::string& functionName);
void PrintSupportedHooks(const std::vector<std::string>& hooks);
