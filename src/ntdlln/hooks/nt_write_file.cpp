#include "nt_hook.h"
#include "trace_transport.h"

DEFINE_NT_HOOK(
    NtWriteFile,
    HANDLE FileHandle,
    HANDLE Event,
    PIO_APC_ROUTINE ApcRoutine,
    PVOID ApcContext,
    PIO_STATUS_BLOCK IoStatusBlock,
    PVOID Buffer,
    ULONG Length,
    PLARGE_INTEGER ByteOffset,
    PULONG Key) {
    TraceEvent event;
    InitializeTraceEvent(&event, "NtWriteFile");
    AddTracePointer(&event, "file_handle", FileHandle);
    AddTracePointer(&event, "event", Event);
    AddTracePointer(&event, "apc_routine", ApcRoutine);
    AddTracePointer(&event, "apc_context", ApcContext);
    AddTracePointer(&event, "io_status_block", IoStatusBlock);
    AddTracePointer(&event, "buffer_address", Buffer);
    AddTraceUInt32(&event, "length", Length);
    AddTracePointer(&event, "byte_offset", ByteOffset);
    AddTracePointer(&event, "key", Key);
    AddTraceBufferPreview(&event, "buffer", Buffer, Length);

    NTSTATUS result = CALL_ORIGINAL(
        NtWriteFile,
        FileHandle,
        Event,
        ApcRoutine,
        ApcContext,
        IoStatusBlock,
        Buffer,
        Length,
        ByteOffset,
        Key);
    event.header.status = result;
    EmitTraceEvent(&event);
    return result;
}
