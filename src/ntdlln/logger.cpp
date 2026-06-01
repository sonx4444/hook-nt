#include "logger.h"
#include "module_resolver.h"
#include "function_resolver.h"

// Global variables
static vprintf_proc gp_vprintf = NULL;
static const size_t MAX_LOGGED_BUFFER_BYTES = 64;

bool TryGetVPrintfFromMsvcrt() {
    PVOID msvcrtBase = GetModuleHandleN(L"C:\\Windows\\System32\\msvcrt.dll");
    if (!msvcrtBase) {
        return false;
    }

    PVOID p_vprintf = GetProcAddressN(msvcrtBase, "vprintf");
    if (p_vprintf) {
        gp_vprintf = (vprintf_proc)p_vprintf;
        return true;
    }

    return false;
}

bool TryLoadMsvcrtAndGetVPrintf() {
    // Resolve from KernelBase directly. Kernel32 commonly exposes a forwarded
    // export, while this minimal resolver intentionally handles concrete RVAs.
    PVOID kernelBase = GetModuleHandleN(L"C:\\Windows\\System32\\KernelBase.dll");
    if (!kernelBase) {
        return false;
    }

    PVOID p_LoadLibraryA = GetProcAddressN(kernelBase, "LoadLibraryA");
    if (!p_LoadLibraryA) {
        return false;
    }

    HMODULE msvcrt = ((LoadLibraryA_proc)p_LoadLibraryA)("msvcrt.dll");
    if (!msvcrt) {
        return false;
    }

    PVOID p_vprintf = GetProcAddressN((PVOID)msvcrt, "vprintf");
    if (p_vprintf) {
        gp_vprintf = (vprintf_proc)p_vprintf;
        return true;
    }

    return false;
}

bool InitializeVPrintf() {
    if (gp_vprintf) {
        return true;
    }

    // Try to get vprintf from msvcrt.dll
    if (TryGetVPrintfFromMsvcrt()) {
        return true;
    }

    // Try to load msvcrt.dll and get vprintf
    if (TryLoadMsvcrtAndGetVPrintf()) {
        return true;
    }

    return false;
}

int printfN(const char* format, ...) {
    if (!InitializeVPrintf()) {
        return 0;
    }

    va_list args;
    va_start(args, format);
    int result = gp_vprintf(format, args);
    va_end(args);
    return result;
}

void HexDump(const char* prefix, const void* data, size_t size) {
    size_t displayedSize = size < MAX_LOGGED_BUFFER_BYTES ? size : MAX_LOGGED_BUFFER_BYTES;
    printfN("  \\%s (hexa): ", prefix);
    for (size_t i = 0; i < displayedSize; i++) {
        printfN("%02X ", ((const unsigned char*)data)[i]);
    }
    if (displayedSize < size) {
        printfN("...");
    }
    printfN("\n");
}

void LogBuffer(const char* prefix, const void* data, size_t size) {
    if (!data) {
        printfN("  \\%s: <null>\n", prefix);
        return;
    }

    size_t displayedSize = size < MAX_LOGGED_BUFFER_BYTES ? size : MAX_LOGGED_BUFFER_BYTES;
    __try {
        HexDump(prefix, data, size);
        printfN("  \\%s (text): ", prefix);
        for (size_t i = 0; i < displayedSize; i++) {
            unsigned char value = ((const unsigned char*)data)[i];
            printfN("%c", value >= 32 && value <= 126 ? value : '.');
        }
        if (displayedSize < size) {
            printfN("...");
        }
        printfN("\n");
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        printfN("  \\%s: <unavailable>\n", prefix);
    }
}
