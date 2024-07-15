// 一套与标准库兼容的字符串操作函数
// 避免使用和标准库相同的函数名，避免单元测试符号冲突
// 都是弱符号，arch 模块可以提供更高效的版本，覆盖默认实现

// GCC 规定，freestanding 环境下需要实现四个函数：memcpy、memmove、memset、memcmp
// 其中 memcpy 需要支持 src==dst 的情况

#include "string.h"
// #include <wheel.h>


WEAK size_t strnlen(const char *s, size_t n) {
    const char *p = s;
    for (; *p && n; ++p, --n) {}
    return (size_t)(p - s);
}

WEAK int strncmp(const char *s1, const char *s2, size_t n) {
    for (; (*s1 || *s2) && n; ++s1, ++s2, --n) {
        if (*s1 != *s2) {
            return (int)*s1 - (int)*s2;
        }
    }
    return 0;
}

// n 是输出字符串 dst 的最大长度，包含结尾的 '\0'
// 如果 src 过长，dst 结尾会缺少终止字符 '\0'
// 如果 src 过短，dst 剩余部分会填充 '\0'
WEAK char *strncpy(char *dst, const char *src, size_t n) {
    // ASSERT(n > 0);
    // ASSERT((dst <= src) || (dst > src + strnlen(src, n)));
    char *bak = dst;
    for (; *src && n; ++src, ++dst, --n) {
        *dst = *src;
    }
    for (; n; ++dst, --n) {
        *dst = '\0';
    }
    return bak;
}

WEAK void *memset(void *buf, int x, size_t n) {
    uint8_t v = x;
    uint8_t *d = (uint8_t *)buf;
    for (; n > 0; ++d, --n) {
        *d = v;
    }
    return buf;
}

WEAK int memcmp(const void *s1, const void *s2, size_t n) {
    const uint8_t *p1 = (const uint8_t *)s1;
    const uint8_t *p2 = (const uint8_t *)s2;
    for (; n > 0; ++p1, ++p2, --n) {
        if (*p1 != *p2) {
            return (int)*p1 - (int)*p2;
        }
    }
    return 0;
}

// 类似 memcpy，不考虑内存重叠的情况
WEAK void *memcpy(void *dst, const void *src, size_t n) {
    // klog("copying from %p to %p\n", src, dst);

    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;

    if (d == s) {
        return dst;
    }

    for (; n; ++s, ++d, --n) {
        *d = *s;
    }

    return dst;
}

WEAK void *memmove(void *dst, const void *src, size_t n) {
    // klog("this is custom implementation\n");

    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;

    if ((src < dst) && (dst < src + n)) {
        d += n;
        s += n;
        for (; n; --s, --d, --n) {
            *d = *s;
        }
    } else {
        for (; n; ++s, ++d, --n) {
            *d = *s;
        }
    }

    return dst;
}
