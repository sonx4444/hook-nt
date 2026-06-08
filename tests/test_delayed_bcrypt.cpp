#include <stdio.h>
#include <windows.h>
#include <winternl.h>

using BCryptOpenAlgorithmProviderProc =
    NTSTATUS(WINAPI*)(PVOID*, const wchar_t*, const wchar_t*, ULONG);
using BCryptCloseAlgorithmProviderProc = NTSTATUS(WINAPI*)(PVOID, ULONG);

int wmain(int argc, wchar_t* argv[]) {
    DWORD delay = argc == 3 && wcscmp(argv[1], L"--delay-ms") == 0
        ? wcstoul(argv[2], nullptr, 10)
        : 0;
    if (delay) {
        Sleep(delay);
    }

    HMODULE bcrypt = LoadLibraryW(L"bcrypt.dll");
    if (!bcrypt) {
        printf("LoadLibraryW failed (%lu)\n", GetLastError());
        return 1;
    }

    auto openAlgorithm = (BCryptOpenAlgorithmProviderProc)GetProcAddress(
        bcrypt,
        "BCryptOpenAlgorithmProvider");
    auto closeAlgorithm = (BCryptCloseAlgorithmProviderProc)GetProcAddress(
        bcrypt,
        "BCryptCloseAlgorithmProvider");
    if (!openAlgorithm || !closeAlgorithm) {
        FreeLibrary(bcrypt);
        return 1;
    }

    PVOID algorithm = nullptr;
    NTSTATUS status = openAlgorithm(&algorithm, L"SHA256", nullptr, 0);
    if (status >= 0 && algorithm) {
        closeAlgorithm(algorithm, 0);
    }
    FreeLibrary(bcrypt);
    return status >= 0 ? 0 : 1;
}
