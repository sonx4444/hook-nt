#include "trace_transport.h"
#include "memory_utils.h"
#include <atomic>
#include <stdio.h>
#include <string.h>
#include <thread>
#include <vector>

static const NTSTATUS STATUS_PIPE_BUSY_TEST = (NTSTATUS)0xC00000AEL;
static const NTSTATUS STATUS_ACCESS_VIOLATION_TEST = (NTSTATUS)0xC0000005L;
static const uint32_t CONCURRENT_THREAD_COUNT = 8;
static const uint32_t EVENTS_PER_THREAD = 100;
static const uint32_t CONCURRENT_EVENT_COUNT = CONCURRENT_THREAD_COUNT * EVENTS_PER_THREAD;
static ULONG LastWriteLength = 0;
static std::atomic<uint32_t> ConcurrentWrites;
static std::atomic<uint32_t> ConcurrentFailures;
static std::atomic<uint32_t> ConcurrentSeen[CONCURRENT_EVENT_COUNT + 1];

static NTSTATUS NTAPI WriteSuccess(
    HANDLE,
    HANDLE,
    PIO_APC_ROUTINE,
    PVOID,
    PIO_STATUS_BLOCK ioStatus,
    PVOID,
    ULONG length,
    PLARGE_INTEGER,
    PULONG) {
    LastWriteLength = length;
    ioStatus->Information = length;
    return 0;
}

static NTSTATUS NTAPI WriteFailure(
    HANDLE,
    HANDLE,
    PIO_APC_ROUTINE,
    PVOID,
    PIO_STATUS_BLOCK,
    PVOID,
    ULONG,
    PLARGE_INTEGER,
    PULONG) {
    return STATUS_PIPE_BUSY_TEST;
}

static NTSTATUS NTAPI WriteConcurrent(
    HANDLE,
    HANDLE,
    PIO_APC_ROUTINE,
    PVOID,
    PIO_STATUS_BLOCK ioStatus,
    PVOID buffer,
    ULONG length,
    PLARGE_INTEGER,
    PULONG) {
    const TraceEvent* event = (const TraceEvent*)buffer;
    uint32_t sequence = event->header.sequence;
    if (sequence == 0 ||
        sequence > CONCURRENT_EVENT_COUNT ||
        event->header.timestamp100ns == 0 ||
        event->header.threadId == 0 ||
        ConcurrentSeen[sequence].fetch_add(1, std::memory_order_relaxed) != 0) {
        ConcurrentFailures.fetch_add(1, std::memory_order_relaxed);
    }
    ConcurrentWrites.fetch_add(1, std::memory_order_relaxed);
    ioStatus->Information = length;
    return 0;
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
    BYTE* cursor = event->payload + event->header.apiNameLength;
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
    TransportPipeHandle = (HANDLE)1;
    TransportSequence = 0;
    TransportDroppedEvents = 0;

    TraceEvent event;
    if (!InitializeTraceEvent(&event, "NtCreateFile") ||
        !AddTracePointer(&event, "handle", (PVOID)0x1234) ||
        !AddTraceUInt32(&event, "small", 7) ||
        !AddTraceUInt64(&event, "large", 0x123456789ULL) ||
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

    TransportNtWriteFile = (PVOID)WriteFailure;
    if (EmitTraceEvent(&event) || TransportDroppedEvents != 1) {
        printf("Failed transport write did not increment drop count\n");
        return 1;
    }

    TransportNtWriteFile = (PVOID)WriteSuccess;
    if (!EmitTraceEvent(&event) ||
        event.header.droppedBefore != 1 ||
        event.header.sequence != 2 ||
        LastWriteLength != event.header.size) {
        printf("Variable-length transport write did not preserve counters\n");
        return 1;
    }

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
    InitializeTraceEvent(&full, "NtLargeEvent");
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

    TransportNtWriteFile = (PVOID)WriteConcurrent;
    TransportSequence = 0;
    TransportDroppedEvents = 0;
    ConcurrentWrites = 0;
    ConcurrentFailures = 0;
    std::vector<std::thread> threads;
    for (uint32_t index = 0; index < CONCURRENT_THREAD_COUNT; ++index) {
        threads.emplace_back([]() {
            for (uint32_t index = 0; index < EVENTS_PER_THREAD; ++index) {
                TraceEvent concurrentEvent;
                if (!InitializeTraceEvent(&concurrentEvent, "NtConcurrent") ||
                    !EmitTraceEvent(&concurrentEvent)) {
                    ConcurrentFailures.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    if (ConcurrentWrites != CONCURRENT_EVENT_COUNT ||
        ConcurrentFailures != 0 ||
        TransportSequence != CONCURRENT_EVENT_COUNT ||
        TransportDroppedEvents != 0) {
        printf("Concurrent transport writes did not preserve event metadata and counters\n");
        return 1;
    }

    return 0;
}
