#include "trace_session.h"
#include "hook_manager.h"
#include "process_manager.h"
#include "trace_renderer.h"
#include <stdio.h>
#include <iostream>

static const size_t TRACE_PIPE_EVENT_CAPACITY = 1024;
static const size_t TRACE_EVENT_QUEUE_CAPACITY = 4096;

TraceSession::TraceSession()
    : serverPipe_(INVALID_HANDLE_VALUE),
      remotePipeHandle_(nullptr),
      stopEvent_(CreateEventW(nullptr, TRUE, FALSE, nullptr)),
      readerFinished_(false),
      outputFormat_(TraceOutputFormat::Text),
      quiet_(false),
      highestDroppedCount_(0) {
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

bool TraceSession::CreatePipe(HANDLE targetProcess) {
    WCHAR pipeName[128];
    swprintf_s(
        pipeName,
        L"\\\\.\\pipe\\hooknt-%lu-%llu",
        GetProcessId(targetProcess),
        GetTickCount64());

    serverPipe_ = CreateNamedPipeW(
        pipeName,
        PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        1,
        0,
        sizeof(TraceEvent) * TRACE_PIPE_EVENT_CAPACITY,
        0,
        nullptr);
    if (serverPipe_ == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "[!] CreateNamedPipeW failed (%lu)\n", GetLastError());
        return false;
    }

    HANDLE localClient = CreateFileW(
        pipeName,
        GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (localClient == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "[!] CreateFileW for trace pipe failed (%lu)\n", GetLastError());
        return false;
    }

    DWORD mode = PIPE_NOWAIT;
    if (!SetNamedPipeHandleState(localClient, &mode, nullptr, nullptr)) {
        fprintf(stderr, "[!] SetNamedPipeHandleState failed (%lu)\n", GetLastError());
        CloseHandle(localClient);
        return false;
    }

    HANDLE remoteClient = nullptr;
    if (!DuplicateHandle(
            GetCurrentProcess(),
            localClient,
            targetProcess,
            &remoteClient,
            0,
            FALSE,
            DUPLICATE_SAME_ACCESS)) {
        fprintf(stderr, "[!] DuplicateHandle for trace pipe failed (%lu)\n", GetLastError());
        CloseHandle(localClient);
        return false;
    }
    CloseHandle(localClient);

    remotePipeHandle_ = remoteClient;
    return true;
}

bool TraceSession::WriteRemoteExport(
    HANDLE targetProcess,
    PVOID ntdllNBase,
    const char* exportName,
    const void* value,
    SIZE_T size) {
    PVOID slot = GetProcAddressRemote(targetProcess, (PBYTE)ntdllNBase, exportName);
    if (!slot) {
        fprintf(stderr, "[!] Missing transport export: %s\n", exportName);
        return false;
    }

    if (!WriteProcessMemory(targetProcess, slot, value, size, nullptr)) {
        fprintf(stderr, "[!] Failed to write transport export %s (%lu)\n", exportName, GetLastError());
        return false;
    }
    return true;
}

bool TraceSession::ConfigureRemoteTransport(HANDLE targetProcess, PVOID ntdllBase, PVOID ntdllNBase) {
    RemoteTrampoline writeFileBypass = {};
    RemoteTrampoline readMemoryBypass = {};
    if (!CreateBypassTrampolineForFunction(targetProcess, ntdllBase, "NtWriteFile", &writeFileBypass) ||
        !CreateBypassTrampolineForFunction(targetProcess, ntdllBase, "NtReadVirtualMemory", &readMemoryBypass)) {
        return false;
    }

    LONG zero = 0;
    return WriteRemoteExport(
               targetProcess,
               ntdllNBase,
               "TransportPipeHandle",
               &remotePipeHandle_,
               sizeof(remotePipeHandle_)) &&
        WriteRemoteExport(
               targetProcess,
               ntdllNBase,
               "TransportNtWriteFile",
               &writeFileBypass.address,
               sizeof(writeFileBypass.address)) &&
        WriteRemoteExport(
               targetProcess,
               ntdllNBase,
               "TransportNtReadVirtualMemory",
               &readMemoryBypass.address,
               sizeof(readMemoryBypass.address)) &&
        WriteRemoteExport(
               targetProcess,
               ntdllNBase,
               "TransportSequence",
               &zero,
               sizeof(zero)) &&
        WriteRemoteExport(
               targetProcess,
               ntdllNBase,
               "TransportDroppedEvents",
               &zero,
               sizeof(zero));
}

bool TraceSession::Initialize(
    HANDLE targetProcess,
    PVOID ntdllBase,
    PVOID ntdllNBase,
    const TraceOutputOptions& outputOptions) {
    if (!stopEvent_) {
        fprintf(stderr, "[!] Failed to create trace stop event\n");
        return false;
    }
    if (!OpenOutput(outputOptions)) {
        return false;
    }

    if (!CreatePipe(targetProcess)) {
        return false;
    }

    if (!ConfigureRemoteTransport(targetProcess, ntdllBase, ntdllNBase)) {
        return false;
    }

    ResetEvent(stopEvent_);
    readerFinished_ = false;
    highestDroppedCount_ = 0;
    writerThread_ = std::thread(&TraceSession::WriterLoop, this);
    readerThread_ = std::thread(&TraceSession::ReaderLoop, this);
    return true;
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
    if (serverPipe_ != INVALID_HANDLE_VALUE) {
        CloseHandle(serverPipe_);
        serverPipe_ = INVALID_HANDLE_VALUE;
    }
    if (outputFile_) {
        outputFile_.flush();
    }
}

void TraceSession::ReaderLoop() {
    HANDLE readEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!readEvent) {
        fprintf(stderr, "[!] Failed to create trace read event (%lu)\n", GetLastError());
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            readerFinished_ = true;
        }
        queueNotEmpty_.notify_all();
        return;
    }

    while (true) {
        TraceEvent event;
        DWORD bytesRead = 0;
        OVERLAPPED overlapped = {};
        overlapped.hEvent = readEvent;
        ResetEvent(readEvent);

        bool readCompleted = ReadFile(
            serverPipe_,
            &event,
            sizeof(event),
            &bytesRead,
            &overlapped) != FALSE;
        if (!readCompleted) {
            DWORD error = GetLastError();
            if (error == ERROR_IO_PENDING) {
                HANDLE waitHandles[] = {readEvent, stopEvent_};
                DWORD waitResult = WaitForMultipleObjects(ARRAYSIZE(waitHandles), waitHandles, FALSE, INFINITE);
                if (waitResult == WAIT_OBJECT_0) {
                    readCompleted = GetOverlappedResult(
                        serverPipe_,
                        &overlapped,
                        &bytesRead,
                        FALSE) != FALSE;
                    if (!readCompleted) {
                        error = GetLastError();
                    }
                } else if (waitResult == WAIT_OBJECT_0 + 1) {
                    CancelIoEx(serverPipe_, &overlapped);
                    WaitForSingleObject(readEvent, INFINITE);
                    GetOverlappedResult(serverPipe_, &overlapped, &bytesRead, FALSE);
                    break;
                } else {
                    fprintf(stderr, "[!] Trace pipe wait failed (%lu)\n", GetLastError());
                    CancelIoEx(serverPipe_, &overlapped);
                    WaitForSingleObject(readEvent, INFINITE);
                    GetOverlappedResult(serverPipe_, &overlapped, &bytesRead, FALSE);
                    break;
                }
            }

            if (!readCompleted) {
                if (error == ERROR_BROKEN_PIPE) {
                    break;
                }
                fprintf(stderr, "[!] Trace pipe read failed (%lu)\n", error);
                break;
            }
        }

        if (readCompleted) {
            if (!IsValidTraceEvent(event, bytesRead)) {
                fprintf(stderr, "[!] Ignored malformed trace event\n");
                continue;
            }

            {
                std::unique_lock<std::mutex> lock(queueMutex_);
                queueNotFull_.wait(lock, [this]() {
                    return eventQueue_.size() < TRACE_EVENT_QUEUE_CAPACITY;
                });
                eventQueue_.push_back(event);
            }
            queueNotEmpty_.notify_one();
            continue;
        }
    }

    CloseHandle(readEvent);
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
    if (event.header.droppedBefore > highestDroppedCount_) {
        highestDroppedCount_ = event.header.droppedBefore;
        fprintf(stderr, "[!] Trace events dropped: %u\n", highestDroppedCount_);
    }
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
