#pragma once

#include "cli_options.h"
#include "hook_manager.h"
#include "trace_session.h"
#include <map>
#include <set>
#include <string>
#include <vector>

class DebugSession {
public:
    DebugSession(
        const std::wstring& hookDllPath,
        const CommandLineOptions& options);

    int Run();
    int Attach();

private:
    struct ModuleRecord {
        PVOID base;
        std::wstring path;
        std::string name;
        uint64_t generation;
    };

    enum class ResolveResult {
        Resolved,
        Pending,
        Error,
    };

    bool InitializeDebuggee();
    int EventLoop();
    bool HandleCreateProcess(const CREATE_PROCESS_DEBUG_INFO& info);
    bool HandleLoadDll(const LOAD_DLL_DEBUG_INFO& info);
    void HandleUnloadDll(PVOID base);
    bool RegisterModule(PVOID base, HANDLE file);
    bool TryInstallPendingHooks();
    ResolveResult ResolveHookAddress(
        const HookDefinition& hook,
        PVOID* address,
        PVOID* sourceModuleBase,
        PVOID* targetModuleBase);
    ResolveResult ResolveExportAddress(
        const std::string& moduleName,
        const std::string& exportName,
        unsigned depth,
        std::set<std::string>* visited,
        PVOID* address,
        PVOID* targetModuleBase);
    const ModuleRecord* FindModule(const std::string& normalizedName) const;
    std::wstring GetModulePath(HANDLE file) const;
    bool CleanupRemote();
    bool IsCleanupQuiescent() const;
    bool IsInstrumentationAddress(DWORD64 instructionPointer) const;
    void CloseDebugEventHandles(const DEBUG_EVENT& event) const;

    std::wstring hookDllPath_;
    CommandLineOptions options_;
    HANDLE process_;
    DWORD processId_;
    PVOID hookImageBase_;
    SIZE_T hookImageSize_;
    TraceSession traceSession_;
    std::map<ULONG_PTR, ModuleRecord> modules_;
    std::vector<InstalledHook> installedHooks_;
    bool transportConfigured_;
    bool initialBreakpointSeen_;
    bool stopBreakRequested_;
    bool attached_;
    bool debugActive_;
    uint64_t moduleGeneration_;
};
