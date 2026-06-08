#pragma once

#include "trace_protocol.h"
#include "trace_ring.h"

typedef NTSTATUS(NTAPI* TransportNtReadVirtualMemoryProc)(
    HANDLE ProcessHandle,
    PVOID BaseAddress,
    PVOID Buffer,
    ULONG NumberOfBytesToRead,
    PULONG NumberOfBytesRead);

typedef NTSTATUS(NTAPI* TransportNtSetEventProc)(
    HANDLE EventHandle,
    PLONG PreviousState);

extern "C" __declspec(dllexport) TraceRing* TransportRing;
extern "C" __declspec(dllexport) HANDLE TransportWakeEvent;
extern "C" __declspec(dllexport) PVOID TransportNtSetEvent;
extern "C" __declspec(dllexport) PVOID TransportNtReadVirtualMemory;
extern "C" __declspec(dllexport) volatile LONG ActiveHookCalls;

bool InitializeTraceEvent(TraceEvent* event, const char* moduleName, const char* apiName);
bool AddTracePointer(TraceEvent* event, const char* name, const void* value);
bool AddTraceUInt32(TraceEvent* event, const char* name, uint32_t value);
bool AddTraceUInt64(TraceEvent* event, const char* name, uint64_t value);
bool AddTraceInt32(TraceEvent* event, const char* name, int32_t value);
bool AddTraceInt64(TraceEvent* event, const char* name, int64_t value);
bool AddTraceBoolean(TraceEvent* event, const char* name, bool value);
bool AddTraceStatus(TraceEvent* event, const char* name, NTSTATUS value);
bool AddTraceBufferPreview(TraceEvent* event, const char* name, const void* buffer, ULONG length);
bool EmitTraceEvent(TraceEvent* event);
