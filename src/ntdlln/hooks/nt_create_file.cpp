#include "logger.h"
#include "nt_hook.h"

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
    printfN("\n[*] NtCreateFile\n");
    printfN("  \\FileHandle       : %p\n", FileHandle);
    printfN("  \\DesiredAccess    : %lu\n", DesiredAccess);
    printfN("  \\ObjectAttributes : %p\n", ObjectAttributes);
    printfN("  \\IoStatusBlock    : %p\n", IoStatusBlock);
    printfN("  \\AllocationSize   : %p\n", AllocationSize);
    printfN("  \\FileAttributes   : %lu\n", FileAttributes);
    printfN("  \\ShareAccess      : %lu\n", ShareAccess);
    printfN("  \\CreateDisposition: %lu\n", CreateDisposition);
    printfN("  \\CreateOptions    : %lu\n", CreateOptions);
    printfN("  \\EaBuffer         : %p\n", EaBuffer);
    printfN("  \\EaLength         : %lu\n", EaLength);

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
    printfN("  ---------------> 0x%08lX\n", result);
    return result;
}
