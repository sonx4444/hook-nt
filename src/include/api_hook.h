#pragma once

#include "api_hook_descriptor.h"
#include "common.h"
#include <intrin.h>

#pragma intrinsic(_InterlockedDecrement)
#pragma intrinsic(_InterlockedIncrement)

extern "C" __declspec(dllexport) volatile LONG ActiveHookCalls;

class HookCallGuard {
public:
    HookCallGuard() {
        _InterlockedIncrement(&ActiveHookCalls);
    }

    ~HookCallGuard() {
        _InterlockedDecrement(&ActiveHookCalls);
    }
};

#define DEFINE_API_HOOK(id, moduleName, exportName, returnType, callingConvention, ...) \
    extern "C" {                                                                \
    __declspec(dllexport) PVOID id##Trampoline = nullptr;                       \
    __declspec(dllexport) extern const ApiHookDescriptor ApiScopeHook_##id = {  \
        API_HOOK_DESCRIPTOR_MAGIC,                                               \
        API_HOOK_DESCRIPTOR_VERSION,                                             \
        sizeof(ApiHookDescriptor),                                               \
        moduleName,                                                              \
        exportName,                                                              \
        #id "Handler",                                                          \
        #id "Trampoline"};                                                      \
    }                                                                            \
    using id##_proc = returnType(callingConvention*)(__VA_ARGS__);               \
    extern "C" __declspec(dllexport) returnType callingConvention id##Handler(__VA_ARGS__)

#define CALL_ORIGINAL(id, ...) reinterpret_cast<id##_proc>(id##Trampoline)(__VA_ARGS__)
