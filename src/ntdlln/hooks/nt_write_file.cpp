#include "logger.h"
#include "nt_hook.h"

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
    printfN("\n[*] NtWriteFile\n");
    printfN("  \\FileHandle   : %p\n", FileHandle);
    printfN("  \\Event        : %p\n", Event);
    printfN("  \\ApcRoutine   : %p\n", ApcRoutine);
    printfN("  \\ApcContext   : %p\n", ApcContext);
    printfN("  \\IoStatusBlock: %p\n", IoStatusBlock);
    LogBuffer("Buffer", Buffer, Length);
    printfN("  \\Length       : %lu\n", Length);
    printfN("  \\ByteOffset   : %p\n", ByteOffset);
    printfN("  \\Key          : %p\n", Key);

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
    printfN("  ---------------> 0x%08lX\n", result);
    return result;
}
