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

    return 0;
}
