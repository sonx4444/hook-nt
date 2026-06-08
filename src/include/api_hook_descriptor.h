#pragma once

#include <stddef.h>
#include <stdint.h>

static const uint32_t API_HOOK_DESCRIPTOR_MAGIC = 0x4B484150;
static const uint16_t API_HOOK_DESCRIPTOR_VERSION = 1;
static const size_t API_HOOK_MODULE_NAME_BYTES = 64;
static const size_t API_HOOK_EXPORT_NAME_BYTES = 128;

#pragma pack(push, 1)
struct ApiHookDescriptor {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    char moduleName[API_HOOK_MODULE_NAME_BYTES];
    char exportName[API_HOOK_EXPORT_NAME_BYTES];
    char handlerExport[API_HOOK_EXPORT_NAME_BYTES];
    char trampolineExport[API_HOOK_EXPORT_NAME_BYTES];
};
#pragma pack(pop)
