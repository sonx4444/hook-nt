#include "hook_registry.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char* argv[]) {
    if (argc != 2) {
        printf("Expected hook DLL path\n");
        return 1;
    }

    std::wstring dllPath(argv[1], argv[1] + strlen(argv[1]));
    std::vector<std::string> hooks = DiscoverHooks(dllPath.c_str());
    if (hooks.size() < 3 ||
        !IsSupportedHook(hooks, "NtCreateFile") ||
        !IsSupportedHook(hooks, "NtReadFile") ||
        !IsSupportedHook(hooks, "NtWriteFile") ||
        IsSupportedHook(hooks, "NtInvalid")) {
        printf("Discovered hook set did not match expected exports\n");
        return 1;
    }

    return 0;
}
