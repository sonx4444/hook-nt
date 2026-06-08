#pragma once

#include "trace_protocol.h"

static const uint32_t TRACE_RING_MAGIC = 0x474E4952;
static const uint16_t TRACE_RING_VERSION = 1;
static const uint32_t TRACE_RING_CAPACITY = 4096;

static_assert(
    (TRACE_RING_CAPACITY & (TRACE_RING_CAPACITY - 1)) == 0,
    "Trace ring capacity must be a power of two");

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4324)
#endif

struct alignas(64) TraceRingSlot {
    volatile LONG64 sequence;
    TraceEvent event;
};

struct alignas(64) TraceRing {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    uint32_t capacity;
    uint32_t eventSize;
    volatile LONG64 enqueuePosition;
    volatile LONG64 dequeuePosition;
    volatile LONG eventSequence;
    volatile LONG droppedEvents;
    volatile LONG wakePending;
    TraceRingSlot slots[TRACE_RING_CAPACITY];
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif
