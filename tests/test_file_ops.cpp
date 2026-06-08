#include <windows.h>
#include <stdio.h>

int main() {
    // Create test file
    HANDLE hFile = CreateFileW(
        L"test_file.txt",
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hFile == INVALID_HANDLE_VALUE) {
        printf("Failed to create file: %d\n", GetLastError());
        return 1;
    }

    // Write to file
    const char* testData = "Hello, ApiScope!";
    DWORD bytesWritten = 0;
    
    if (!WriteFile(
        hFile,
        testData,
        (DWORD)strlen(testData),
        &bytesWritten,
        NULL
    )) {
        printf("Failed to write to file: %d\n", GetLastError());
        CloseHandle(hFile);
        return 1;
    }

    // Reset file pointer to beginning
    if (SetFilePointer(hFile, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER) {
        printf("Failed to set file pointer: %d\n", GetLastError());
        CloseHandle(hFile);
        return 1;
    }

    // Read from file
    char buffer[256] = { 0 };
    DWORD bytesRead = 0;
    
    if (!ReadFile(
        hFile,
        buffer,
        sizeof(buffer) - 1,
        &bytesRead,
        NULL
    )) {
        printf("Failed to read from file: %d\n", GetLastError());
        CloseHandle(hFile);
        return 1;
    }

    // Null terminate the buffer
    buffer[bytesRead] = '\0';
    printf("Read from file: %s\n", buffer);

    // Cleanup
    CloseHandle(hFile);
    // DeleteFileW(L"test_file.txt");

    return 0;
}
