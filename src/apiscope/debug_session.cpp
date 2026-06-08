#include "debug_session.h"

#include "hook_registry.h"
#include "process_manager.h"
#include <algorithm>
#include <atomic>
#include <stdio.h>
#include <tlhelp32.h>

static std::atomic<bool> StopRequested(false);

static BOOL WINAPI ConsoleControlHandler(DWORD controlType) {
    if (controlType == CTRL_C_EVENT || controlType == CTRL_BREAK_EVENT) {
        StopRequested.store(true, std::memory_order_relaxed);
        return TRUE;
    }
    return FALSE;
}

static std::string NarrowAscii(const std::wstring& value) {
    std::string result;
    for (wchar_t character : value) {
        if (character > 0x7F) {
            return "";
        }
        result.push_back((char)character);
    }
    return result;
}

DebugSession::DebugSession(
    const std::wstring& hookDllPath,
    const CommandLineOptions& options)
    : hookDllPath_(hookDllPath),
      options_(options),
      process_(nullptr),
      processId_(0),
      hookImageBase_(nullptr),
      hookImageSize_(0),
      transportConfigured_(false),
      initialBreakpointSeen_(false),
      stopBreakRequested_(false),
      attached_(false),
      debugActive_(false),
      moduleGeneration_(0) {
}

int DebugSession::Run() {
    std::wstring commandLine = BuildWindowsCommandLine(options_.targetArguments);
    std::vector<WCHAR> commandLineBuffer(commandLine.begin(), commandLine.end());
    commandLineBuffer.push_back(L'\0');

    STARTUPINFOW startupInfo = {sizeof(startupInfo)};
    PROCESS_INFORMATION processInfo = {};
    if (!CreateProcessW(
            options_.targetArguments[0].c_str(),
            commandLineBuffer.data(),
            nullptr,
            nullptr,
            FALSE,
            DEBUG_ONLY_THIS_PROCESS,
            nullptr,
            nullptr,
            &startupInfo,
            &processInfo)) {
        fprintf(stderr, "[!] CreateProcessW failed (%lu)\n", GetLastError());
        return 1;
    }

    process_ = processInfo.hProcess;
    processId_ = processInfo.dwProcessId;
    CloseHandle(processInfo.hThread);
    fprintf(stderr, "[+] Process created under debugger, PID: %lu\n", processId_);
    return EventLoop();
}

int DebugSession::Attach() {
    DWORD access = PROCESS_QUERY_INFORMATION |
        PROCESS_CREATE_THREAD |
        PROCESS_VM_OPERATION |
        PROCESS_VM_READ |
        PROCESS_VM_WRITE |
        PROCESS_DUP_HANDLE |
        PROCESS_SUSPEND_RESUME |
        SYNCHRONIZE;
    process_ = OpenProcess(access, FALSE, options_.targetProcessId);
    if (!process_) {
        fprintf(stderr, "[!] OpenProcess failed (%lu)\n", GetLastError());
        return 1;
    }
    processId_ = options_.targetProcessId;
    attached_ = true;
    if (!DebugActiveProcess(processId_)) {
        fprintf(stderr, "[!] DebugActiveProcess failed (%lu)\n", GetLastError());
        CloseHandle(process_);
        process_ = nullptr;
        return 1;
    }
    fprintf(stderr, "[+] Attached debugger to PID: %lu\n", processId_);
    return EventLoop();
}

bool DebugSession::InitializeDebuggee() {
    if (!DebugSetProcessKillOnExit(FALSE)) {
        fprintf(stderr, "[!] DebugSetProcessKillOnExit failed (%lu)\n", GetLastError());
        return false;
    }
    debugActive_ = true;

    fprintf(stderr, "[+] Mapping apiscope-hooks.dll\n");
    hookImageBase_ = InjectDll(process_, hookDllPath_.c_str());
    if (!hookImageBase_) {
        return false;
    }
    hookImageSize_ = GetRemoteImageSize(process_, (PBYTE)hookImageBase_);
    if (!hookImageSize_) {
        fprintf(stderr, "[!] Failed to read mapped hook image size\n");
        return false;
    }
    fprintf(stderr, "[+] Hook image mapped at 0x%p\n", hookImageBase_);
    if (!traceSession_.Start(process_, hookImageBase_, options_.output)) {
        fprintf(stderr, "[!] Trace transport setup failed\n");
        return false;
    }
    return true;
}

std::wstring DebugSession::GetModulePath(HANDLE file) const {
    if (!file || file == INVALID_HANDLE_VALUE) {
        return L"";
    }
    std::vector<wchar_t> path(32768);
    DWORD length = GetFinalPathNameByHandleW(
        file,
        path.data(),
        (DWORD)path.size(),
        FILE_NAME_NORMALIZED);
    if (!length || length >= path.size()) {
        return L"";
    }
    return std::wstring(path.data(), length);
}

bool DebugSession::RegisterModule(PVOID base, HANDLE file) {
    std::wstring path = GetModulePath(file);
    std::string name = NormalizeModuleName(NarrowAscii(path));
    if (name.empty()) {
        name = NormalizeModuleName(GetRemoteModuleName(process_, (PBYTE)base));
    }
    if (name.empty()) {
        fprintf(stderr, "[!] Could not identify module at 0x%p\n", base);
        return true;
    }

    ModuleRecord module = {};
    module.base = base;
    module.path = path;
    module.name = name;
    module.generation = ++moduleGeneration_;
    modules_[(ULONG_PTR)base] = module;
    fprintf(stderr, "[+] Module loaded: %s at 0x%p\n", name.c_str(), base);

    if (name == "ntdll.dll" && !transportConfigured_) {
        if (!traceSession_.ConfigureTransport(process_, base, hookImageBase_)) {
            fprintf(stderr, "[!] Failed to configure transport bypasses\n");
            return false;
        }
        transportConfigured_ = true;
    }
    return !transportConfigured_ || TryInstallPendingHooks();
}

bool DebugSession::HandleCreateProcess(const CREATE_PROCESS_DEBUG_INFO& info) {
    if (!hookImageBase_ && !InitializeDebuggee()) {
        return false;
    }
    return RegisterModule(info.lpBaseOfImage, info.hFile);
}

bool DebugSession::HandleLoadDll(const LOAD_DLL_DEBUG_INFO& info) {
    return RegisterModule(info.lpBaseOfDll, info.hFile);
}

void DebugSession::HandleUnloadDll(PVOID base) {
    auto module = modules_.find((ULONG_PTR)base);
    if (module != modules_.end()) {
        fprintf(stderr, "[+] Module unloaded: %s\n", module->second.name.c_str());
    }

    for (auto hook = installedHooks_.begin(); hook != installedHooks_.end();) {
        if (hook->targetModuleBase == base) {
            if (!ReleaseUnloadedHook(process_, *hook)) {
                fprintf(stderr, "[!] Failed to release unloaded hook %s\n", hook->canonicalName.c_str());
            }
            hook = installedHooks_.erase(hook);
        } else if (hook->sourceModuleBase == base) {
            if (!UnhookFunction(process_, *hook)) {
                fprintf(stderr, "[!] Failed to remove hook %s\n", hook->canonicalName.c_str());
            }
            hook = installedHooks_.erase(hook);
        } else {
            ++hook;
        }
    }
    modules_.erase((ULONG_PTR)base);
}

const DebugSession::ModuleRecord* DebugSession::FindModule(
    const std::string& normalizedName) const {
    const ModuleRecord* newest = nullptr;
    for (const auto& entry : modules_) {
        if (entry.second.name == normalizedName &&
            (!newest || entry.second.generation > newest->generation)) {
            newest = &entry.second;
        }
    }
    return newest;
}

DebugSession::ResolveResult DebugSession::ResolveExportAddress(
    const std::string& moduleName,
    const std::string& exportName,
    unsigned depth,
    std::set<std::string>* visited,
    PVOID* address,
    PVOID* targetModuleBase) {
    if (depth >= 8) {
        fprintf(stderr, "[!] Forwarder chain is too deep\n");
        return ResolveResult::Error;
    }
    std::string normalizedModule = NormalizeModuleName(moduleName);
    if (normalizedModule.find('.') == std::string::npos) {
        normalizedModule += ".dll";
    }
    std::string identity = normalizedModule + "!" + exportName;
    if (!visited->insert(identity).second) {
        fprintf(stderr, "[!] Forwarder loop detected at %s\n", identity.c_str());
        return ResolveResult::Error;
    }

    const ModuleRecord* module = FindModule(normalizedModule);
    if (!module) {
        return ResolveResult::Pending;
    }

    std::optional<RemoteExport> remoteExport = GetRemoteExport(
        process_,
        (PBYTE)module->base,
        exportName.c_str());
    if (!remoteExport) {
        fprintf(stderr, "[!] Export not found: %s\n", identity.c_str());
        return ResolveResult::Error;
    }
    if (remoteExport->address) {
        *address = remoteExport->address;
        *targetModuleBase = module->base;
        return ResolveResult::Resolved;
    }

    size_t separator = remoteExport->forwarder.find_last_of('.');
    if (separator == std::string::npos ||
        separator == 0 ||
        separator + 1 >= remoteExport->forwarder.size() ||
        remoteExport->forwarder[separator + 1] == '#') {
        fprintf(stderr, "[!] Unsupported export forwarder: %s\n", remoteExport->forwarder.c_str());
        return ResolveResult::Error;
    }
    return ResolveExportAddress(
        remoteExport->forwarder.substr(0, separator),
        remoteExport->forwarder.substr(separator + 1),
        depth + 1,
        visited,
        address,
        targetModuleBase);
}

DebugSession::ResolveResult DebugSession::ResolveHookAddress(
    const HookDefinition& hook,
    PVOID* address,
    PVOID* sourceModuleBase,
    PVOID* targetModuleBase) {
    const ModuleRecord* source = FindModule(hook.moduleName);
    if (!source) {
        return ResolveResult::Pending;
    }
    *sourceModuleBase = source->base;
    std::set<std::string> visited;
    return ResolveExportAddress(
        hook.moduleName,
        hook.exportName,
        0,
        &visited,
        address,
        targetModuleBase);
}

bool DebugSession::TryInstallPendingHooks() {
    for (const HookDefinition& definition : options_.hooks) {
        auto installed = std::find_if(
            installedHooks_.begin(),
            installedHooks_.end(),
            [&definition](const InstalledHook& hook) {
                return hook.canonicalName == definition.canonicalName;
            });
        if (installed != installedHooks_.end()) {
            continue;
        }

        PVOID address = nullptr;
        PVOID sourceModuleBase = nullptr;
        PVOID targetModuleBase = nullptr;
        ResolveResult result = ResolveHookAddress(
            definition,
            &address,
            &sourceModuleBase,
            &targetModuleBase);
        if (result == ResolveResult::Pending) {
            continue;
        }
        if (result == ResolveResult::Error) {
            return false;
        }
        for (const InstalledHook& hook : installedHooks_) {
            if (hook.originalFunction == address) {
                fprintf(
                    stderr,
                    "[!] Hooks %s and %s resolve to the same address\n",
                    hook.canonicalName.c_str(),
                    definition.canonicalName.c_str());
                return false;
            }
        }

        InstalledHook hook;
        if (!HookFunction(process_, address, hookImageBase_, definition, &hook)) {
            return false;
        }
        hook.sourceModuleBase = sourceModuleBase;
        hook.targetModuleBase = targetModuleBase;
        installedHooks_.push_back(hook);
        fprintf(stderr, "[+] Hook installed: %s\n", definition.canonicalName.c_str());
    }
    return true;
}

bool DebugSession::CleanupRemote() {
    bool cleaned = true;
    for (auto hook = installedHooks_.rbegin(); hook != installedHooks_.rend(); ++hook) {
        cleaned = UnhookFunction(process_, *hook) && cleaned;
    }
    installedHooks_.clear();
    if (hookImageBase_) {
        cleaned = traceSession_.CleanupRemote(process_, hookImageBase_) && cleaned;
    }
    traceSession_.Stop();
    if (hookImageBase_) {
        cleaned = VirtualFreeEx(process_, hookImageBase_, 0, MEM_RELEASE) != FALSE && cleaned;
        hookImageBase_ = nullptr;
    }
    return cleaned;
}

bool DebugSession::IsInstrumentationAddress(DWORD64 instructionPointer) const {
    DWORD64 hookBase = (DWORD64)hookImageBase_;
    if (hookBase && instructionPointer >= hookBase &&
        instructionPointer < hookBase + hookImageSize_) {
        return true;
    }
    for (const InstalledHook& hook : installedHooks_) {
        DWORD64 original = (DWORD64)hook.originalFunction;
        DWORD64 trampoline = (DWORD64)hook.trampolineAddress;
        if ((instructionPointer >= original &&
             instructionPointer < original + hook.originalBytes.size()) ||
            (instructionPointer >= trampoline &&
             instructionPointer < trampoline + 1024)) {
            return true;
        }
    }
    return false;
}

bool DebugSession::IsCleanupQuiescent() const {
    if (!hookImageBase_) {
        return true;
    }
    std::optional<RemoteExport> counterExport = GetRemoteExport(
        process_,
        (PBYTE)hookImageBase_,
        "ActiveHookCalls");
    LONG activeCalls = 0;
    if (!counterExport ||
        !counterExport->address ||
        !ReadProcessMemory(
            process_,
            counterExport->address,
            &activeCalls,
            sizeof(activeCalls),
            nullptr) ||
        activeCalls != 0) {
        return false;
    }

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return false;
    }
    THREADENTRY32 entry = {};
    entry.dwSize = sizeof(entry);
    bool quiescent = true;
    if (Thread32First(snapshot, &entry)) {
        do {
            if (entry.th32OwnerProcessID != processId_) {
                continue;
            }
            HANDLE thread = OpenThread(
                THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
                FALSE,
                entry.th32ThreadID);
            if (!thread) {
                if (GetLastError() == ERROR_INVALID_PARAMETER) {
                    continue;
                }
                quiescent = false;
                break;
            }
            CONTEXT context = {};
            context.ContextFlags = CONTEXT_CONTROL;
            bool gotContext = GetThreadContext(thread, &context) != FALSE;
            bool inInstrumentation =
                gotContext && IsInstrumentationAddress(context.Rip);
            CloseHandle(thread);
            if (!gotContext || inInstrumentation) {
                quiescent = false;
                break;
            }
        } while (Thread32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return quiescent;
}

void DebugSession::CloseDebugEventHandles(const DEBUG_EVENT& event) const {
    if (event.dwDebugEventCode == CREATE_PROCESS_DEBUG_EVENT) {
        if (event.u.CreateProcessInfo.hFile) {
            CloseHandle(event.u.CreateProcessInfo.hFile);
        }
        if (event.u.CreateProcessInfo.hThread) {
            CloseHandle(event.u.CreateProcessInfo.hThread);
        }
        if (event.u.CreateProcessInfo.hProcess) {
            CloseHandle(event.u.CreateProcessInfo.hProcess);
        }
    } else if (event.dwDebugEventCode == CREATE_THREAD_DEBUG_EVENT) {
        if (event.u.CreateThread.hThread) {
            CloseHandle(event.u.CreateThread.hThread);
        }
    } else if (event.dwDebugEventCode == LOAD_DLL_DEBUG_EVENT &&
               event.u.LoadDll.hFile) {
        CloseHandle(event.u.LoadDll.hFile);
    }
}

int DebugSession::EventLoop() {
    StopRequested.store(false, std::memory_order_relaxed);
    SetConsoleCtrlHandler(ConsoleControlHandler, TRUE);
    int result = 1;
    bool finished = false;
    bool naturalExit = false;
    bool retryCleanStop = false;
    DWORD targetExitCode = 0;

    while (!finished) {
        if (StopRequested.load(std::memory_order_relaxed) && !stopBreakRequested_) {
            if (!DebugBreakProcess(process_)) {
                fprintf(stderr, "[!] DebugBreakProcess failed (%lu)\n", GetLastError());
                break;
            }
            stopBreakRequested_ = true;
        }

        DEBUG_EVENT event = {};
        if (!WaitForDebugEventEx(&event, 100)) {
            if (GetLastError() == ERROR_SEM_TIMEOUT) {
                continue;
            }
            fprintf(stderr, "[!] WaitForDebugEventEx failed (%lu)\n", GetLastError());
            break;
        }

        DWORD continueStatus = DBG_CONTINUE;
        bool eventOk = true;
        bool cleanStop = false;
        switch (event.dwDebugEventCode) {
        case CREATE_PROCESS_DEBUG_EVENT:
            eventOk = HandleCreateProcess(event.u.CreateProcessInfo);
            break;
        case LOAD_DLL_DEBUG_EVENT:
            eventOk = HandleLoadDll(event.u.LoadDll);
            break;
        case UNLOAD_DLL_DEBUG_EVENT:
            HandleUnloadDll(event.u.UnloadDll.lpBaseOfDll);
            break;
        case EXCEPTION_DEBUG_EVENT:
            if (event.u.Exception.ExceptionRecord.ExceptionCode == EXCEPTION_BREAKPOINT) {
                if (stopBreakRequested_) {
                    if (IsCleanupQuiescent()) {
                        cleanStop = true;
                    } else {
                        retryCleanStop = true;
                    }
                } else if (!initialBreakpointSeen_) {
                    initialBreakpointSeen_ = true;
                } else {
                    continueStatus = DBG_EXCEPTION_NOT_HANDLED;
                }
            } else {
                continueStatus = DBG_EXCEPTION_NOT_HANDLED;
            }
            break;
        case EXIT_PROCESS_DEBUG_EVENT:
            targetExitCode = event.u.ExitProcess.dwExitCode;
            result = targetExitCode == 0 ? 0 : 1;
            naturalExit = true;
            traceSession_.Drain();
            finished = true;
            break;
        default:
            break;
        }

        if (!eventOk || cleanStop) {
            if (!CleanupRemote()) {
                fprintf(stderr, "[!] Remote cleanup was incomplete\n");
            }
        }

        CloseDebugEventHandles(event);
        if (!ContinueDebugEvent(event.dwProcessId, event.dwThreadId, continueStatus)) {
            fprintf(stderr, "[!] ContinueDebugEvent failed (%lu)\n", GetLastError());
            break;
        }

        if (retryCleanStop) {
            retryCleanStop = false;
            Sleep(10);
            if (!DebugBreakProcess(process_)) {
                fprintf(stderr, "[!] DebugBreakProcess retry failed (%lu)\n", GetLastError());
                break;
            }
        }

        if (!eventOk || cleanStop) {
            if (!DebugActiveProcessStop(processId_)) {
                fprintf(stderr, "[!] DebugActiveProcessStop failed (%lu)\n", GetLastError());
            }
            debugActive_ = false;
            result = cleanStop ? 0 : 1;
            finished = true;
        }
    }

    SetConsoleCtrlHandler(ConsoleControlHandler, FALSE);
    if (debugActive_ && !finished) {
        DebugActiveProcessStop(processId_);
    }
    if (naturalExit) {
        fprintf(
            stderr,
            "[+] Target process exited with status %lu (0x%08lX)\n",
            targetExitCode,
            targetExitCode);
    } else {
        traceSession_.Stop();
    }
    if (process_) {
        CloseHandle(process_);
        process_ = nullptr;
    }
    return result;
}
