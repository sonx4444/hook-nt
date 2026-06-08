#include "cli_options.h"
#include "debug_session.h"
#include "hook_registry.h"
#include <stdio.h>
#include <string>
#include <vector>

static std::wstring GetSiblingPath(const wchar_t* fileName) {
    WCHAR executablePath[MAX_PATH];
    DWORD length = GetModuleFileNameW(nullptr, executablePath, MAX_PATH);
    if (length == 0 || length == MAX_PATH) {
        return fileName;
    }

    std::wstring path(executablePath, length);
    size_t separator = path.find_last_of(L"\\/");
    return path.substr(0, separator + 1) + fileName;
}

static void PrintSelectedHooks(const std::vector<HookDefinition>& hooks) {
    fprintf(stderr, "[+] Hooks: ");
    for (const HookDefinition& hook : hooks) {
        fprintf(stderr, "%s ", hook.canonicalName.c_str());
    }
    fprintf(stderr, "\n\n");
}

int wmain(int argc, wchar_t* argv[]) {
    const std::vector<HookDefinition> noHooks;
    CommandLineOptions options = ParseCommandLine(argc, argv, noHooks);
    if (options.mode == CommandMode::Help) {
        PrintUsage();
        return 0;
    }
    if (options.mode == CommandMode::Version) {
        printf("apiscope %s\n", APISCOPE_VERSION);
        return 0;
    }

    std::wstring hookDllPath = GetSiblingPath(L"apiscope-hooks.dll");
    std::vector<HookDefinition> availableHooks = DiscoverHooks(hookDllPath.c_str());
    options = ParseCommandLine(argc, argv, availableHooks);
    if (options.mode == CommandMode::ListHooks) {
        if (availableHooks.empty()) {
            fprintf(stderr, "[!] No hooks discovered in apiscope-hooks.dll\n");
            return 1;
        }
        PrintSupportedHooks(availableHooks);
        return 0;
    }
    if (options.mode == CommandMode::Error) {
        fprintf(stderr, "[!] %s\n", options.error.c_str());
        PrintUsage();
        return 1;
    }

    if (options.mode == CommandMode::Attach &&
        options.targetProcessId == GetCurrentProcessId()) {
        fprintf(stderr, "[!] Refusing to attach apiscope.exe to itself\n");
        return 1;
    }

    fprintf(stderr, "[+] ApiScope - Windows API Tracer\n");
    if (options.mode == CommandMode::Attach) {
        fprintf(stderr, "[+] Attaching to PID: %lu\n", options.targetProcessId);
    } else {
        fwprintf(stderr, L"[+] Target: %ls\n", options.targetArguments[0].c_str());
    }
    PrintSelectedHooks(options.hooks);

    DebugSession session(hookDllPath, options);
    return options.mode == CommandMode::Attach
        ? session.Attach()
        : session.Run();
}
