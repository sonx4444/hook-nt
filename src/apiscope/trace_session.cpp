#include "trace_session.h"
#include "hook_manager.h"
#include "memory_utils.h"
#include "process_manager.h"
#include "trace_renderer.h"
#include <intrin.h>
#include <stdio.h>
#include <iostream>

static const size_t TRACE_EVENT_QUEUE_CAPACITY = 4096;
static const size_t TRACE_RING_READ_BATCH = 256;

#pragma intrinsic(_InterlockedCompareExchange64)
#pragma intrinsic(_InterlockedCompareExchange)
#pragma intrinsic(_InterlockedExchange)
#pragma intrinsic(_InterlockedExchange64)

extern "C" NTSTATUS NTAPI NtMapViewOfSection(
    HANDLE sectionHandle,
    HANDLE processHandle,
    PVOID* baseAddress,
    ULONG_PTR zeroBits,
    SIZE_T commitSize,
    PLARGE_INTEGER sectionOffset,
    PSIZE_T viewSize,
    ULONG inheritDisposition,
    ULONG allocationType,
    ULONG win32Protect);

extern "C" NTSTATUS NTAPI NtUnmapViewOfSection(
    HANDLE processHandle,
    PVOID baseAddress);

TraceSession::TraceSession()
    : mappingHandle_(nullptr),
      localRing_(nullptr),
      remoteRing_(nullptr),
      wakeEvent_(nullptr),
      remoteWakeEvent_(nullptr),
      stopEvent_(CreateEventW(nullptr, TRUE, FALSE, nullptr)),
      readerFinished_(false),
      outputFormat_(TraceOutputFormat::Text),
      quiet_(false),
      reportedDroppedCount_(0),
      setEventBypass_(),
      readMemoryBypass_() {
}

TraceSession::~TraceSession() {
    Stop();
    if (stopEvent_) {
        CloseHandle(stopEvent_);
        stopEvent_ = nullptr;
    }
}

bool TraceSession::OpenOutput(const TraceOutputOptions& outputOptions) {
    outputFormat_ = outputOptions.format;
    quiet_ = outputOptions.quiet;
    if (outputOptions.outputPath.empty()) {
        if (outputFormat_ == TraceOutputFormat::Jsonl) {
            fprintf(stderr, "[!] JSONL output requires --output <path>\n");
            return false;
        }
        if (quiet_) {
            fprintf(stderr, "[!] Quiet mode requires --output <path>\n");
            return false;
        }
        return true;
    }

    outputFile_.open(outputOptions.outputPath, std::ios::out | std::ios::trunc);
    if (!outputFile_) {
        fprintf(stderr, "[!] Failed to open trace output file\n");
        return false;
    }
    return true;
}

bool TraceSession::CreateSharedRing(HANDLE targetProcess) {
    mappingHandle_ = CreateFileMappingW(
        INVALID_HANDLE_VALUE,
        nullptr,
        PAGE_READWRITE,
        0,
        (DWORD)sizeof(TraceRing),
        nullptr);
    if (!mappingHandle_) {
        fprintf(stderr, "[!] CreateFileMappingW failed (%lu)\n", GetLastError());
        return false;
    }

    localRing_ = (TraceRing*)MapViewOfFile(
        mappingHandle_,
        FILE_MAP_READ | FILE_MAP_WRITE,
        0,
        0,
        sizeof(TraceRing));
    if (!localRing_) {
        fprintf(stderr, "[!] MapViewOfFile failed (%lu)\n", GetLastError());
        ReleaseLocalRing();
        return false;
    }

    ZeroMemory(localRing_, sizeof(*localRing_));
    localRing_->magic = TRACE_RING_MAGIC;
    localRing_->version = TRACE_RING_VERSION;
    localRing_->capacity = TRACE_RING_CAPACITY;
    localRing_->eventSize = sizeof(TraceEvent);
    for (uint32_t index = 0; index < TRACE_RING_CAPACITY; ++index) {
        localRing_->slots[index].sequence = index;
    }

    PVOID remoteBase = nullptr;
    SIZE_T viewSize = sizeof(TraceRing);
    NTSTATUS status = NtMapViewOfSection(
        mappingHandle_,
        targetProcess,
        &remoteBase,
        0,
        0,
        nullptr,
        &viewSize,
        2,
        0,
        PAGE_READWRITE);
    if (!NT_SUCCESS(status)) {
        fprintf(stderr, "[!] NtMapViewOfSection failed (0x%08lX)\n", (ULONG)status);
        ReleaseLocalRing();
        return false;
    }

    remoteRing_ = (TraceRing*)remoteBase;

    wakeEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!wakeEvent_) {
        fprintf(stderr, "[!] CreateEventW for trace wakeup failed (%lu)\n", GetLastError());
        NtUnmapViewOfSection(targetProcess, remoteRing_);
        remoteRing_ = nullptr;
        ReleaseLocalRing();
        return false;
    }
    if (!DuplicateHandle(
            GetCurrentProcess(),
            wakeEvent_,
            targetProcess,
            &remoteWakeEvent_,
            0,
            FALSE,
            DUPLICATE_SAME_ACCESS)) {
        fprintf(stderr, "[!] DuplicateHandle for trace wakeup failed (%lu)\n", GetLastError());
        NtUnmapViewOfSection(targetProcess, remoteRing_);
        remoteRing_ = nullptr;
        ReleaseLocalRing();
        return false;
    }
    return true;
}

void TraceSession::ReleaseLocalRing() {
    if (wakeEvent_) {
        CloseHandle(wakeEvent_);
        wakeEvent_ = nullptr;
    }
    if (localRing_) {
        UnmapViewOfFile(localRing_);
        localRing_ = nullptr;
    }
    if (mappingHandle_) {
        CloseHandle(mappingHandle_);
        mappingHandle_ = nullptr;
    }
}

bool TraceSession::TryReadRing(TraceEvent* event) {
    if (!localRing_ || !event) {
        return false;
    }

    LONG64 position =
        _InterlockedCompareExchange64(&localRing_->dequeuePosition, 0, 0);
    TraceRingSlot* slot =
        &localRing_->slots[(uint64_t)position & (TRACE_RING_CAPACITY - 1)];
    LONG64 sequence = _InterlockedCompareExchange64(&slot->sequence, 0, 0);
    if (sequence != position + 1) {
        return false;
    }

    CustomMemCpy(event, &slot->event, sizeof(*event));
    _InterlockedExchange64(
        &slot->sequence,
        position + TRACE_RING_CAPACITY);
    _InterlockedExchange64(&localRing_->dequeuePosition, position + 1);
    return true;
}

bool TraceSession::WriteRemoteExport(
    HANDLE targetProcess,
    PVOID hookImageBase,
    const char* exportName,
    const void* value,
    SIZE_T size) {
    std::optional<RemoteExport> remoteExport =
        GetRemoteExport(targetProcess, (PBYTE)hookImageBase, exportName);
    if (!remoteExport || !remoteExport->address) {
        fprintf(stderr, "[!] Missing transport export: %s\n", exportName);
        return false;
    }

    if (!WriteProcessMemory(
            targetProcess,
            remoteExport->address,
            value,
            size,
            nullptr)) {
        fprintf(stderr, "[!] Failed to write transport export %s (%lu)\n", exportName, GetLastError());
        return false;
    }
    return true;
}

bool TraceSession::ConfigureTransport(
    HANDLE targetProcess,
    PVOID ntdllBase,
    PVOID hookImageBase) {
    if (!CreateBypassTrampolineForFunction(
            targetProcess,
            ntdllBase,
            "NtSetEvent",
            &setEventBypass_) ||
        !CreateBypassTrampolineForFunction(
            targetProcess,
            ntdllBase,
            "NtReadVirtualMemory",
            &readMemoryBypass_)) {
        return false;
    }

    return WriteRemoteExport(
               targetProcess,
               hookImageBase,
               "TransportNtSetEvent",
               &setEventBypass_.address,
               sizeof(setEventBypass_.address)) &&
        WriteRemoteExport(
               targetProcess,
               hookImageBase,
               "TransportNtReadVirtualMemory",
               &readMemoryBypass_.address,
               sizeof(readMemoryBypass_.address));
}

bool TraceSession::Start(
    HANDLE targetProcess,
    PVOID hookImageBase,
    const TraceOutputOptions& outputOptions) {
    if (!stopEvent_) {
        fprintf(stderr, "[!] Failed to create trace stop event\n");
        return false;
    }
    if (!OpenOutput(outputOptions)) {
        return false;
    }

    if (!CreateSharedRing(targetProcess)) {
        return false;
    }

    LONG zero = 0;
    if (!WriteRemoteExport(
            targetProcess,
            hookImageBase,
            "TransportRing",
            &remoteRing_,
            sizeof(remoteRing_)) ||
        !WriteRemoteExport(
            targetProcess,
            hookImageBase,
            "TransportWakeEvent",
            &remoteWakeEvent_,
            sizeof(remoteWakeEvent_)) ||
        !WriteRemoteExport(
            targetProcess,
            hookImageBase,
            "ActiveHookCalls",
            &zero,
            sizeof(zero))) {
        NtUnmapViewOfSection(targetProcess, remoteRing_);
        remoteRing_ = nullptr;
        ReleaseLocalRing();
        return false;
    }

    ResetEvent(stopEvent_);
    readerFinished_ = false;
    reportedDroppedCount_ = 0;
    writerThread_ = std::thread(&TraceSession::WriterLoop, this);
    readerThread_ = std::thread(&TraceSession::ReaderLoop, this);
    return true;
}

bool TraceSession::CleanupRemote(HANDLE targetProcess, PVOID hookImageBase) {
    PVOID empty = nullptr;
    bool cleaned = true;
    cleaned = WriteRemoteExport(
        targetProcess,
        hookImageBase,
        "TransportRing",
        &empty,
        sizeof(empty)) && cleaned;
    cleaned = WriteRemoteExport(
        targetProcess,
        hookImageBase,
        "TransportWakeEvent",
        &empty,
        sizeof(empty)) && cleaned;
    cleaned = WriteRemoteExport(
        targetProcess,
        hookImageBase,
        "TransportNtSetEvent",
        &empty,
        sizeof(empty)) && cleaned;
    cleaned = WriteRemoteExport(
        targetProcess,
        hookImageBase,
        "TransportNtReadVirtualMemory",
        &empty,
        sizeof(empty)) && cleaned;

    if (remoteRing_) {
        if (!NT_SUCCESS(NtUnmapViewOfSection(targetProcess, remoteRing_))) {
            cleaned = false;
        }
        remoteRing_ = nullptr;
    }
    if (remoteWakeEvent_) {
        HANDLE duplicate = nullptr;
        if (DuplicateHandle(
                targetProcess,
                remoteWakeEvent_,
                GetCurrentProcess(),
                &duplicate,
                0,
                FALSE,
                DUPLICATE_CLOSE_SOURCE | DUPLICATE_SAME_ACCESS)) {
            CloseHandle(duplicate);
        } else {
            cleaned = false;
        }
        remoteWakeEvent_ = nullptr;
    }
    if (setEventBypass_.address) {
        cleaned = VirtualFreeEx(
            targetProcess,
            setEventBypass_.address,
            0,
            MEM_RELEASE) != FALSE && cleaned;
        setEventBypass_ = {};
    }
    if (readMemoryBypass_.address) {
        cleaned = VirtualFreeEx(
            targetProcess,
            readMemoryBypass_.address,
            0,
            MEM_RELEASE) != FALSE && cleaned;
        readMemoryBypass_ = {};
    }
    return cleaned;
}

void TraceSession::Stop() {
    if (stopEvent_) {
        SetEvent(stopEvent_);
    }
    if (readerThread_.joinable()) {
        readerThread_.join();
    }
    if (writerThread_.joinable()) {
        writerThread_.join();
    }
    ReleaseLocalRing();
    if (outputFile_) {
        outputFile_.flush();
    }
}

void TraceSession::Drain() {
    if (stopEvent_) {
        SetEvent(stopEvent_);
    }
    if (readerThread_.joinable()) {
        readerThread_.join();
    }
    if (writerThread_.joinable()) {
        writerThread_.join();
    }
    ReleaseLocalRing();
    if (outputFile_) {
        outputFile_.flush();
    }
}

void TraceSession::ReaderLoop() {
    auto reportDroppedEvents = [this]() {
        uint32_t droppedCount = (uint32_t)_InterlockedCompareExchange(
            &localRing_->droppedEvents,
            0,
            0);
        if (droppedCount > reportedDroppedCount_) {
            reportedDroppedCount_ = droppedCount;
            fprintf(stderr, "[!] Trace events dropped: %u\n", reportedDroppedCount_);
        }
    };

    auto queueEvent = [this](const TraceEvent& event) {
        if (!IsValidTraceEvent(event, event.header.size)) {
            fprintf(stderr, "[!] Ignored malformed trace event\n");
            return;
        }
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            queueNotFull_.wait(lock, [this]() {
                return eventQueue_.size() < TRACE_EVENT_QUEUE_CAPACITY;
            });
            eventQueue_.push_back(event);
        }
        queueNotEmpty_.notify_one();
    };

    bool stopping = false;
    while (true) {
        size_t eventsRead = 0;
        while (eventsRead < TRACE_RING_READ_BATCH) {
            TraceEvent event;
            if (!TryReadRing(&event)) {
                break;
            }
            ++eventsRead;
            queueEvent(event);
        }
        reportDroppedEvents();
        if (eventsRead != 0) {
            continue;
        }

        _InterlockedExchange(&localRing_->wakePending, 0);
        TraceEvent racedEvent;
        if (TryReadRing(&racedEvent)) {
            queueEvent(racedEvent);
            continue;
        }
        if (stopping) {
            break;
        }

        HANDLE waitHandles[] = {wakeEvent_, stopEvent_};
        DWORD waitResult = WaitForMultipleObjects(
            ARRAYSIZE(waitHandles),
            waitHandles,
            FALSE,
            INFINITE);
        if (waitResult == WAIT_OBJECT_0) {
            continue;
        }
        if (waitResult == WAIT_OBJECT_0 + 1) {
            stopping = true;
            continue;
        }
        fprintf(stderr, "[!] Trace ring wait failed (%lu)\n", GetLastError());
        break;
    }

    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        readerFinished_ = true;
    }
    queueNotEmpty_.notify_all();
}

void TraceSession::WriterLoop() {
    while (true) {
        TraceEvent event;
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            queueNotEmpty_.wait(lock, [this]() {
                return !eventQueue_.empty() || readerFinished_;
            });
            if (eventQueue_.empty()) {
                break;
            }
            event = eventQueue_.front();
            eventQueue_.pop_front();
        }
        queueNotFull_.notify_one();
        RenderEvent(event);
    }
}

void TraceSession::RenderEvent(const TraceEvent& event) {
    if (!quiet_) {
        RenderTraceEventText(std::cout, event);
    }
    if (outputFile_.is_open()) {
        if (outputFormat_ == TraceOutputFormat::Jsonl) {
            RenderTraceEventJsonl(outputFile_, event);
        } else {
            RenderTraceEventText(outputFile_, event);
        }
    }
}
