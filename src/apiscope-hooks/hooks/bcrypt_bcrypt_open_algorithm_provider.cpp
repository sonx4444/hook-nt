#include "api_hook.h"
#include "trace_transport.h"

DEFINE_API_HOOK(
    BCryptOpenAlgorithmProvider,
    "bcrypt.dll",
    "BCryptOpenAlgorithmProvider",
    NTSTATUS,
    WINAPI,
    PVOID* Algorithm,
    const wchar_t* AlgorithmId,
    const wchar_t* Implementation,
    ULONG Flags) {
    HookCallGuard hookCall;
    TraceEvent event;
    InitializeTraceEvent(&event, "bcrypt.dll", "BCryptOpenAlgorithmProvider");
    AddTracePointer(&event, "algorithm", Algorithm);
    AddTracePointer(&event, "algorithm_id", AlgorithmId);
    AddTracePointer(&event, "implementation", Implementation);
    AddTraceUInt32(&event, "flags", Flags);

    NTSTATUS result = CALL_ORIGINAL(
        BCryptOpenAlgorithmProvider,
        Algorithm,
        AlgorithmId,
        Implementation,
        Flags);
    AddTraceStatus(&event, "result", result);
    EmitTraceEvent(&event);
    return result;
}
