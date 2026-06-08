#include "trace_renderer.h"
#include "trace_transport.h"
#include "memory_utils.h"
#include <sstream>
#include <stdio.h>

static NTSTATUS NTAPI ReadSuccess(HANDLE, PVOID source, PVOID destination, ULONG length, PULONG captured) {
    CustomMemCpy(destination, source, length);
    *captured = length;
    return 0;
}

static BYTE* FindField(TraceEvent* event, const char* expectedName) {
    BYTE* cursor = event->payload +
        event->header.moduleNameLength +
        event->header.apiNameLength;
    for (uint16_t index = 0; index < event->header.fieldCount; ++index) {
        TraceFieldHeader* field = (TraceFieldHeader*)cursor;
        cursor += sizeof(*field);
        if (strlen(expectedName) == field->nameLength &&
            CustomMemCmp(cursor, expectedName, field->nameLength) == 0) {
            return cursor - sizeof(*field);
        }
        cursor += field->nameLength + field->valueSize;
    }
    return nullptr;
}

int main() {
    TransportNtReadVirtualMemory = (PVOID)ReadSuccess;
    const char preview[] = "Hello";

    TraceEvent event;
    InitializeTraceEvent(&event, "future.dll", "FutureApi");
    AddTraceUInt32(&event, "answer", 42);
    AddTraceUInt32(&event, "second", 7);
    AddTraceBoolean(&event, "enabled", true);
    AddTraceBufferPreview(&event, "buffer", preview, 5);
    if (!IsValidTraceEvent(event, event.header.size)) {
        printf("Valid generic trace event was rejected\n");
        return 1;
    }

    std::ostringstream json;
    RenderTraceEventJsonl(json, event);
    if (json.str().find("\"module\":\"future.dll\"") == std::string::npos ||
        json.str().find("\"api\":\"FutureApi\"") == std::string::npos ||
        json.str().find("\"hook\":\"future.dll!FutureApi\"") == std::string::npos ||
        json.str().find("\"timestamp_100ns\":") == std::string::npos ||
        json.str().find("\"thread_id\":") == std::string::npos ||
        json.str().find("\"answer\":42") == std::string::npos ||
        json.str().find("\"enabled\":true") == std::string::npos ||
        json.str().find("\"buffer\":{\"type\":\"bytes\"") == std::string::npos ||
        json.str().find("\"text\":\"Hello\"") == std::string::npos) {
        printf("Generic JSON renderer output was incomplete\n");
        return 1;
    }

    std::ostringstream text;
    RenderTraceEventText(text, event);
    if (text.str().find("[*] future.dll!FutureApi") == std::string::npos ||
        text.str().find("    timestamp    : ") == std::string::npos ||
        text.str().find("    thread_id    : ") == std::string::npos ||
        text.str().find("    answer       : 42") == std::string::npos ||
        text.str().find("Hello") == std::string::npos) {
        printf("Generic text renderer output was incomplete\n");
        return 1;
    }

    TraceEvent malformed = event;
    malformed.header.size--;
    if (IsValidTraceEvent(malformed, malformed.header.size)) {
        printf("Truncated TLV was accepted\n");
        return 1;
    }

    malformed = event;
    malformed.header.size++;
    if (IsValidTraceEvent(malformed, malformed.header.size)) {
        printf("Trailing bytes were accepted\n");
        return 1;
    }

    malformed = event;
    BYTE* bufferField = FindField(&malformed, "buffer");
    TraceFieldHeader* bufferHeader = (TraceFieldHeader*)bufferField;
    TraceBytesHeader* bytes = (TraceBytesHeader*)(bufferField + sizeof(*bufferHeader) + bufferHeader->nameLength);
    bytes->captured = TRACE_MAX_BUFFER_BYTES + 1;
    if (IsValidTraceEvent(malformed, malformed.header.size)) {
        printf("Invalid byte preview metadata was accepted\n");
        return 1;
    }

    malformed = event;
    BYTE* secondField = FindField(&malformed, "second");
    TraceFieldHeader* secondHeader = (TraceFieldHeader*)secondField;
    CustomMemCpy(secondField + sizeof(*secondHeader), "answer", secondHeader->nameLength);
    if (IsValidTraceEvent(malformed, malformed.header.size)) {
        printf("Duplicate field name was accepted\n");
        return 1;
    }

    TraceEvent unknown = event;
    TraceFieldHeader* unknownHeader = (TraceFieldHeader*)FindField(&unknown, "answer");
    unknownHeader->type = 99;
    if (!IsValidTraceEvent(unknown, unknown.header.size)) {
        printf("Unknown future field type was rejected\n");
        return 1;
    }
    std::ostringstream unknownJson;
    RenderTraceEventJsonl(unknownJson, unknown);
    if (unknownJson.str().find("\"type\":\"unknown_99\"") == std::string::npos) {
        printf("Unknown future field type was not rendered\n");
        return 1;
    }

    return 0;
}
