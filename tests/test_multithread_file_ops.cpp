#include <windows.h>
#include <atomic>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <thread>
#include <vector>

static const uint32_t THREAD_COUNT = 8;

static void RunFileOperations(HANDLE startEvent, uint32_t index, std::atomic<uint32_t>* failures) {
    if (WaitForSingleObject(startEvent, INFINITE) != WAIT_OBJECT_0) {
        ++*failures;
        return;
    }

    wchar_t path[64];
    swprintf_s(path, L"multithread_test_file_%u.txt", index);

    HANDLE file = CreateFileW(
        path,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        ++*failures;
        return;
    }

    char expected[32];
    sprintf_s(expected, "thread-%u", index);
    DWORD expectedLength = (DWORD)strlen(expected);
    DWORD bytesWritten = 0;
    if (!WriteFile(file, expected, expectedLength, &bytesWritten, nullptr) ||
        bytesWritten != expectedLength ||
        SetFilePointer(file, 0, nullptr, FILE_BEGIN) == INVALID_SET_FILE_POINTER) {
        ++*failures;
        CloseHandle(file);
        DeleteFileW(path);
        return;
    }

    char actual[32] = {};
    DWORD bytesRead = 0;
    if (!ReadFile(file, actual, sizeof(actual) - 1, &bytesRead, nullptr) ||
        bytesRead != expectedLength ||
        memcmp(actual, expected, expectedLength) != 0) {
        ++*failures;
    }

    CloseHandle(file);
    DeleteFileW(path);
}

int main(int argc, char* argv[]) {
    if (argc == 3 && strcmp(argv[1], "--delay-ms") == 0) {
        Sleep(strtoul(argv[2], nullptr, 10));
    }

    HANDLE startEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!startEvent) {
        printf("Failed to create start event: %lu\n", GetLastError());
        return 1;
    }

    std::atomic<uint32_t> failures = 0;
    std::vector<std::thread> threads;
    for (uint32_t index = 0; index < THREAD_COUNT; ++index) {
        threads.emplace_back(RunFileOperations, startEvent, index, &failures);
    }

    if (!SetEvent(startEvent)) {
        ++failures;
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    CloseHandle(startEvent);

    if (failures != 0) {
        printf("Multithreaded file operations failed: %u\n", failures.load());
        return 1;
    }
    return 0;
}
