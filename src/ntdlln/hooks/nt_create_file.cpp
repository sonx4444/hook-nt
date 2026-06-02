#include "nt_hook.h"
#include "trace_transport.h"

DEFINE_NT_HOOK(
    NtCreateFile,
    HANDLE* FileHandle,
    ACCESS_MASK DesiredAccess,
    OBJECT_ATTRIBUTES* ObjectAttributes,
    IO_STATUS_BLOCK* IoStatusBlock,
    PVOID AllocationSize,
    ULONG FileAttributes,
    ULONG ShareAccess,
    ULONG CreateDisposition,
    ULONG CreateOptions,
    PVOID EaBuffer,
    ULONG EaLength) {
    TraceEvent event;
    InitializeTraceEvent(&event, "NtCreateFile");
    AddTracePointer(&event, "file_handle", FileHandle);
    AddTraceUInt32(&event, "desired_access", DesiredAccess);
    AddTracePointer(&event, "object_attributes", ObjectAttributes);
    AddTracePointer(&event, "io_status_block", IoStatusBlock);
    AddTracePointer(&event, "allocation_size", AllocationSize);
    AddTraceUInt32(&event, "file_attributes", FileAttributes);
    AddTraceUInt32(&event, "share_access", ShareAccess);
    AddTraceUInt32(&event, "create_disposition", CreateDisposition);
    AddTraceUInt32(&event, "create_options", CreateOptions);
    AddTracePointer(&event, "ea_buffer", EaBuffer);
    AddTraceUInt32(&event, "ea_length", EaLength);

    NTSTATUS result = CALL_ORIGINAL(
        NtCreateFile,
        FileHandle,
        DesiredAccess,
        ObjectAttributes,
        IoStatusBlock,
        AllocationSize,
        FileAttributes,
        ShareAccess,
        CreateDisposition,
        CreateOptions,
        EaBuffer,
        EaLength);
    event.header.status = result;
    EmitTraceEvent(&event);
    return result;
}
