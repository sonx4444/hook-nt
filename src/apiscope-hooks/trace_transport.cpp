#include "trace_transport.h"
#include "memory_utils.h"
#include <intrin.h>

#pragma intrinsic(_InterlockedCompareExchange64)
#pragma intrinsic(_InterlockedExchange)
#pragma intrinsic(_InterlockedExchange64)
#pragma intrinsic(_InterlockedIncrement)
#pragma intrinsic(__readgsqword)

static const NTSTATUS TRACE_STATUS_NOT_CONFIGURED = (NTSTATUS)0xC0000184L;

extern "C" {
__declspec(dllexport) TraceRing* TransportRing = nullptr;
__declspec(dllexport) HANDLE TransportWakeEvent = nullptr;
__declspec(dllexport) PVOID TransportNtSetEvent = nullptr;
__declspec(dllexport) PVOID TransportNtReadVirtualMemory = nullptr;
__declspec(dllexport) volatile LONG ActiveHookCalls = 0;
}

static bool GetBoundedStringLength(const char* value, size_t maximum, uint8_t* length) {
    if (!value || !length) {
        return false;
    }

    size_t index = 0;
    while (index <= maximum && value[index]) {
        ++index;
    }
    if (index == 0 || index > maximum) {
        return false;
    }

    *length = (uint8_t)index;
    return true;
}

static bool IsValidFieldName(const char* name, uint8_t* length) {
    if (!GetBoundedStringLength(name, TRACE_MAX_FIELD_NAME_BYTES, length)) {
        return false;
    }

    if (name[0] < 'a' || name[0] > 'z') {
        return false;
    }
    for (uint8_t index = 1; index < *length; ++index) {
        char character = name[index];
        if (!((character >= 'a' && character <= 'z') ||
              (character >= '0' && character <= '9') ||
              character == '_')) {
            return false;
        }
    }
    return true;
}

static bool IsValidIdentifier(const char* name, size_t maximum, bool moduleName, uint8_t* length) {
    if (!GetBoundedStringLength(name, maximum, length)) {
        return false;
    }

    for (uint8_t index = 0; index < *length; ++index) {
        char character = name[index];
        if (!((character >= 'a' && character <= 'z') ||
              (character >= 'A' && character <= 'Z') ||
              (character >= '0' && character <= '9') ||
              character == '_' ||
              (moduleName && (character == '.' || character == '-')))) {
            return false;
        }
    }
    return true;
}

static size_t TraceNameBytes(const TraceEvent* event) {
    return (size_t)event->header.moduleNameLength + event->header.apiNameLength;
}

static bool HasTraceField(const TraceEvent* event, const char* name, uint8_t nameLength) {
    const BYTE* cursor = event->payload + TraceNameBytes(event);
    for (uint16_t index = 0; index < event->header.fieldCount; ++index) {
        TraceFieldHeader field;
        CustomMemCpy(&field, cursor, sizeof(field));
        cursor += sizeof(field);
        if (field.nameLength == nameLength && CustomMemCmp(cursor, name, nameLength) == 0) {
            return true;
        }
        cursor += field.nameLength + field.valueSize;
    }
    return false;
}

static bool AppendTraceField(
    TraceEvent* event,
    TraceFieldType type,
    const char* name,
    const void* value,
    uint16_t valueSize) {
    if (!event || !value) {
        if (event) {
            event->header.flags |= TraceEventFlagFieldError;
        }
        return false;
    }

    if (event->header.magic != TRACE_EVENT_MAGIC ||
        event->header.version != TRACE_EVENT_VERSION ||
        event->header.size < sizeof(event->header) + TraceNameBytes(event) ||
        event->header.size > sizeof(*event)) {
        event->header.flags |= TraceEventFlagFieldError;
        return false;
    }

    uint8_t nameLength = 0;
    if (!IsValidFieldName(name, &nameLength) || HasTraceField(event, name, nameLength)) {
        event->header.flags |= TraceEventFlagFieldError;
        return false;
    }

    size_t required = sizeof(TraceFieldHeader) + nameLength + valueSize;
    if ((size_t)event->header.size + required > sizeof(*event)) {
        event->header.flags |= TraceEventFlagTruncated;
        return false;
    }

    TraceFieldHeader field = {};
    field.type = type;
    field.nameLength = nameLength;
    field.valueSize = valueSize;

    BYTE* cursor = (BYTE*)event + event->header.size;
    CustomMemCpy(cursor, &field, sizeof(field));
    cursor += sizeof(field);
    CustomMemCpy(cursor, name, nameLength);
    cursor += nameLength;
    CustomMemCpy(cursor, value, valueSize);

    event->header.size = (uint16_t)(event->header.size + required);
    ++event->header.fieldCount;
    return true;
}

static uint32_t GetCurrentThreadIdFast() {
    return (uint32_t)__readgsqword(0x48);
}

struct SharedSystemTime {
    volatile uint32_t lowPart;
    volatile int32_t high1Time;
    volatile int32_t high2Time;
};

static uint64_t GetSystemTime100nsFast() {
    const volatile SharedSystemTime* systemTime =
        reinterpret_cast<const volatile SharedSystemTime*>(0x7FFE0014);
    int32_t high1Time;
    uint32_t lowPart;
    do {
        high1Time = systemTime->high1Time;
        lowPart = systemTime->lowPart;
    } while (high1Time != systemTime->high2Time);
    return ((uint64_t)(uint32_t)high1Time << 32) | lowPart;
}

bool InitializeTraceEvent(TraceEvent* event, const char* moduleName, const char* apiName) {
    if (!event) {
        return false;
    }

    CustomMemSet(event, 0, sizeof(*event));
    uint8_t moduleNameLength = 0;
    uint8_t apiNameLength = 0;
    if (!IsValidIdentifier(moduleName, TRACE_MAX_MODULE_NAME_BYTES, true, &moduleNameLength) ||
        !IsValidIdentifier(apiName, TRACE_MAX_API_NAME_BYTES, false, &apiNameLength)) {
        event->header.flags |= TraceEventFlagFieldError;
        return false;
    }

    event->header.magic = TRACE_EVENT_MAGIC;
    event->header.version = TRACE_EVENT_VERSION;
    event->header.size = (uint16_t)(sizeof(event->header) + moduleNameLength + apiNameLength);
    event->header.moduleNameLength = moduleNameLength;
    event->header.apiNameLength = apiNameLength;
    event->header.threadId = GetCurrentThreadIdFast();
    event->header.timestamp100ns = GetSystemTime100nsFast();
    CustomMemCpy(event->payload, moduleName, moduleNameLength);
    CustomMemCpy(event->payload + moduleNameLength, apiName, apiNameLength);
    return true;
}

bool AddTracePointer(TraceEvent* event, const char* name, const void* value) {
    uint64_t pointer = (uint64_t)value;
    return AppendTraceField(event, TraceFieldPointer, name, &pointer, sizeof(pointer));
}

bool AddTraceUInt32(TraceEvent* event, const char* name, uint32_t value) {
    return AppendTraceField(event, TraceFieldUInt32, name, &value, sizeof(value));
}

bool AddTraceUInt64(TraceEvent* event, const char* name, uint64_t value) {
    return AppendTraceField(event, TraceFieldUInt64, name, &value, sizeof(value));
}

bool AddTraceInt32(TraceEvent* event, const char* name, int32_t value) {
    return AppendTraceField(event, TraceFieldInt32, name, &value, sizeof(value));
}

bool AddTraceInt64(TraceEvent* event, const char* name, int64_t value) {
    return AppendTraceField(event, TraceFieldInt64, name, &value, sizeof(value));
}

bool AddTraceBoolean(TraceEvent* event, const char* name, bool value) {
    uint8_t encoded = value ? 1 : 0;
    return AppendTraceField(event, TraceFieldBoolean, name, &encoded, sizeof(encoded));
}

bool AddTraceStatus(TraceEvent* event, const char* name, NTSTATUS value) {
    return AppendTraceField(event, TraceFieldStatus, name, &value, sizeof(value));
}

static void CaptureTraceBuffer(TraceBytesValue* preview, const void* buffer, ULONG length) {
    if (!preview) {
        return;
    }

    CustomMemSet(preview, 0, sizeof(*preview));
    preview->header.requested = length;

    if (!buffer || length == 0) {
        return;
    }

    if (!TransportNtReadVirtualMemory) {
        preview->header.captureStatus = TRACE_STATUS_NOT_CONFIGURED;
        return;
    }

    ULONG captureLength = length < TRACE_MAX_BUFFER_BYTES ? length : (ULONG)TRACE_MAX_BUFFER_BYTES;
    ULONG captured = 0;
    TransportNtReadVirtualMemoryProc readMemory =
        reinterpret_cast<TransportNtReadVirtualMemoryProc>(TransportNtReadVirtualMemory);
    preview->header.captureStatus = readMemory(
        (HANDLE)-1,
        (PVOID)buffer,
        preview->bytes,
        captureLength,
        &captured);
    preview->header.captured = captured < captureLength ? captured : captureLength;
}

bool AddTraceBufferPreview(TraceEvent* event, const char* name, const void* buffer, ULONG length) {
    TraceBytesValue preview;
    CaptureTraceBuffer(&preview, buffer, length);
    uint16_t valueSize = (uint16_t)(sizeof(preview.header) + preview.header.captured);
    return AppendTraceField(event, TraceFieldBytes, name, &preview, valueSize);
}

bool EmitTraceEvent(TraceEvent* event) {
    TraceRing* ring = TransportRing;
    if (!event || !ring ||
        ring->magic != TRACE_RING_MAGIC ||
        ring->version != TRACE_RING_VERSION ||
        ring->capacity != TRACE_RING_CAPACITY ||
        ring->eventSize != sizeof(TraceEvent) ||
        event->header.magic != TRACE_EVENT_MAGIC ||
        event->header.version != TRACE_EVENT_VERSION ||
        event->header.size < sizeof(event->header) + TraceNameBytes(event) ||
        event->header.size > sizeof(*event)) {
        return false;
    }

    event->header.sequence = (uint32_t)_InterlockedIncrement(&ring->eventSequence);

    LONG64 position;
    TraceRingSlot* slot;
    while (true) {
        position = _InterlockedCompareExchange64(&ring->enqueuePosition, 0, 0);
        slot = &ring->slots[(uint64_t)position & (TRACE_RING_CAPACITY - 1)];
        LONG64 sequence = _InterlockedCompareExchange64(&slot->sequence, 0, 0);
        LONG64 difference = sequence - position;
        if (difference < 0) {
            _InterlockedIncrement(&ring->droppedEvents);
            return false;
        }
        if (difference == 0 &&
            _InterlockedCompareExchange64(
                &ring->enqueuePosition,
                position + 1,
                position) == position) {
            break;
        }
    }

    CustomMemCpy(&slot->event, event, event->header.size);
    _InterlockedExchange64(&slot->sequence, position + 1);
    if (TransportWakeEvent && TransportNtSetEvent &&
        _InterlockedExchange(&ring->wakePending, 1) == 0) {
        TransportNtSetEventProc setEvent =
            reinterpret_cast<TransportNtSetEventProc>(TransportNtSetEvent);
        if (!NT_SUCCESS(setEvent(TransportWakeEvent, nullptr))) {
            _InterlockedExchange(&ring->wakePending, 0);
        }
    }
    return true;
}
