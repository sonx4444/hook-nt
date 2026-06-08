#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include <windows.h>

static std::wstring Quote(const std::wstring& value) {
    if (value.find_first_of(L" \t\"") == std::wstring::npos) {
        return value;
    }
    std::wstring result = L"\"";
    for (wchar_t character : value) {
        if (character == L'"') {
            result += L'\\';
        }
        result += character;
    }
    result += L'"';
    return result;
}

int wmain(int argc, wchar_t* argv[]) {
    if (argc < 3) {
        printf("Usage: test_send_ctrl_break <delay-ms> <command> [args...]\n");
        return 1;
    }

    DWORD delay = wcstoul(argv[1], nullptr, 10);
    std::wstring commandLine;
    for (int index = 2; index < argc; ++index) {
        if (!commandLine.empty()) {
            commandLine += L' ';
        }
        commandLine += Quote(argv[index]);
    }
    std::vector<wchar_t> buffer(commandLine.begin(), commandLine.end());
    buffer.push_back(L'\0');

    STARTUPINFOW startupInfo = {sizeof(startupInfo)};
    PROCESS_INFORMATION processInfo = {};
    if (!CreateProcessW(
            argv[2],
            buffer.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NEW_PROCESS_GROUP,
            nullptr,
            nullptr,
            &startupInfo,
            &processInfo)) {
        printf("CreateProcessW failed (%lu)\n", GetLastError());
        return 1;
    }
    CloseHandle(processInfo.hThread);

    Sleep(delay);
    if (!GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, processInfo.dwProcessId)) {
        printf("GenerateConsoleCtrlEvent failed (%lu)\n", GetLastError());
        TerminateProcess(processInfo.hProcess, 1);
        CloseHandle(processInfo.hProcess);
        return 1;
    }

    if (WaitForSingleObject(processInfo.hProcess, 15000) != WAIT_OBJECT_0) {
        printf("ApiScope did not exit after Ctrl+Break\n");
        TerminateProcess(processInfo.hProcess, 1);
        CloseHandle(processInfo.hProcess);
        return 1;
    }
    DWORD exitCode = 1;
    GetExitCodeProcess(processInfo.hProcess, &exitCode);
    CloseHandle(processInfo.hProcess);
    return exitCode == 0 ? 0 : 1;
}
