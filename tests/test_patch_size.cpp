#include "hook_manager.h"
#include "memory_utils.h"
#include <stdio.h>

int main() {
    BYTE safeInstructions[32];
    CustomMemSet(safeInstructions, 0x90, sizeof(safeInstructions));
    if (CalculatePatchSize(GetCurrentProcess(), safeInstructions) != PATCH_SIZE) {
        printf("Safe instruction span was not accepted\n");
        return 1;
    }

    BYTE ripRelativeInstructions[32] = {0x48, 0x8B, 0x05, 0x00, 0x00, 0x00, 0x00};
    if (CalculatePatchSize(GetCurrentProcess(), ripRelativeInstructions) != 0) {
        printf("RIP-relative instruction was not rejected\n");
        return 1;
    }

    BYTE relativeControlFlowInstructions[32] = {0xE8, 0x00, 0x00, 0x00, 0x00};
    if (CalculatePatchSize(GetCurrentProcess(), relativeControlFlowInstructions) != 0) {
        printf("Relative control-flow instruction was not rejected\n");
        return 1;
    }

    BYTE originalBytes[32];
    BYTE expectedBytes[32];
    CustomMemSet(originalBytes, 0x90, sizeof(originalBytes));
    CustomMemCpy(expectedBytes, originalBytes, sizeof(originalBytes));

    RemoteTrampoline trampoline;
    if (!CreateBypassTrampoline(GetCurrentProcess(), originalBytes, &trampoline)) {
        printf("Failed to create bypass trampoline\n");
        return 1;
    }
    if (CustomMemCmp(originalBytes, expectedBytes, sizeof(originalBytes)) != 0) {
        printf("Bypass trampoline modified the original entry bytes\n");
        VirtualFree(trampoline.address, 0, MEM_RELEASE);
        return 1;
    }
    VirtualFree(trampoline.address, 0, MEM_RELEASE);

    return 0;
}
