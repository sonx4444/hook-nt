#pragma once

#include "trace_protocol.h"

typedef NTSTATUS(NTAPI* TransportNtWriteFileProc)(
    HANDLE FileHandle,
    HANDLE Event,
    PIO_APC_ROUTINE ApcRoutine,
    PVOID ApcContext,
    PIO_STATUS_BLOCK IoStatusBlock,
    PVOID Buffer,
    ULONG Length,
    PLARGE_INTEGER ByteOffset,
    PULONG Key);

typedef NTSTATUS(NTAPI* TransportNtReadVirtualMemoryProc)(
    HANDLE ProcessHandle,
    PVOID BaseAddress,
    PVOID Buffer,
    ULONG NumberOfBytesToRead,
    PULONG NumberOfBytesRead);

extern "C" __declspec(dllexport) HANDLE TransportPipeHandle;
extern "C" __declspec(dllexport) PVOID TransportNtWriteFile;
extern "C" __declspec(dllexport) PVOID TransportNtReadVirtualMemory;
extern "C" __declspec(dllexport) volatile LONG TransportSequence;
extern "C" __declspec(dllexport) volatile LONG TransportDroppedEvents;

bool InitializeTraceEvent(TraceEvent* event, const char* apiName);
bool AddTracePointer(TraceEvent* event, const char* name, const void* value);
bool AddTraceUInt32(TraceEvent* event, const char* name, uint32_t value);
bool AddTraceUInt64(TraceEvent* event, const char* name, uint64_t value);
bool AddTraceStatus(TraceEvent* event, const char* name, NTSTATUS value);
bool AddTraceBufferPreview(TraceEvent* event, const char* name, const void* buffer, ULONG length);
bool EmitTraceEvent(TraceEvent* event);
