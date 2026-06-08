#include "memory_utils.h"

// Keep byte accesses observable: apiscope-hooks.dll cannot link synthesized CRT helpers.
int CustomStrCmp(const char* str1, const char* str2) {
    const volatile unsigned char* left = (const volatile unsigned char*)str1;
    const volatile unsigned char* right = (const volatile unsigned char*)str2;

    while (*left && (*left == *right)) {
        left++;
        right++;
    }
    return *left - *right;
}

int CustomMemCmp(const void* first, const void* second, size_t count) {
    const volatile unsigned char* left = (const volatile unsigned char*)first;
    const volatile unsigned char* right = (const volatile unsigned char*)second;
    while (count--) {
        if (*left != *right) {
            return *left - *right;
        }
        left++;
        right++;
    }
    return 0;
}

void* CustomMemCpy(void* dest, const void* src, size_t count) {
    volatile unsigned char* d = (volatile unsigned char*)dest;
    const volatile unsigned char* s = (const volatile unsigned char*)src;
    
    while (count--) {
        *d++ = *s++;
    }
    
    return dest;
}

void* CustomMemSet(void* dest, int value, size_t count) {
    volatile unsigned char* d = (volatile unsigned char*)dest;
    
    while (count--) {
        *d++ = (unsigned char)value;
    }
    
    return dest;
}
