#pragma once

#include "trace_session.h"
#include <string>
#include <vector>

enum class CommandMode {
    Run,
    Attach,
    ListHooks,
    Help,
    Version,
    Error,
};

struct CommandLineOptions {
    CommandMode mode;
    std::vector<std::string> hooks;
    std::vector<std::wstring> targetArguments;
    DWORD targetProcessId;
    TraceOutputOptions output;
    std::string error;
};

CommandLineOptions ParseCommandLine(
    int argc,
    wchar_t* argv[],
    const std::vector<std::string>& availableHooks);
std::wstring BuildWindowsCommandLine(const std::vector<std::wstring>& arguments);
void PrintUsage();
