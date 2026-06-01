#include "common.h"
#include "hook_manager.h"
#include "hook_registry.h"
#include "process_manager.h"
#include <iostream>
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
    printf("[!] %s\n", message);
    TerminateProcess(hProcess, 1);
    CloseHandle(hThread);
    CloseHandle(hProcess);
    return 1;
}

int main(int argc, char* argv[]) {
    std::wstring dllPath = GetSiblingPath(L"ntdlln.dll");
    std::vector<std::string> availableHooks = DiscoverHooks(dllPath.c_str());

    if (argc == 2 && strcmp(argv[1], "--list-hooks") == 0) {
        if (availableHooks.empty()) {
            printf("[!] No hooks discovered in ntdlln.dll\n");
            return 1;
        }
        PrintSupportedHooks(availableHooks);
        return 0;
    }

    if (argc < 3) {
        printf("Usage: hooknt.exe <target program> <list of NT functions to hook>\n");
        printf("       hooknt.exe --list-hooks\n");
        printf("Example: hooknt.exe notepad.exe NtCreateFile NtReadFile NtWriteFile\n");
        return 1;
    }

    if (availableHooks.empty()) {
        printf("[!] No hooks discovered in ntdlln.dll\n");
        return 1;
    }

    // Extract the target program and NT functions from command-line arguments
    std::string targetProgram = argv[1];
    std::wstring targetProgramW(targetProgram.begin(), targetProgram.end());

    std::vector<std::string> ntFunctionsToHook;
    for (int i = 2; i < argc; ++i) {
        if (!IsSupportedHook(availableHooks, argv[i])) {
            printf("[!] Unsupported hook: %s\n", argv[i]);
            printf("[!] Supported hooks:\n");
            PrintSupportedHooks(availableHooks);
            return 1;
        }
        ntFunctionsToHook.push_back(argv[i]);
    }

    printf("[+] HookNt - NT API Function Hooker\n");
    printf("[+] Target: %s\n", targetProgram.c_str());
    printf("[+] Functions to hook: ");
    for (const auto& func : ntFunctionsToHook) {
        printf("%s ", func.c_str());
    }
    printf("\n\n");

    // Create a new process, suspend it and get its PID
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    
    if (!CreateProcessW(
        targetProgramW.c_str(),
        NULL, NULL, NULL, TRUE, CREATE_SUSPENDED, NULL, NULL, &si, &pi)) {
        printf("[!] CreateProcess failed (%d)\n", GetLastError());
        return 1;
    }
    
    DWORD pid = pi.dwProcessId;
    HANDLE hProcess = pi.hProcess;
    HANDLE hThread = pi.hThread;
    printf("[+] Process created, PID: %d\n", pid);

    // Find ntdll.dll in the target process
    PVOID ntdllBase = FindNtdllBase(hProcess);
    if (!ntdllBase) {
        return FailSuspendedProcess(hProcess, hThread, "ntdll.dll not found");
    }
    printf("[+] Found ntdll.dll at 0x%p\n", ntdllBase);

    // Inject our DLL
    printf("[+] Injecting DLL\n");
    PVOID ntdllNBase = InjectDll(hProcess, dllPath.c_str());
    if (!ntdllNBase) {
        return FailSuspendedProcess(hProcess, hThread, "DLL injection failed");
    }
    printf("[+] DLL injected at 0x%p\n", ntdllNBase);

    // Hook each function
    bool allHooksSuccessful = true;
    for (const auto& functionName : ntFunctionsToHook) {
        if (!HookFunction(hProcess, ntdllBase, ntdllNBase, functionName.c_str())) {
            printf("[!] Failed to hook %s\n", functionName.c_str());
            allHooksSuccessful = false;
        }
    }

    if (!allHooksSuccessful) {
        return FailSuspendedProcess(hProcess, hThread, "Hook installation failed");
    }
    printf("[+] All hooks installed successfully\n");

    printf("[+] Setup done, resuming process...\n");
    
    // Resume the main thread
    if (ResumeThread(hThread) == (DWORD)-1) {
        return FailSuspendedProcess(hProcess, hThread, "ResumeThread failed");
    }
    CloseHandle(hThread);

    WaitForSingleObject(hProcess, INFINITE);
    DWORD exitCode = 1;
    if (!GetExitCodeProcess(hProcess, &exitCode)) {
        printf("[!] GetExitCodeProcess failed (%d)\n", GetLastError());
        CloseHandle(hProcess);
        return 1;
    }
    CloseHandle(hProcess);
    printf("[+] Target exited with code %lu\n", exitCode);
    
    return exitCode == 0 ? 0 : 1;
}
