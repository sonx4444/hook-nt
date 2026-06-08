#include "trace_renderer.h"
#include <algorithm>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

struct TraceFieldView {
    TraceFieldHeader header;
    std::string name;
    const BYTE* value;
};

static std::string HexStatus(NTSTATUS status) {
    std::ostringstream value;
    value << "0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << (uint32_t)status;
    return value.str();
}

static std::string FormatTimestamp(uint64_t timestamp100ns) {
    if (timestamp100ns == 0) {
        return "unavailable";
    }

    ULARGE_INTEGER timestamp;
    timestamp.QuadPart = timestamp100ns;
    FILETIME fileTime = {};
    fileTime.dwLowDateTime = timestamp.LowPart;
    fileTime.dwHighDateTime = timestamp.HighPart;

    SYSTEMTIME systemTime = {};
    if (!FileTimeToSystemTime(&fileTime, &systemTime)) {
        return "invalid";
    }

    std::ostringstream value;
    value << std::setfill('0')
          << std::setw(4) << systemTime.wYear << "-"
          << std::setw(2) << systemTime.wMonth << "-"
          << std::setw(2) << systemTime.wDay << "T"
          << std::setw(2) << systemTime.wHour << ":"
          << std::setw(2) << systemTime.wMinute << ":"
          << std::setw(2) << systemTime.wSecond << "."
          << std::setw(7) << (timestamp100ns % 10000000) << "Z";
    return value.str();
}

static std::string HexBytes(const BYTE* bytes, size_t size, bool spaced) {
    std::ostringstream value;
    for (size_t index = 0; index < size; ++index) {
        if (spaced && index != 0) {
            value << ' ';
        }
        value << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << (uint32_t)bytes[index];
    }
    return value.str();
}

static std::string JsonEscape(const std::string& input) {
    std::ostringstream output;
    for (unsigned char value : input) {
        switch (value) {
        case '"':
            output << "\\\"";
            break;
        case '\\':
            output << "\\\\";
            break;
        case '\b':
            output << "\\b";
            break;
        case '\f':
            output << "\\f";
            break;
        case '\n':
            output << "\\n";
            break;
        case '\r':
            output << "\\r";
            break;
        case '\t':
            output << "\\t";
            break;
        default:
            if (value < 0x20) {
                output << "\\u" << std::hex << std::setw(4) << std::setfill('0') << (uint32_t)value;
            } else {
                output << value;
            }
        }
    }
    return output.str();
}

static std::string PreviewText(const BYTE* bytes, size_t size) {
    std::string text;
    for (size_t index = 0; index < size; ++index) {
        BYTE value = bytes[index];
        text.push_back(value >= 32 && value <= 126 ? (char)value : '.');
    }
    return text;
}

static bool IsValidIdentifier(const BYTE* name, size_t length, bool moduleName) {
    size_t maximum = moduleName ? TRACE_MAX_MODULE_NAME_BYTES : TRACE_MAX_API_NAME_BYTES;
    if (length == 0 || length > maximum) {
        return false;
    }
    for (size_t index = 0; index < length; ++index) {
        char character = (char)name[index];
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

static bool IsValidFieldName(const BYTE* name, size_t length) {
    if (length == 0 || length > TRACE_MAX_FIELD_NAME_BYTES) {
        return false;
    }
    if (name[0] < 'a' || name[0] > 'z') {
        return false;
    }
    for (size_t index = 1; index < length; ++index) {
        char character = (char)name[index];
        if (!((character >= 'a' && character <= 'z') ||
              (character >= '0' && character <= '9') ||
              character == '_')) {
            return false;
        }
    }
    return true;
}

static bool HasExpectedValueSize(uint8_t type, const BYTE* value, uint16_t valueSize) {
    switch (type) {
    case TraceFieldPointer:
    case TraceFieldUInt64:
    case TraceFieldInt64:
        return valueSize == sizeof(uint64_t);
    case TraceFieldUInt32:
    case TraceFieldStatus:
    case TraceFieldInt32:
        return valueSize == sizeof(uint32_t);
    case TraceFieldBoolean:
        return valueSize == sizeof(uint8_t) && (*value == 0 || *value == 1);
    case TraceFieldBytes: {
        if (valueSize < sizeof(TraceBytesHeader)) {
            return false;
        }
        TraceBytesHeader bytes;
        memcpy(&bytes, value, sizeof(bytes));
        return bytes.captured <= TRACE_MAX_BUFFER_BYTES &&
            bytes.captured <= bytes.requested &&
            valueSize == sizeof(bytes) + bytes.captured;
    }
    default:
        return true;
    }
}

static bool ParseTraceEvent(
    const TraceEvent& event,
    size_t bytesReceived,
    std::string* moduleName,
    std::string* apiName,
    std::vector<TraceFieldView>* fields) {
    if (bytesReceived < sizeof(event.header) ||
        event.header.magic != TRACE_EVENT_MAGIC ||
        event.header.version != TRACE_EVENT_VERSION ||
        event.header.size != bytesReceived ||
        event.header.size > sizeof(event) ||
        event.header.size < sizeof(event.header) +
            event.header.moduleNameLength +
            event.header.apiNameLength) {
        return false;
    }

    const BYTE* cursor = event.payload;
    if (!IsValidIdentifier(cursor, event.header.moduleNameLength, true)) {
        return false;
    }
    if (moduleName) {
        moduleName->assign((const char*)cursor, event.header.moduleNameLength);
    }
    cursor += event.header.moduleNameLength;
    if (!IsValidIdentifier(cursor, event.header.apiNameLength, false)) {
        return false;
    }
    if (apiName) {
        apiName->assign((const char*)cursor, event.header.apiNameLength);
    }
    cursor += event.header.apiNameLength;
    const BYTE* end = (const BYTE*)&event + event.header.size;

    std::vector<TraceFieldView> parsedFields;
    for (uint16_t index = 0; index < event.header.fieldCount; ++index) {
        if ((size_t)(end - cursor) < sizeof(TraceFieldHeader)) {
            return false;
        }

        TraceFieldView field = {};
        memcpy(&field.header, cursor, sizeof(field.header));
        cursor += sizeof(field.header);
        size_t fieldSize = (size_t)field.header.nameLength + field.header.valueSize;
        if ((size_t)(end - cursor) < fieldSize ||
            !IsValidFieldName(cursor, field.header.nameLength)) {
            return false;
        }

        field.name.assign((const char*)cursor, field.header.nameLength);
        for (const TraceFieldView& existing : parsedFields) {
            if (field.name == existing.name) {
                return false;
            }
        }
        cursor += field.header.nameLength;
        field.value = cursor;
        if (!HasExpectedValueSize(field.header.type, field.value, field.header.valueSize)) {
            return false;
        }
        cursor += field.header.valueSize;
        parsedFields.push_back(field);
    }

    if (cursor != end) {
        return false;
    }
    if (fields) {
        *fields = parsedFields;
    }
    return true;
}

static uint32_t ReadUInt32(const BYTE* value) {
    uint32_t result;
    memcpy(&result, value, sizeof(result));
    return result;
}

static uint64_t ReadUInt64(const BYTE* value) {
    uint64_t result;
    memcpy(&result, value, sizeof(result));
    return result;
}

static TraceBytesHeader ReadBytesHeader(const BYTE* value) {
    TraceBytesHeader result;
    memcpy(&result, value, sizeof(result));
    return result;
}

static size_t TextLabelWidth(const std::vector<TraceFieldView>& fields, const TraceEvent& event) {
    size_t width = strlen("timestamp");
    for (const TraceFieldView& field : fields) {
        width = (std::max)(width, field.name.size());
        if (field.header.type == TraceFieldBytes) {
            width = (std::max)(width, field.name.size() + strlen("_text"));
            width = (std::max)(width, field.name.size() + strlen("_status"));
        }
    }
    if (event.header.flags & TraceEventFlagTruncated) {
        width = (std::max)(width, strlen("truncated"));
    }
    if (event.header.flags & TraceEventFlagFieldError) {
        width = (std::max)(width, strlen("field_error"));
    }
    return width;
}

static void RenderTextLabel(std::ostream& output, const std::string& name, size_t width) {
    output << "    " << std::left << std::setw((int)width) << std::setfill(' ') << name << ": ";
}

bool IsValidTraceEvent(const TraceEvent& event, size_t bytesReceived) {
    return ParseTraceEvent(event, bytesReceived, nullptr, nullptr, nullptr);
}

void RenderTraceEventText(std::ostream& output, const TraceEvent& event) {
    std::string moduleName;
    std::string apiName;
    std::vector<TraceFieldView> fields;
    if (!ParseTraceEvent(event, event.header.size, &moduleName, &apiName, &fields)) {
        return;
    }

    size_t labelWidth = TextLabelWidth(fields, event);
    output << "\n[*] " << moduleName << "!" << apiName << " ----------\n";
    RenderTextLabel(output, "timestamp", labelWidth);
    output << FormatTimestamp(event.header.timestamp100ns) << "\n";
    RenderTextLabel(output, "thread_id", labelWidth);
    output << event.header.threadId << "\n";
    RenderTextLabel(output, "sequence", labelWidth);
    output << event.header.sequence << "\n";
    for (const TraceFieldView& field : fields) {
        RenderTextLabel(output, field.name, labelWidth);
        switch (field.header.type) {
        case TraceFieldPointer:
            output << "0x" << std::right << std::uppercase << std::hex << std::setw(16)
                   << std::setfill('0') << ReadUInt64(field.value) << std::setfill(' ') << std::dec;
            break;
        case TraceFieldUInt32:
            output << ReadUInt32(field.value);
            break;
        case TraceFieldUInt64:
            output << ReadUInt64(field.value);
            break;
        case TraceFieldInt32:
            output << (int32_t)ReadUInt32(field.value);
            break;
        case TraceFieldInt64:
            output << (int64_t)ReadUInt64(field.value);
            break;
        case TraceFieldBoolean:
            output << (*field.value ? "true" : "false");
            break;
        case TraceFieldStatus:
            output << HexStatus((NTSTATUS)ReadUInt32(field.value));
            break;
        case TraceFieldBytes: {
            TraceBytesHeader bytes = ReadBytesHeader(field.value);
            const BYTE* preview = field.value + sizeof(bytes);
            output << HexBytes(preview, bytes.captured, true);
            if (bytes.captured < bytes.requested) {
                output << " ...";
            }
            output << "\n";
            RenderTextLabel(output, field.name + "_text", labelWidth);
            output << PreviewText(preview, bytes.captured);
            if (bytes.captured < bytes.requested) {
                output << "...";
            }
            output << "\n";
            RenderTextLabel(output, field.name + "_status", labelWidth);
            output << HexStatus(bytes.captureStatus);
            break;
        }
        default:
            output << "type_" << (uint32_t)field.header.type << " "
                   << HexBytes(field.value, field.header.valueSize, true);
            break;
        }
        output << "\n";
    }
    if (event.header.flags & TraceEventFlagTruncated) {
        RenderTextLabel(output, "truncated", labelWidth);
        output << "true\n";
    }
    if (event.header.flags & TraceEventFlagFieldError) {
        RenderTextLabel(output, "field_error", labelWidth);
        output << "true\n";
    }
    output.flush();
}

void RenderTraceEventJsonl(std::ostream& output, const TraceEvent& event) {
    std::string moduleName;
    std::string apiName;
    std::vector<TraceFieldView> fields;
    if (!ParseTraceEvent(event, event.header.size, &moduleName, &apiName, &fields)) {
        return;
    }

    output << "{\"sequence\":" << event.header.sequence
           << ",\"timestamp\":\"" << FormatTimestamp(event.header.timestamp100ns) << "\""
           << ",\"timestamp_100ns\":" << event.header.timestamp100ns
           << ",\"thread_id\":" << event.header.threadId
           << ",\"module\":\"" << JsonEscape(moduleName) << "\""
           << ",\"api\":\"" << JsonEscape(apiName) << "\""
           << ",\"hook\":\"" << JsonEscape(moduleName + "!" + apiName) << "\""
           << ",\"truncated\":" << ((event.header.flags & TraceEventFlagTruncated) ? "true" : "false")
           << ",\"field_error\":" << ((event.header.flags & TraceEventFlagFieldError) ? "true" : "false")
           << ",\"fields\":{";

    for (size_t index = 0; index < fields.size(); ++index) {
        const TraceFieldView& field = fields[index];
        if (index != 0) {
            output << ",";
        }
        output << "\"" << field.name << "\":";
        switch (field.header.type) {
        case TraceFieldPointer:
        case TraceFieldUInt64:
            output << ReadUInt64(field.value);
            break;
        case TraceFieldUInt32:
            output << ReadUInt32(field.value);
            break;
        case TraceFieldInt32:
            output << (int32_t)ReadUInt32(field.value);
            break;
        case TraceFieldInt64:
            output << (int64_t)ReadUInt64(field.value);
            break;
        case TraceFieldBoolean:
            output << (*field.value ? "true" : "false");
            break;
        case TraceFieldStatus: {
            NTSTATUS status = (NTSTATUS)ReadUInt32(field.value);
            output << "{\"type\":\"status\",\"value\":" << (int32_t)status
                   << ",\"hex\":\"" << HexStatus(status) << "\"}";
            break;
        }
        case TraceFieldBytes: {
            TraceBytesHeader bytes = ReadBytesHeader(field.value);
            const BYTE* preview = field.value + sizeof(bytes);
            output << "{\"type\":\"bytes\",\"requested\":" << bytes.requested
                   << ",\"captured\":" << bytes.captured
                   << ",\"capture_status\":" << (int32_t)bytes.captureStatus
                   << ",\"capture_status_hex\":\"" << HexStatus(bytes.captureStatus) << "\""
                   << ",\"hex\":\"" << HexBytes(preview, bytes.captured, false) << "\""
                   << ",\"text\":\"" << JsonEscape(PreviewText(preview, bytes.captured)) << "\"}";
            break;
        }
        default:
            output << "{\"type\":\"unknown_" << (uint32_t)field.header.type
                   << "\",\"hex\":\"" << HexBytes(field.value, field.header.valueSize, false) << "\"}";
            break;
        }
    }

    output << "}}\n";
    output.flush();
}
