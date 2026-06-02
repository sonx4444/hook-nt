#pragma once

#include "common.h"
#include <stdint.h>

static const uint32_t TRACE_EVENT_MAGIC = 0x544E4B48;
static const uint16_t TRACE_EVENT_VERSION = 3;
static const size_t TRACE_MAX_EVENT_BYTES = 1024;
static const size_t TRACE_MAX_API_NAME_BYTES = 63;
static const size_t TRACE_MAX_FIELD_NAME_BYTES = 63;
static const size_t TRACE_MAX_BUFFER_BYTES = 64;

enum TraceEventFlags : uint16_t {
    TraceEventFlagTruncated = 1 << 0,
    TraceEventFlagFieldError = 1 << 1,
};

enum TraceFieldType : uint8_t {
    TraceFieldPointer = 1,
    TraceFieldUInt32 = 2,
    TraceFieldUInt64 = 3,
    TraceFieldStatus = 4,
    TraceFieldBytes = 5,
};

#pragma pack(push, 1)
struct TraceEventHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint16_t flags;
    uint16_t fieldCount;
    uint32_t sequence;
    uint32_t droppedBefore;
    NTSTATUS status;
    uint64_t timestamp100ns;
    uint32_t threadId;
    uint8_t apiNameLength;
    uint8_t reserved[3];
};

struct TraceFieldHeader {
    uint8_t type;
    uint8_t nameLength;
    uint16_t valueSize;
};

struct TraceBytesHeader {
    uint32_t requested;
    uint32_t captured;
    NTSTATUS captureStatus;
};
#pragma pack(pop)

struct TraceBytesValue {
    TraceBytesHeader header;
    BYTE bytes[TRACE_MAX_BUFFER_BYTES];
};

struct TraceEvent {
    TraceEventHeader header;
    BYTE payload[TRACE_MAX_EVENT_BYTES - sizeof(TraceEventHeader)];
};

static_assert(sizeof(TraceEvent) == TRACE_MAX_EVENT_BYTES, "Trace event capacity changed");
