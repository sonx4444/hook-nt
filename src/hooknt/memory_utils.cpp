#include "memory_utils.h"

int CustomStrCmp(const char* str1, const char* str2) {
    while (*str1 && (*str1 == *str2)) {
        str1++;
        str2++;
    }
    return *(unsigned char*)str1 - *(unsigned char*)str2;
}

int CustomMemCmp(const void* first, const void* second, size_t count) {
    const unsigned char* left = (const unsigned char*)first;
    const unsigned char* right = (const unsigned char*)second;
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
    char* d = (char*)dest;
    const char* s = (const char*)src;
    
    while (count--) {
        *d++ = *s++;
    }
    
    return dest;
}

void* CustomMemSet(void* dest, int value, size_t count) {
    char* d = (char*)dest;
    
    while (count--) {
        *d++ = (char)value;
    }
    
    return dest;
}
