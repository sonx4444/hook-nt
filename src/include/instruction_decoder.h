#pragma once

#include <stddef.h>
#include <stdint.h>

struct DecodedInstruction {
    uint8_t length;
    bool hasRipRelativeMemory;
    bool hasRelativeImmediate;
};

bool DecodeInstruction64(
    const uint8_t* code,
    size_t codeSize,
    DecodedInstruction* instruction);
