#include "trace_transport.h"
#include "memory_utils.h"
#include <atomic>
#include <stdio.h>
#include <string.h>
#include <thread>
#include <vector>

static const NTSTATUS STATUS_ACCESS_VIOLATION_TEST = (NTSTATUS)0xC0000005L;
static const uint32_t CONCURRENT_THREAD_COUNT = 8;
static const uint32_t EVENTS_PER_THREAD = 100;
static const uint32_t CONCURRENT_EVENT_COUNT = CONCURRENT_THREAD_COUNT * EVENTS_PER_THREAD;
static std::atomic<uint32_t> ConcurrentFailures;
static std::atomic<uint32_t> ConcurrentSeen[CONCURRENT_EVENT_COUNT + 1];
static std::atomic<uint32_t> WakeCalls;
static TraceRing TestRing;

static void InitializeTestRing() {
    ZeroMemory(&TestRing, sizeof(TestRing));
    TestRing.magic = TRACE_RING_MAGIC;
    TestRing.version = TRACE_RING_VERSION;
    TestRing.capacity = TRACE_RING_CAPACITY;
    TestRing.eventSize = sizeof(TraceEvent);
    for (uint32_t index = 0; index < TRACE_RING_CAPACITY; ++index) {
        TestRing.slots[index].sequence = index;
    }
    TransportRing = &TestRing;
}

static NTSTATUS NTAPI SetEventSuccess(HANDLE, PLONG) {
    WakeCalls.fetch_add(1, std::memory_order_relaxed);
    return 0;
}

static bool ReadTestRing(TraceEvent* event) {
    LONG64 position = TestRing.dequeuePosition;
    TraceRingSlot* slot =
        &TestRing.slots[(uint64_t)position & (TRACE_RING_CAPACITY - 1)];
    if (slot->sequence != position + 1) {
        return false;
    }
    *event = slot->event;
    slot->sequence = position + TRACE_RING_CAPACITY;
    TestRing.dequeuePosition = position + 1;
    return true;
}

static NTSTATUS NTAPI ReadSuccess(HANDLE, PVOID source, PVOID destination, ULONG length, PULONG captured) {
    CustomMemCpy(destination, source, length);
    *captured = length;
    return 0;
}

static NTSTATUS NTAPI ReadFailure(HANDLE, PVOID, PVOID, ULONG, PULONG captured) {
    *captured = 0;
    return STATUS_ACCESS_VIOLATION_TEST;
}

static TraceBytesValue* FindBytesValue(TraceEvent* event, const char* expectedName) {
    BYTE* cursor = event->payload +
        event->header.moduleNameLength +
        event->header.apiNameLength;
    for (uint16_t index = 0; index < event->header.fieldCount; ++index) {
        TraceFieldHeader* field = (TraceFieldHeader*)cursor;
        cursor += sizeof(*field);
        if (field->type == TraceFieldBytes &&
            strlen(expectedName) == field->nameLength &&
            CustomMemCmp(cursor, expectedName, field->nameLength) == 0) {
            return (TraceBytesValue*)(cursor + field->nameLength);
        }
        cursor += field->nameLength + field->valueSize;
    }
    return nullptr;
}

int main() {
    InitializeTestRing();
    WakeCalls = 0;
    TransportWakeEvent = (HANDLE)1;
    TransportNtSetEvent = (PVOID)SetEventSuccess;

    TraceEvent event;
    if (!InitializeTraceEvent(&event, "ntdll.dll", "NtCreateFile") ||
        !AddTracePointer(&event, "handle", (PVOID)0x1234) ||
        !AddTraceUInt32(&event, "small", 7) ||
        !AddTraceUInt64(&event, "large", 0x123456789ULL) ||
        !AddTraceInt32(&event, "signed_small", -7) ||
        !AddTraceInt64(&event, "signed_large", -9) ||
        !AddTraceBoolean(&event, "enabled", true) ||
        !AddTraceStatus(&event, "result", STATUS_ACCESS_VIOLATION_TEST) ||
        event.header.timestamp100ns == 0 ||
        event.header.threadId == 0 ||
        event.header.size >= sizeof(event)) {
        printf("Generic scalar builder failed\n");
        return 1;
    }

    uint16_t fieldCount = event.header.fieldCount;
    if (AddTraceUInt32(&event, "small", 8) ||
        AddTraceUInt32(&event, "InvalidName", 8) ||
        event.header.fieldCount != fieldCount ||
        !(event.header.flags & TraceEventFlagFieldError)) {
        printf("Invalid builder fields were not omitted and flagged\n");
        return 1;
    }

    if (!EmitTraceEvent(&event)) {
        printf("Shared ring enqueue failed\n");
        return 1;
    }
    TraceEvent received;
    if (!ReadTestRing(&received) ||
        received.header.sequence != 1 ||
        received.header.size != event.header.size ||
        WakeCalls != 1) {
        printf("Shared ring did not preserve event metadata\n");
        return 1;
    }
    if (!EmitTraceEvent(&event) || WakeCalls != 1 || !ReadTestRing(&received)) {
        printf("Shared ring wakeups were not coalesced\n");
        return 1;
    }
    TestRing.wakePending = 0;
    if (!EmitTraceEvent(&event) || WakeCalls != 2 || !ReadTestRing(&received)) {
        printf("Shared ring did not signal a new event burst\n");
        return 1;
    }
    TransportWakeEvent = nullptr;
    TransportNtSetEvent = nullptr;

    const char source[] = "Hello";
    TransportNtReadVirtualMemory = (PVOID)ReadSuccess;
    if (!AddTraceBufferPreview(&event, "buffer", source, 5)) {
        printf("Buffer preview builder failed\n");
        return 1;
    }
    TraceBytesValue* preview = FindBytesValue(&event, "buffer");
    if (!preview || preview->header.captured != 5 || CustomMemCmp(preview->bytes, source, 5) != 0) {
        printf("Safe buffer preview copy failed\n");
        return 1;
    }

    TransportNtReadVirtualMemory = (PVOID)ReadFailure;
    if (!AddTraceBufferPreview(&event, "failed_buffer", (PVOID)1, 5)) {
        printf("Failed buffer preview builder failed\n");
        return 1;
    }
    preview = FindBytesValue(&event, "failed_buffer");
    if (!preview ||
        preview->header.captureStatus != STATUS_ACCESS_VIOLATION_TEST ||
        preview->header.captured != 0) {
        printf("Failed buffer preview copy was not reported\n");
        return 1;
    }

    TraceEvent full;
    InitializeTraceEvent(&full, "test.dll", "LargeEvent");
    TransportNtReadVirtualMemory = (PVOID)ReadSuccess;
    for (uint32_t index = 0; index < 100; ++index) {
        char name[16];
        sprintf_s(name, "buffer_%u", index);
        if (!AddTraceBufferPreview(&full, name, source, 5)) {
            break;
        }
    }
    if (!(full.header.flags & TraceEventFlagTruncated) || full.header.size > sizeof(full)) {
        printf("Oversized event was not bounded and flagged\n");
        return 1;
    }

    InitializeTestRing();
    for (uint32_t index = 0; index < TRACE_RING_CAPACITY; ++index) {
        TraceEvent queued;
        InitializeTraceEvent(&queued, "test.dll", "Full");
        if (!EmitTraceEvent(&queued)) {
            printf("Shared ring filled before reaching capacity\n");
            return 1;
        }
    }
    TraceEvent dropped;
    InitializeTraceEvent(&dropped, "test.dll", "Dropped");
    if (EmitTraceEvent(&dropped) || TestRing.droppedEvents != 1) {
        printf("Full shared ring did not report a dropped event\n");
        return 1;
    }
    if (!ReadTestRing(&received)) {
        printf("Could not release a full shared ring slot\n");
        return 1;
    }
    TraceEvent recovered;
    InitializeTraceEvent(&recovered, "test.dll", "Recovered");
    if (!EmitTraceEvent(&recovered) || TestRing.droppedEvents != 1) {
        printf("Shared ring did not recover after a dropped event\n");
        return 1;
    }
    while (ReadTestRing(&received)) {
    }

    InitializeTestRing();
    ConcurrentFailures = 0;
    for (uint32_t index = 0; index <= CONCURRENT_EVENT_COUNT; ++index) {
        ConcurrentSeen[index] = 0;
    }
    std::vector<std::thread> threads;
    for (uint32_t index = 0; index < CONCURRENT_THREAD_COUNT; ++index) {
        threads.emplace_back([]() {
            for (uint32_t index = 0; index < EVENTS_PER_THREAD; ++index) {
                TraceEvent concurrentEvent;
                if (!InitializeTraceEvent(&concurrentEvent, "test.dll", "Concurrent") ||
                    !EmitTraceEvent(&concurrentEvent)) {
                    ConcurrentFailures.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    uint32_t concurrentEvents = 0;
    while (ReadTestRing(&received)) {
        uint32_t sequence = received.header.sequence;
        if (sequence == 0 ||
            sequence > CONCURRENT_EVENT_COUNT ||
            received.header.timestamp100ns == 0 ||
            received.header.threadId == 0 ||
            ConcurrentSeen[sequence].fetch_add(1, std::memory_order_relaxed) != 0) {
            ConcurrentFailures.fetch_add(1, std::memory_order_relaxed);
        }
        ++concurrentEvents;
    }
    if (concurrentEvents != CONCURRENT_EVENT_COUNT ||
        ConcurrentFailures != 0 ||
        TestRing.eventSequence != CONCURRENT_EVENT_COUNT ||
        TestRing.droppedEvents != 0) {
        printf("Concurrent transport writes did not preserve event metadata and counters\n");
        return 1;
    }

    return 0;
}
