#include "memory_utils.h"
#include <stdio.h>

int main() {
    char source[] = "ApiScope";
    char destination[sizeof(source)] = {};

    CustomMemCpy(destination, source, sizeof(source));
    if (CustomStrCmp(destination, source) != 0) {
        printf("CustomMemCpy failed\n");
        return 1;
    }

    CustomMemSet(destination, 'x', sizeof(destination) - 1);
    destination[sizeof(destination) - 1] = '\0';
    if (CustomStrCmp(destination, "xxxxxxxx") != 0) {
        printf("CustomMemSet failed\n");
        return 1;
    }

    if (CustomStrCmp("abc", "abd") >= 0 || CustomStrCmp("abd", "abc") <= 0) {
        printf("CustomStrCmp ordering failed\n");
        return 1;
    }

    return 0;
}
