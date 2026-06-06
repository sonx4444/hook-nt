#include "cli_options.h"
#include "common.h"
#include "hook_manager.h"
#include "hook_registry.h"
#include "process_manager.h"
#include "trace_session.h"
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

static int FailSuspendedProcess(HANDLE hProcess, HANDLE hThread, const char* message) {
    fprintf(stderr, "[!] %s\n", message);
    TerminateProcess(hProcess, 1);
    CloseHandle(hThread);
    CloseHandle(hProcess);
    return 1;
}

typedef NTSTATUS(NTAPI* NtProcessControlProc)(HANDLE ProcessHandle);

static bool SetProcessSuspended(HANDLE process, const char* functionName) {
    NtProcessControlProc controlProcess = (NtProcessControlProc)GetProcAddress(
        GetModuleHandleW(L"ntdll.dll"),
        functionName);
    if (!controlProcess) {
        fprintf(stderr, "[!] %s is unavailable\n", functionName);
        return false;
    }

    NTSTATUS status = controlProcess(process);
    if (!NT_SUCCESS(status)) {
        fprintf(stderr, "[!] %s failed (0x%08X)\n", functionName, (uint32_t)status);
        return false;
    }
    return true;
}

static bool RollbackHooks(HANDLE process, std::vector<InstalledHook>* installedHooks) {
    bool restored = true;
    for (auto hook = installedHooks->rbegin(); hook != installedHooks->rend(); ++hook) {
        if (!UnhookFunction(process, *hook)) {
            restored = false;
        }
    }
    installedHooks->clear();
    return restored;
}

static bool PrepareTraceTarget(
    HANDLE process,
    const std::wstring& dllPath,
    const CommandLineOptions& options,
    TraceSession* traceSession,
    std::vector<InstalledHook>* installedHooks) {
    // Windows maps the same-bitness system ntdll.dll at one boot-session ASLR
    // base, so this x64 controller can use its local base for the x64 target.
    PVOID ntdllBase = GetModuleHandleW(L"ntdll.dll");
    if (!ntdllBase) {
        fprintf(stderr, "[!] ntdll.dll not found\n");
        return false;
    }
    fprintf(stderr, "[+] Found ntdll.dll at 0x%p\n", ntdllBase);

    fprintf(stderr, "[+] Injecting DLL\n");
    PVOID ntdllNBase = InjectDll(process, dllPath.c_str());
    if (!ntdllNBase) {
        fprintf(stderr, "[!] DLL injection failed\n");
        return false;
    }
    fprintf(stderr, "[+] DLL injected at 0x%p\n", ntdllNBase);

    if (!traceSession->Initialize(process, ntdllBase, ntdllNBase, options.output)) {
        fprintf(stderr, "[!] Trace transport setup failed\n");
        return false;
    }

    for (const std::string& hookName : options.hooks) {
        InstalledHook hook;
        if (!HookFunction(process, ntdllBase, ntdllNBase, hookName.c_str(), &hook)) {
            fprintf(stderr, "[!] Failed to hook %s\n", hookName.c_str());
            if (!RollbackHooks(process, installedHooks)) {
                fprintf(stderr, "[!] Failed to roll back installed hooks\n");
            }
            return false;
        }
        installedHooks->push_back(hook);
    }
    fprintf(stderr, "[+] All hooks installed successfully\n");
    return true;
}

int wmain(int argc, wchar_t* argv[]) {
    const std::vector<std::string> noHooks;
    CommandLineOptions options = ParseCommandLine(argc, argv, noHooks);
    if (options.mode == CommandMode::Help) {
        PrintUsage();
        return 0;
    }
    if (options.mode == CommandMode::Version) {
        printf("hooknt %s\n", HOOKNT_VERSION);
        return 0;
    }

    std::wstring dllPath = GetSiblingPath(L"ntdlln.dll");
    std::vector<std::string> availableHooks = DiscoverHooks(dllPath.c_str());
    options = ParseCommandLine(argc, argv, availableHooks);
    if (options.mode == CommandMode::ListHooks) {
        if (availableHooks.empty()) {
            fprintf(stderr, "[!] No hooks discovered in ntdlln.dll\n");
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

    if (options.mode == CommandMode::Attach) {
        if (options.targetProcessId == GetCurrentProcessId()) {
            fprintf(stderr, "[!] Refusing to attach hooknt.exe to itself\n");
            return 1;
        }

        fprintf(stderr, "[+] HookNt - NT API Function Hooker\n");
        fprintf(stderr, "[+] Attaching to PID: %lu\n", options.targetProcessId);
        fprintf(stderr, "[+] Functions to hook: ");
        for (const std::string& hook : options.hooks) {
            fprintf(stderr, "%s ", hook.c_str());
        }
        fprintf(stderr, "\n\n");

        DWORD access = PROCESS_QUERY_INFORMATION |
            PROCESS_VM_OPERATION |
            PROCESS_VM_READ |
            PROCESS_VM_WRITE |
            PROCESS_DUP_HANDLE |
            PROCESS_SUSPEND_RESUME |
            SYNCHRONIZE;
        HANDLE process = OpenProcess(access, FALSE, options.targetProcessId);
        if (!process) {
            fprintf(stderr, "[!] OpenProcess failed (%lu)\n", GetLastError());
            return 1;
        }
        if (!SetProcessSuspended(process, "NtSuspendProcess")) {
            CloseHandle(process);
            return 1;
        }
        fprintf(stderr, "[+] Target suspended for hook installation\n");

        TraceSession traceSession;
        std::vector<InstalledHook> installedHooks;
        if (!PrepareTraceTarget(process, dllPath, options, &traceSession, &installedHooks)) {
            traceSession.Stop();
            if (!SetProcessSuspended(process, "NtResumeProcess")) {
                fprintf(stderr, "[!] Target remains suspended; resume it manually\n");
            }
            CloseHandle(process);
            return 1;
        }
        if (!SetProcessSuspended(process, "NtResumeProcess")) {
            if (!RollbackHooks(process, &installedHooks)) {
                fprintf(stderr, "[!] Failed to roll back installed hooks\n");
            }
            traceSession.Stop();
            fprintf(stderr, "[!] Target remains suspended; resume it manually\n");
            CloseHandle(process);
            return 1;
        }
        fprintf(stderr, "[+] Target resumed; logging until process exit...\n");

        WaitForSingleObject(process, INFINITE);
        traceSession.Stop();

        DWORD exitCode = 0;
        if (!GetExitCodeProcess(process, &exitCode)) {
            fprintf(stderr, "[!] GetExitCodeProcess failed (%lu)\n", GetLastError());
            CloseHandle(process);
            return 1;
        }
        CloseHandle(process);
        fprintf(stderr, "[+] Target exited with code %lu\n", exitCode);
        return 0;
    }

    fprintf(stderr, "[+] HookNt - NT API Function Hooker\n");
    fwprintf(stderr, L"[+] Target: %ls\n", options.targetArguments[0].c_str());
    fprintf(stderr, "[+] Functions to hook: ");
    for (const std::string& hook : options.hooks) {
        fprintf(stderr, "%s ", hook.c_str());
    }
    fprintf(stderr, "\n\n");

    std::wstring commandLine = BuildWindowsCommandLine(options.targetArguments);
    std::vector<WCHAR> commandLineBuffer(commandLine.begin(), commandLine.end());
    commandLineBuffer.push_back(L'\0');

    STARTUPINFOW startupInfo = {sizeof(startupInfo)};
    PROCESS_INFORMATION processInfo;
    if (!CreateProcessW(
            options.targetArguments[0].c_str(),
            commandLineBuffer.data(),
            nullptr,
            nullptr,
            TRUE,
            CREATE_SUSPENDED,
            nullptr,
            nullptr,
            &startupInfo,
            &processInfo)) {
        fprintf(stderr, "[!] CreateProcessW failed (%lu)\n", GetLastError());
        return 1;
    }
    fprintf(stderr, "[+] Process created, PID: %lu\n", processInfo.dwProcessId);

    TraceSession traceSession;
    std::vector<InstalledHook> installedHooks;
    if (!PrepareTraceTarget(processInfo.hProcess, dllPath, options, &traceSession, &installedHooks)) {
        return FailSuspendedProcess(processInfo.hProcess, processInfo.hThread, "Trace setup failed");
    }
    fprintf(stderr, "[+] Setup done, resuming process...\n");

    if (ResumeThread(processInfo.hThread) == (DWORD)-1) {
        return FailSuspendedProcess(processInfo.hProcess, processInfo.hThread, "ResumeThread failed");
    }
    CloseHandle(processInfo.hThread);

    WaitForSingleObject(processInfo.hProcess, INFINITE);
    traceSession.Stop();

    DWORD exitCode = 1;
    if (!GetExitCodeProcess(processInfo.hProcess, &exitCode)) {
        fprintf(stderr, "[!] GetExitCodeProcess failed (%lu)\n", GetLastError());
        CloseHandle(processInfo.hProcess);
        return 1;
    }
    CloseHandle(processInfo.hProcess);
    fprintf(stderr, "[+] Target exited with code %lu\n", exitCode);
    return exitCode == 0 ? 0 : 1;
}
