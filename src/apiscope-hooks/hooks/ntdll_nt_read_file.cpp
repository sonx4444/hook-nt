#include "api_hook.h"
#include "trace_transport.h"

DEFINE_API_HOOK(
    NtReadFile,
    "ntdll.dll",
    "NtReadFile",
    NTSTATUS,
    NTAPI,
    HANDLE FileHandle,
    HANDLE Event,
    PIO_APC_ROUTINE ApcRoutine,
    PVOID ApcContext,
    PIO_STATUS_BLOCK IoStatusBlock,
    PVOID Buffer,
    ULONG Length,
    PLARGE_INTEGER ByteOffset,
    PULONG Key) {
    HookCallGuard hookCall;
    TraceEvent event;
    InitializeTraceEvent(&event, "ntdll.dll", "NtReadFile");
    AddTracePointer(&event, "file_handle", FileHandle);
    AddTracePointer(&event, "event", Event);
    AddTracePointer(&event, "apc_routine", ApcRoutine);
    AddTracePointer(&event, "apc_context", ApcContext);
    AddTracePointer(&event, "io_status_block", IoStatusBlock);
    AddTracePointer(&event, "buffer_address", Buffer);
    AddTraceUInt32(&event, "length", Length);
    AddTracePointer(&event, "byte_offset", ByteOffset);
    AddTracePointer(&event, "key", Key);

    NTSTATUS result = CALL_ORIGINAL(
        NtReadFile,
        FileHandle,
        Event,
        ApcRoutine,
        ApcContext,
        IoStatusBlock,
        Buffer,
        Length,
        ByteOffset,
        Key);
    if (NT_SUCCESS(result) && IoStatusBlock) {
        size_t bytesRead = IoStatusBlock->Information < Length ? IoStatusBlock->Information : Length;
        AddTraceBufferPreview(&event, "buffer", Buffer, (ULONG)bytesRead);
    }
    AddTraceStatus(&event, "result", result);
    EmitTraceEvent(&event);
    return result;
}
