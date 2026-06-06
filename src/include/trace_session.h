#pragma once

#include "common.h"
#include "trace_protocol.h"
#include <condition_variable>
#include <deque>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

enum class TraceOutputFormat {
    Text,
    Jsonl,
};

struct TraceOutputOptions {
    TraceOutputFormat format;
    std::wstring outputPath;
    bool quiet;
};

class TraceSession {
public:
    TraceSession();
    ~TraceSession();

    bool Initialize(
        HANDLE targetProcess,
        PVOID ntdllBase,
        PVOID ntdllNBase,
        const TraceOutputOptions& outputOptions);
    void Stop();

private:
    bool OpenOutput(const TraceOutputOptions& outputOptions);
    bool CreatePipe(HANDLE targetProcess);
    bool ConfigureRemoteTransport(HANDLE targetProcess, PVOID ntdllBase, PVOID ntdllNBase);
    bool WriteRemoteExport(HANDLE targetProcess, PVOID ntdllNBase, const char* exportName, const void* value, SIZE_T size);
    void ReaderLoop();
    void WriterLoop();
    void RenderEvent(const TraceEvent& event);

    HANDLE serverPipe_;
    HANDLE remotePipeHandle_;
    HANDLE stopEvent_;
    std::thread readerThread_;
    std::thread writerThread_;
    std::mutex queueMutex_;
    std::condition_variable queueNotEmpty_;
    std::condition_variable queueNotFull_;
    std::deque<TraceEvent> eventQueue_;
    bool readerFinished_;
    std::ofstream outputFile_;
    TraceOutputFormat outputFormat_;
    bool quiet_;
    uint32_t highestDroppedCount_;
};
