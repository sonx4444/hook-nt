#include <windows.h>
#include <atomic>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <thread>
#include <vector>

static const uint32_t THREAD_COUNT = 8;

static void RunFileOperations(
    HANDLE startEvent,
    uint32_t index,
    uint32_t iterations,
    std::atomic<uint32_t>* failures) {
    if (WaitForSingleObject(startEvent, INFINITE) != WAIT_OBJECT_0) {
        ++*failures;
        return;
    }

    for (uint32_t iteration = 0; iteration < iterations; ++iteration) {
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
        sprintf_s(expected, "thread-%u-%u", index, iteration);
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
}

int main(int argc, char* argv[]) {
    uint32_t delayMilliseconds = 0;
    uint32_t iterations = 1;
    for (int index = 1; index < argc; index += 2) {
        if (index + 1 >= argc) {
            printf("Missing value for %s\n", argv[index]);
            return 1;
        }
        if (strcmp(argv[index], "--delay-ms") == 0) {
            delayMilliseconds = strtoul(argv[index + 1], nullptr, 10);
        } else if (strcmp(argv[index], "--iterations") == 0) {
            iterations = strtoul(argv[index + 1], nullptr, 10);
            if (iterations == 0) {
                printf("Iterations must be greater than zero\n");
                return 1;
            }
        } else {
            printf("Unknown option: %s\n", argv[index]);
            return 1;
        }
    }
    if (delayMilliseconds > 0) {
        Sleep(delayMilliseconds);
    }

    HANDLE startEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!startEvent) {
        printf("Failed to create start event: %lu\n", GetLastError());
        return 1;
    }

    std::atomic<uint32_t> failures = 0;
    std::vector<std::thread> threads;
    for (uint32_t index = 0; index < THREAD_COUNT; ++index) {
        threads.emplace_back(RunFileOperations, startEvent, index, iterations, &failures);
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
