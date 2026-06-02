#include "trace_session.h"
#include "hook_manager.h"
#include "process_manager.h"
#include "trace_renderer.h"
#include <stdio.h>
#include <iostream>

TraceSession::TraceSession()
    : serverPipe_(INVALID_HANDLE_VALUE),
      remotePipeHandle_(nullptr),
      stopRequested_(false),
      outputFormat_(TraceOutputFormat::Text),
      quiet_(false),
      highestDroppedCount_(0) {
}

TraceSession::~TraceSession() {
    Stop();
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
        PIPE_ACCESS_INBOUND,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_NOWAIT,
        1,
        0,
        sizeof(TraceEvent) * 64,
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
        WriteRemoteExport(targetProcess, ntdllNBase, "TransportSequence", &zero, sizeof(zero)) &&
        WriteRemoteExport(targetProcess, ntdllNBase, "TransportDroppedEvents", &zero, sizeof(zero));
}

bool TraceSession::Initialize(
    HANDLE targetProcess,
    PVOID ntdllBase,
    PVOID ntdllNBase,
    const TraceOutputOptions& outputOptions) {
    if (!OpenOutput(outputOptions)) {
        return false;
    }

    if (!CreatePipe(targetProcess)) {
        return false;
    }

    if (!ConfigureRemoteTransport(targetProcess, ntdllBase, ntdllNBase)) {
        return false;
    }

    stopRequested_ = false;
    readerThread_ = std::thread(&TraceSession::ReaderLoop, this);
    return true;
}

void TraceSession::Stop() {
    stopRequested_ = true;
    if (readerThread_.joinable()) {
        readerThread_.join();
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
    while (true) {
        TraceEvent event;
        DWORD bytesRead = 0;
        if (ReadFile(serverPipe_, &event, sizeof(event), &bytesRead, nullptr)) {
            if (!IsValidTraceEvent(event, bytesRead)) {
                fprintf(stderr, "[!] Ignored malformed trace event\n");
                continue;
            }
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
            continue;
        }

        DWORD error = GetLastError();
        if (error == ERROR_NO_DATA) {
            if (stopRequested_) {
                break;
            }
            Sleep(1);
            continue;
        }
        if (error == ERROR_BROKEN_PIPE) {
            break;
        }
        fprintf(stderr, "[!] Trace pipe read failed (%lu)\n", error);
        break;
    }
}
