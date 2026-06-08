#include "instruction_decoder.h"

#include <Zydis/Zydis.h>

bool DecodeInstruction64(
    const uint8_t* code,
    size_t codeSize,
    DecodedInstruction* decoded) {
    if (!code || codeSize == 0 || !decoded) {
        return false;
    }

    ZydisDecoder decoder;
    if (ZYAN_FAILED(ZydisDecoderInit(
            &decoder,
            ZYDIS_MACHINE_MODE_LONG_64,
            ZYDIS_STACK_WIDTH_64))) {
        return false;
    }

    ZydisDecodedInstruction instruction;
    ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];
    if (ZYAN_FAILED(ZydisDecoderDecodeFull(
            &decoder,
            code,
            codeSize,
            &instruction,
            operands))) {
        return false;
    }

    DecodedInstruction result = {};
    result.length = instruction.length;
    for (uint8_t i = 0; i < instruction.operand_count; ++i) {
        const ZydisDecodedOperand& operand = operands[i];
        if (operand.type == ZYDIS_OPERAND_TYPE_MEMORY &&
            (operand.mem.base == ZYDIS_REGISTER_RIP ||
             operand.mem.base == ZYDIS_REGISTER_EIP)) {
            result.hasRipRelativeMemory = true;
        }
        if (operand.type == ZYDIS_OPERAND_TYPE_IMMEDIATE &&
            operand.imm.is_relative) {
            result.hasRelativeImmediate = true;
        }
    }

    *decoded = result;
    return true;
}
