#include "hook_registry.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char* argv[]) {
    if (argc != 2) {
        printf("Expected hook DLL path\n");
        return 1;
    }

    std::wstring dllPath(argv[1], argv[1] + strlen(argv[1]));
    std::vector<HookDefinition> hooks = DiscoverHooks(dllPath.c_str());
    if (hooks.size() < 4 ||
        !FindSupportedHook(hooks, "ntdll.dll!NtCreateFile") ||
        !FindSupportedHook(hooks, "ntdll.dll!NtReadFile") ||
        !FindSupportedHook(hooks, "ntdll.dll!NtWriteFile") ||
        !FindSupportedHook(hooks, "bcrypt.dll!BCryptOpenAlgorithmProvider") ||
        FindSupportedHook(hooks, "ntdll.dll!NtInvalid")) {
        printf("Discovered hook set did not match expected exports\n");
        return 1;
    }
    if (NormalizeModuleName("C:\\Windows\\System32\\NTDLL.DLL") != "ntdll.dll") {
        printf("Module normalization did not match\n");
        return 1;
    }

    return 0;
}
