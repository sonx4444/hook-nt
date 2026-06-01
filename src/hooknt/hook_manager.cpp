#include "hook_manager.h"
#include "process_manager.h"
#include "memory_utils.h"
#include <distorm.h>
#include <vector>

bool HookFunction(HANDLE hProcess, PVOID ntdllBase, PVOID ntdllNBase, const char* functionName) {
    // Get the address of the original NT function
    PVOID originalFunction = GetProcAddressRemote(hProcess, (PBYTE)ntdllBase, functionName);
    if (!originalFunction) {
        printf("[!] Not found %s\n", functionName);
        return false;
    }
    printf("[+] Found %s at 0x%p\n", functionName, originalFunction);

    // Get the address of the hooked NT function
    char hookedFunctionName[MAX_FUNCTION_NAME];
    snprintf(hookedFunctionName, sizeof(hookedFunctionName), "%sN", functionName);
    PVOID hookedFunction = GetProcAddressRemote(hProcess, (PBYTE)ntdllNBase, hookedFunctionName);
    if (!hookedFunction) {
        printf("[!] Not found %s\n", hookedFunctionName);
        return false;
    }
    printf("[+] Found %s at 0x%p\n", hookedFunctionName, hookedFunction);

    // Get the address of the trampoline variable
    char trampolineVarName[MAX_FUNCTION_NAME];
    snprintf(trampolineVarName, sizeof(trampolineVarName), "%sTrampoline", functionName);
    PVOID trampolineVarAddr = GetProcAddressRemote(hProcess, (PBYTE)ntdllNBase, trampolineVarName);
    if (!trampolineVarAddr) {
        printf("[!] Not found %s\n", trampolineVarName);
        return false;
    }

    // Allocate memory for the trampoline
    PVOID trampolineMemory = VirtualAllocEx(hProcess, NULL, TRAMPOLINE_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!trampolineMemory) {
        printf("[!] Failed to allocate trampoline memory\n");
        return false;
    }
    printf("[+] Allocated trampoline memory at 0x%p\n", trampolineMemory);

    // Write the trampoline address to the variable
    if (!WriteProcessMemory(hProcess, trampolineVarAddr, &trampolineMemory, sizeof(PVOID), nullptr)) {
        printf("[!] Failed to write trampoline address\n");
        VirtualFreeEx(hProcess, trampolineMemory, 0, MEM_RELEASE);
        return false;
    }

    // Patch the original NT function to jump to the hooked function
    if (!PatchFunction(hProcess, originalFunction, hookedFunction, trampolineMemory)) {
        PVOID emptyTrampoline = nullptr;
        WriteProcessMemory(hProcess, trampolineVarAddr, &emptyTrampoline, sizeof(PVOID), nullptr);
        VirtualFreeEx(hProcess, trampolineMemory, 0, MEM_RELEASE);
        return false;
    }

    return true;
}

size_t CalculatePatchSize(HANDLE hProcess, PVOID address) {
    // Read the first few bytes of the function to disassemble
    BYTE buffer[32];
    SIZE_T bytesRead = 0;
    
    if (!ReadProcessMemory(hProcess, address, buffer, sizeof(buffer), &bytesRead)) {
        printf("[!] Failed to read memory at %p for patch calculation\n", address);
        return 0;
    }

    // Use DiStorm to disassemble the instructions
    _CodeInfo codeInfo = {};
    codeInfo.codeOffset = (_OffsetType)(ULONG_PTR)address;
    codeInfo.code = buffer;
    codeInfo.codeLen = (int)bytesRead;
    codeInfo.dt = Decode64Bits;

    _DInst decodedInstructions[16];
    unsigned int decodedInstructionsCount = 0;
    
    _DecodeResult result = distorm_decompose64(&codeInfo, decodedInstructions, 16, &decodedInstructionsCount);
    
    if ((result != DECRES_SUCCESS && result != DECRES_MEMORYERR) || decodedInstructionsCount == 0) {
        printf("[!] Failed to disassemble instructions at %p\n", address);
        return 0;
    }

    // Calculate the minimum size needed for our 14-byte patch
    // We need to find complete instructions that cover at least PATCH_SIZE bytes
    size_t totalSize = 0;
    size_t i = 0;
    
    while (totalSize < PATCH_SIZE && i < decodedInstructionsCount) {
        const _DInst& instruction = decodedInstructions[i];
        if (instruction.flags == FLAG_NOT_DECODABLE || (instruction.flags & FLAG_RIP_RELATIVE)) {
            printf("[!] Unsupported RIP-relative or undecodable instruction in trampoline\n");
            return 0;
        }

        for (unsigned int operandIndex = 0; operandIndex < instruction.opsNo; ++operandIndex) {
            if (instruction.ops[operandIndex].type == O_PC) {
                printf("[!] Unsupported relative control-flow instruction in trampoline\n");
                return 0;
            }
        }

        totalSize += decodedInstructions[i].size;
        i++;
    }

    // Ensure we have at least PATCH_SIZE bytes for our absolute jump
    if (totalSize < PATCH_SIZE) {
        printf("[!] Could only calculate %zu safe bytes; need at least %d\n", totalSize, PATCH_SIZE);
        return 0;
    }

    printf("[+] Calculated patch size: %zu bytes for %zu instructions\n", totalSize, i);
    return totalSize;
}

bool CreateTrampoline(HANDLE hProcess, PVOID originalFunction, PVOID trampolineAddress, size_t patchSize) {
    // Read the instructions that will be overwritten
    BYTE* originalBytes = new BYTE[patchSize];
    if (!ReadProcessMemory(hProcess, originalFunction, originalBytes, patchSize, nullptr)) {
        printf("[!] Failed to read original instructions\n");
        delete[] originalBytes;
        return false;
    }

    // Calculate the jump back address (original function + patch size)
    PVOID jumpBackAddress = (PBYTE)originalFunction + patchSize;

    // Create the trampoline code
    BYTE* trampolineCode = new BYTE[patchSize + PATCH_SIZE];
    CustomMemCpy(trampolineCode, originalBytes, patchSize);

    // Define the jump back code template
    BYTE jumpBackCode[] = {
        0x68, 0x00, 0x00, 0x00, 0x00,       // push imm32 (low 32 bits)
        0xC7, 0x44, 0x24, 0x04,             // mov [rsp+4], imm32 (high 32 bits)
        0x00, 0x00, 0x00, 0x00,             // high 32 bits placeholder
        0xC3                                // ret
    };

    // Insert the jump back address into the code
    *(DWORD*)&jumpBackCode[1] = (DWORD)((UINT64)jumpBackAddress & 0xFFFFFFFF);
    *(DWORD*)&jumpBackCode[9] = (DWORD)((UINT64)jumpBackAddress >> 32);

    // Copy the jump back code to the trampoline
    CustomMemCpy(trampolineCode + patchSize, jumpBackCode, sizeof(jumpBackCode));

    // Write the trampoline code
    if (!WriteProcessMemory(hProcess, trampolineAddress, trampolineCode, patchSize + PATCH_SIZE, nullptr)) {
        printf("[!] WriteProcessMemory failed: %d\n", GetLastError());
        delete[] originalBytes;
        delete[] trampolineCode;
        return false;
    }

    DWORD oldProtect;
    if (!VirtualProtectEx(hProcess, trampolineAddress, patchSize + PATCH_SIZE, PAGE_EXECUTE_READ, &oldProtect)) {
        printf("[!] VirtualProtectEx failed: %d\n", GetLastError());
        delete[] originalBytes;
        delete[] trampolineCode;
        return false;
    }

    if (!FlushInstructionCache(hProcess, trampolineAddress, patchSize + PATCH_SIZE)) {
        printf("[!] FlushInstructionCache failed: %d\n", GetLastError());
        delete[] originalBytes;
        delete[] trampolineCode;
        return false;
    }

    delete[] originalBytes;
    delete[] trampolineCode;
    return true;
}

bool PatchFunction(HANDLE hProcess, PVOID originalFunction, PVOID newAddress, PVOID trampolineAddress) {
    // Calculate the size of instructions that will be overwritten
    size_t patchSize = CalculatePatchSize(hProcess, originalFunction);
    if (patchSize == 0) {
        printf("[!] Failed to calculate patch size\n");
        return false;
    }

    // Create the trampoline
    if (!CreateTrampoline(hProcess, originalFunction, trampolineAddress, patchSize)) {
        printf("[!] Failed to create trampoline\n");
        return false;
    }

    // Create the absolute jump to our hook using push+ret
    BYTE shellcode[] = {
        0x68, 0x00, 0x00, 0x00, 0x00,       // push imm32 (low 32 bits)
        0xC7, 0x44, 0x24, 0x04,             // mov [rsp+4], imm32 (high 32 bits)
        0x00, 0x00, 0x00, 0x00,             // high 32 bits placeholder
        0xC3                                // ret
    };

    // Insert the target address into the shellcode
    *(DWORD*)&shellcode[1] = (DWORD)((UINT64)newAddress & 0xFFFFFFFF);
    *(DWORD*)&shellcode[9] = (DWORD)((UINT64)newAddress >> 32);

    DWORD oldProtect;
    if (!VirtualProtectEx(hProcess, originalFunction, patchSize, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        printf("VirtualProtectEx failed: %d\n", GetLastError());
        return false;
    }

    std::vector<BYTE> patch(patchSize, 0x90);
    CustomMemCpy(patch.data(), shellcode, sizeof(shellcode));

    // Copy the shellcode to the target address
    if (!WriteProcessMemory(hProcess, originalFunction, patch.data(), patch.size(), NULL)) {
        printf("[!] WriteProcessMemory failed: %d\n", GetLastError());
        VirtualProtectEx(hProcess, originalFunction, patchSize, oldProtect, &oldProtect);
        return false;
    }

    // Restore the original protection
    if (!VirtualProtectEx(hProcess, originalFunction, patchSize, oldProtect, &oldProtect)) {
        printf("[!] VirtualProtectEx restore failed: %d\n", GetLastError());
        return false;
    }

    if (!FlushInstructionCache(hProcess, originalFunction, patchSize)) {
        printf("[!] FlushInstructionCache failed: %d\n", GetLastError());
        return false;
    }

    return true;
}
