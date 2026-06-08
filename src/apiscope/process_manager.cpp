#include "process_manager.h"
#include "memory_utils.h"
#include <stdio.h>

static bool IsRangeWithin(SIZE_T offset, SIZE_T length, SIZE_T bufferSize) {
    return offset <= bufferSize && length <= bufferSize - offset;
}

static DWORD GetMemoryProtection(DWORD characteristics);
static PVOID ReflectiveDLLInject(HANDLE hProcess, PBYTE lpDllBuffer, SIZE_T dllFileSize);

PVOID InjectDll(HANDLE hProcess, const wchar_t* dllPath) {
    // Load the DLL into memory
    HANDLE hFile = CreateFileW(dllPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "[!] CreateFileW failed: %d\n", GetLastError());
        return nullptr;
    }

    DWORD dwDllSize = GetFileSize(hFile, NULL);
    if (dwDllSize == INVALID_FILE_SIZE || dwDllSize == 0) {
        fprintf(stderr, "[!] GetFileSize failed: %d\n", GetLastError());
        CloseHandle(hFile);
        return nullptr;
    }

    LPVOID lpDllBuffer = VirtualAlloc(NULL, dwDllSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!lpDllBuffer) {
        fprintf(stderr, "[!] VirtualAlloc failed: %d\n", GetLastError());
        CloseHandle(hFile);
        return nullptr;
    }

    DWORD bytesRead = 0;
    if (!ReadFile(hFile, lpDllBuffer, dwDllSize, &bytesRead, NULL) || bytesRead != dwDllSize) {
        fprintf(stderr, "[!] ReadFile failed: %d\n", GetLastError());
        VirtualFree(lpDllBuffer, 0, MEM_RELEASE);
        CloseHandle(hFile);
        return nullptr;
    }

    CloseHandle(hFile);

    PVOID dllBase = ReflectiveDLLInject(hProcess, (PBYTE)lpDllBuffer, dwDllSize);
    VirtualFree(lpDllBuffer, 0, MEM_RELEASE);
    return dllBase;
}

std::optional<RemoteExport> GetRemoteExport(
    HANDLE hProcess,
    PBYTE dllBase,
    const char* functionName) {
    RemoteExport remoteExport = {};
    // Read the DOS header from the remote process
    IMAGE_DOS_HEADER dosHeader;
    if (!ReadProcessMemory(hProcess, dllBase, &dosHeader, sizeof(dosHeader), nullptr)) {
        fprintf(stderr, "[!] Failed to read DOS header. Error: %lu\n", GetLastError());
        return std::nullopt;
    }

    // Verify the DOS signature
    if (dosHeader.e_magic != IMAGE_DOS_SIGNATURE) {
        fprintf(stderr, "[!] Invalid DOS signature.\n");
        return std::nullopt;
    }

    // Read the NT headers from the remote process
    IMAGE_NT_HEADERS ntHeaders;
    if (!ReadProcessMemory(hProcess, (BYTE*)dllBase + dosHeader.e_lfanew, &ntHeaders, sizeof(ntHeaders), nullptr)) {
        fprintf(stderr, "[!] Failed to read NT headers. Error: %lu\n", GetLastError());
        return std::nullopt;
    }

    // Verify the NT signature
    if (ntHeaders.Signature != IMAGE_NT_SIGNATURE) {
        fprintf(stderr, "[!] Invalid NT signature.\n");
        return std::nullopt;
    }

    // Get the export directory information
    IMAGE_DATA_DIRECTORY exportDataDir = ntHeaders.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (exportDataDir.VirtualAddress == 0) {
        fprintf(stderr, "[!] No export directory found.\n");
        return std::nullopt;
    }

    // Read the export directory from the remote process
    IMAGE_EXPORT_DIRECTORY exportDir;
    if (!ReadProcessMemory(hProcess, (BYTE*)dllBase + exportDataDir.VirtualAddress, &exportDir, sizeof(exportDir), nullptr)) {
        fprintf(stderr, "[!] Failed to read export directory. Error: %lu\n", GetLastError());
        return std::nullopt;
    }

    // Read the array of function addresses, names, and ordinals
    PDWORD addressOfFunctions = new DWORD[exportDir.NumberOfFunctions];
    PDWORD addressOfNames = new DWORD[exportDir.NumberOfNames];
    PWORD addressOfNameOrdinals = new WORD[exportDir.NumberOfNames];

    if (!ReadProcessMemory(hProcess, (BYTE*)dllBase + exportDir.AddressOfFunctions, addressOfFunctions, exportDir.NumberOfFunctions * sizeof(DWORD), nullptr) ||
        !ReadProcessMemory(hProcess, (BYTE*)dllBase + exportDir.AddressOfNames, addressOfNames, exportDir.NumberOfNames * sizeof(DWORD), nullptr) ||
        !ReadProcessMemory(hProcess, (BYTE*)dllBase + exportDir.AddressOfNameOrdinals, addressOfNameOrdinals, exportDir.NumberOfNames * sizeof(WORD), nullptr)) {
        fprintf(stderr, "[!] Failed to read export tables. Error: %lu\n", GetLastError());
        delete[] addressOfFunctions;
        delete[] addressOfNames;
        delete[] addressOfNameOrdinals;
        return std::nullopt;
    }

    // Iterate over all named exports
    for (DWORD i = 0; i < exportDir.NumberOfNames; i++) {
        // Read the function name from the remote process
        char currentFunctionName[256];
        if (!ReadProcessMemory(hProcess, (BYTE*)dllBase + addressOfNames[i], currentFunctionName, sizeof(currentFunctionName), nullptr)) {
            fprintf(stderr, "[!] Failed to read function name. Error: %lu\n", GetLastError());
            continue;
        }
        currentFunctionName[sizeof(currentFunctionName) - 1] = '\0';

        // Compare the function name with the target name
        if (CustomStrCmp(currentFunctionName, functionName) == 0) {
            // Get the ordinal corresponding to this name
            WORD ordinal = addressOfNameOrdinals[i];
            if (ordinal >= exportDir.NumberOfFunctions) {
                fprintf(stderr, "[!] Invalid export ordinal.\n");
                break;
            }

            // Get the function's RVA and compute its absolute address
            DWORD functionRVA = addressOfFunctions[ordinal];
            if (functionRVA >= exportDataDir.VirtualAddress &&
                functionRVA < exportDataDir.VirtualAddress + exportDataDir.Size) {
                char forwarder[256];
                if (!ReadProcessMemory(
                        hProcess,
                        dllBase + functionRVA,
                        forwarder,
                        sizeof(forwarder),
                        nullptr)) {
                    delete[] addressOfFunctions;
                    delete[] addressOfNames;
                    delete[] addressOfNameOrdinals;
                    return std::nullopt;
                }
                forwarder[sizeof(forwarder) - 1] = '\0';
                remoteExport.forwarder = forwarder;
            } else {
                remoteExport.address = dllBase + functionRVA;
            }

            // Clean up
            delete[] addressOfFunctions;
            delete[] addressOfNames;
            delete[] addressOfNameOrdinals;

            return remoteExport;
        }
    }

    // Clean up
    delete[] addressOfFunctions;
    delete[] addressOfNames;
    delete[] addressOfNameOrdinals;

    return std::nullopt;
}

std::string GetRemoteModuleName(HANDLE hProcess, PBYTE dllBase) {
    IMAGE_DOS_HEADER dosHeader;
    IMAGE_NT_HEADERS ntHeaders;
    if (!ReadProcessMemory(hProcess, dllBase, &dosHeader, sizeof(dosHeader), nullptr) ||
        dosHeader.e_magic != IMAGE_DOS_SIGNATURE ||
        !ReadProcessMemory(
            hProcess,
            dllBase + dosHeader.e_lfanew,
            &ntHeaders,
            sizeof(ntHeaders),
            nullptr) ||
        ntHeaders.Signature != IMAGE_NT_SIGNATURE) {
        return "";
    }

    IMAGE_DATA_DIRECTORY directory =
        ntHeaders.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!directory.VirtualAddress) {
        return "";
    }
    IMAGE_EXPORT_DIRECTORY exports;
    if (!ReadProcessMemory(
            hProcess,
            dllBase + directory.VirtualAddress,
            &exports,
            sizeof(exports),
            nullptr) ||
        !exports.Name) {
        return "";
    }

    char moduleName[256];
    if (!ReadProcessMemory(
            hProcess,
            dllBase + exports.Name,
            moduleName,
            sizeof(moduleName),
            nullptr)) {
        return "";
    }
    moduleName[sizeof(moduleName) - 1] = '\0';
    return moduleName;
}

SIZE_T GetRemoteImageSize(HANDLE hProcess, PBYTE dllBase) {
    IMAGE_DOS_HEADER dosHeader;
    IMAGE_NT_HEADERS ntHeaders;
    if (!ReadProcessMemory(hProcess, dllBase, &dosHeader, sizeof(dosHeader), nullptr) ||
        dosHeader.e_magic != IMAGE_DOS_SIGNATURE ||
        !ReadProcessMemory(
            hProcess,
            dllBase + dosHeader.e_lfanew,
            &ntHeaders,
            sizeof(ntHeaders),
            nullptr) ||
        ntHeaders.Signature != IMAGE_NT_SIGNATURE) {
        return 0;
    }
    return ntHeaders.OptionalHeader.SizeOfImage;
}

static DWORD GetMemoryProtection(DWORD characteristics) {
    BOOL isExecutable = (characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
    BOOL isReadable = (characteristics & IMAGE_SCN_MEM_READ) != 0;
    BOOL isWritable = (characteristics & IMAGE_SCN_MEM_WRITE) != 0;

    if (isExecutable && isWritable) return PAGE_EXECUTE_READWRITE;
    else if (isExecutable && !isWritable && isReadable) return PAGE_EXECUTE_READ;
    else if (isExecutable && !isWritable && !isReadable) return PAGE_EXECUTE;
    else if (!isExecutable && isWritable) return PAGE_READWRITE;
    else if (!isExecutable && !isWritable && isReadable) return PAGE_READONLY;
    else return PAGE_NOACCESS;
}

static PVOID ReflectiveDLLInject(HANDLE hProcess, PBYTE lpDllBuffer, SIZE_T dllFileSize) {
    // Step 1: Parse the DLL headers
    if (!lpDllBuffer || dllFileSize < sizeof(IMAGE_DOS_HEADER)) {
        fprintf(stderr, "[!] DLL image is too small.\n");
        return nullptr;
    }

    PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)lpDllBuffer;
    if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE || dosHeader->e_lfanew < 0 ||
        !IsRangeWithin((SIZE_T)dosHeader->e_lfanew, sizeof(IMAGE_NT_HEADERS), dllFileSize)) {
        fprintf(stderr, "[!] Invalid DOS signature.\n");
        return nullptr;
    }

    PIMAGE_NT_HEADERS ntHeaders = (PIMAGE_NT_HEADERS)(lpDllBuffer + dosHeader->e_lfanew);
    if (ntHeaders->Signature != IMAGE_NT_SIGNATURE ||
        ntHeaders->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        fprintf(stderr, "[!] Invalid NT signature.\n");
        return nullptr;
    }

    SIZE_T sectionHeadersOffset = (PBYTE)IMAGE_FIRST_SECTION(ntHeaders) - lpDllBuffer;
    SIZE_T sectionHeadersSize = ntHeaders->FileHeader.NumberOfSections * sizeof(IMAGE_SECTION_HEADER);
    if (!IsRangeWithin(sectionHeadersOffset, sectionHeadersSize, dllFileSize)) {
        fprintf(stderr, "[!] Invalid section headers.\n");
        return nullptr;
    }
    
    // Step 2: Allocate memory for the DLL in the remote process
    SIZE_T dllImageSize = ntHeaders->OptionalHeader.SizeOfImage;
    if (dllImageSize == 0 || ntHeaders->OptionalHeader.SizeOfHeaders > dllFileSize ||
        ntHeaders->OptionalHeader.SizeOfHeaders > dllImageSize) {
        fprintf(stderr, "[!] Invalid DLL image size.\n");
        return nullptr;
    }

    PBYTE remoteDllBase = (PBYTE)VirtualAllocEx(hProcess, NULL, dllImageSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remoteDllBase) {
        fprintf(stderr, "[!] VirtualAllocEx failed: %d\n", GetLastError());
        return nullptr;
    }

    // Step 3: Allocate memory for the DLL in the current process
    PBYTE localDllBase = (PBYTE)VirtualAlloc(NULL, dllImageSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!localDllBase) {
        fprintf(stderr, "[!] VirtualAlloc failed: %d\n", GetLastError());
        VirtualFreeEx(hProcess, remoteDllBase, 0, MEM_RELEASE);
        return nullptr;
    }

    // Step 4: Copy the headers and sections of the DLL to the current process
    CustomMemCpy(localDllBase, lpDllBuffer, ntHeaders->OptionalHeader.SizeOfHeaders);

    // Step 5: Copy each section
    PIMAGE_SECTION_HEADER section = IMAGE_FIRST_SECTION(ntHeaders);
    for (int i = 0; i < ntHeaders->FileHeader.NumberOfSections; i++) {
        if (!IsRangeWithin(section[i].PointerToRawData, section[i].SizeOfRawData, dllFileSize) ||
            !IsRangeWithin(section[i].VirtualAddress, section[i].SizeOfRawData, dllImageSize)) {
            fprintf(stderr, "[!] Invalid DLL section range.\n");
            VirtualFree(localDllBase, 0, MEM_RELEASE);
            VirtualFreeEx(hProcess, remoteDllBase, 0, MEM_RELEASE);
            return nullptr;
        }

        BYTE* dest = (BYTE*)localDllBase + section[i].VirtualAddress;
        BYTE* src = (BYTE*)lpDllBuffer + section[i].PointerToRawData;
        CustomMemCpy(dest, src, section[i].SizeOfRawData);
    }

    // Step 6: Perform relocations if the allocated base is different from the preferred base
    SIZE_T delta = (SIZE_T)remoteDllBase - (SIZE_T)ntHeaders->OptionalHeader.ImageBase;
    IMAGE_DATA_DIRECTORY relocationDirectory = ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    if (delta != 0 && relocationDirectory.Size) {
        if (!IsRangeWithin(relocationDirectory.VirtualAddress, relocationDirectory.Size, dllImageSize)) {
            fprintf(stderr, "[!] Invalid relocation directory.\n");
            VirtualFree(localDllBase, 0, MEM_RELEASE);
            VirtualFreeEx(hProcess, remoteDllBase, 0, MEM_RELEASE);
            return nullptr;
        }

        IMAGE_BASE_RELOCATION* reloc = (IMAGE_BASE_RELOCATION*)(localDllBase +
            relocationDirectory.VirtualAddress);
        PBYTE relocationEnd = (PBYTE)reloc + relocationDirectory.Size;
        while ((PBYTE)reloc + sizeof(IMAGE_BASE_RELOCATION) <= relocationEnd && reloc->VirtualAddress) {
            if (reloc->SizeOfBlock < sizeof(IMAGE_BASE_RELOCATION) ||
                (PBYTE)reloc + reloc->SizeOfBlock > relocationEnd) {
                fprintf(stderr, "[!] Invalid relocation block.\n");
                VirtualFree(localDllBase, 0, MEM_RELEASE);
                VirtualFreeEx(hProcess, remoteDllBase, 0, MEM_RELEASE);
                return nullptr;
            }

            DWORD count = (reloc->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);
            WORD* relocData = (WORD*)((BYTE*)reloc + sizeof(IMAGE_BASE_RELOCATION));
            for (DWORD i = 0; i < count; i++) {
                DWORD type = relocData[i] >> 12;
                DWORD offset = relocData[i] & 0xFFF;
                if (type == IMAGE_REL_BASED_DIR64) {
                    if (!IsRangeWithin(reloc->VirtualAddress + offset, sizeof(SIZE_T), dllImageSize)) {
                        fprintf(stderr, "[!] Invalid relocation target.\n");
                        VirtualFree(localDllBase, 0, MEM_RELEASE);
                        VirtualFreeEx(hProcess, remoteDllBase, 0, MEM_RELEASE);
                        return nullptr;
                    }
                    SIZE_T* patchAddr = (SIZE_T*)(localDllBase + reloc->VirtualAddress + offset);
                    *patchAddr += delta;
                }
            }
            reloc = (IMAGE_BASE_RELOCATION*)((BYTE*)reloc + reloc->SizeOfBlock);
        }
    }
    
    // Step 7: Copy the DLL to the remote process
    if (!WriteProcessMemory(hProcess, remoteDllBase, localDllBase, dllImageSize, NULL)) {
        fprintf(stderr, "[!] WriteProcessMemory failed: %d\n", GetLastError());
        VirtualFree(localDllBase, 0, MEM_RELEASE);
        VirtualFreeEx(hProcess, remoteDllBase, 0, MEM_RELEASE);
        return nullptr;
    }

    // Step 8: Set the appropriate protection for the DLL in the remote process
    for (int i = 0; i < ntHeaders->FileHeader.NumberOfSections; i++) {
        DWORD oldProtect;
        DWORD newProtect = GetMemoryProtection(section[i].Characteristics);
        SIZE_T sectionVirtualSize = section[i].Misc.VirtualSize;
        
        if (sectionVirtualSize > 0) {
            if (!IsRangeWithin(section[i].VirtualAddress, sectionVirtualSize, dllImageSize)) {
                fprintf(stderr, "[!] Invalid DLL section virtual range.\n");
                VirtualFree(localDllBase, 0, MEM_RELEASE);
                VirtualFreeEx(hProcess, remoteDllBase, 0, MEM_RELEASE);
                return nullptr;
            }

            if (!VirtualProtectEx(hProcess, (PBYTE)remoteDllBase + section[i].VirtualAddress, sectionVirtualSize, newProtect, &oldProtect)) {
                fprintf(stderr, "[!] VirtualProtectEx failed: %d\n", GetLastError());
                VirtualFree(localDllBase, 0, MEM_RELEASE);
                VirtualFreeEx(hProcess, remoteDllBase, 0, MEM_RELEASE);
                return nullptr;
            }
        }
    }

    if (!FlushInstructionCache(hProcess, remoteDllBase, dllImageSize)) {
        fprintf(stderr, "[!] FlushInstructionCache failed: %d\n", GetLastError());
        VirtualFree(localDllBase, 0, MEM_RELEASE);
        VirtualFreeEx(hProcess, remoteDllBase, 0, MEM_RELEASE);
        return nullptr;
    }

    // Step 9: Free the local DLL base
    VirtualFree(localDllBase, 0, MEM_RELEASE);

    // Step 10: Return the base address of the DLL in the remote process
    return remoteDllBase;
}
