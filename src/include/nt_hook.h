#pragma once

#include "common.h"

#define DEFINE_NT_HOOK(name, ...)                                      \
    extern "C" {                                                       \
    __declspec(dllexport) PVOID name##Trampoline = nullptr;             \
    }                                                                  \
    using name##_proc = NTSTATUS(NTAPI*)(__VA_ARGS__);                  \
    extern "C" __declspec(dllexport) NTSTATUS NTAPI name##N(__VA_ARGS__)

#define CALL_ORIGINAL(name, ...) reinterpret_cast<name##_proc>(name##Trampoline)(__VA_ARGS__)
