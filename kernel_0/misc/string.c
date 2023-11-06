#include <wheel.h>

__WEAK usize strlen(const char * s) {
    const char * start = s;
    for (; *s; ++s) {}
    return (usize) (s - start);
}

__WEAK int strcmp(const char * s1, const char * s2) {
    unsigned char c1, c2;
    while (1) {
        c1 = *s1++;
        c2 = *s2++;
        if (c1 != c2) {
            return c1 - c2;
        }
        if (c1 == '\0') {
            break;
        }
    }
    return 0;
}

__WEAK int strncmp(const char * s1, const char * s2, usize n) {
    unsigned char c1, c2;
    while (n--) {
        c1 = *s1++;
        c2 = *s2++;
        if (c1 != c2) {
            return c1 - c2;
        }
        if (c1 == '\0') {
            break;
        }
    }
    return 0;
}

__WEAK char * strcpy(char * dst, const char * src) {
    char * bak = dst;
    while ((*dst++ = *src++) != '\0') {}
    return bak;
}

// if `src` is shorter than `n`, the rest of `dst` will be filled with NULL
__WEAK char * strncpy(char * dst, const char * src, usize n) {
    char * bak = dst;
    while (n--) {
        if ((*dst = *src) != '\0') {
            ++src;
        }
        ++dst;
    }
    return bak;
}

// compares the first `n` bytes of `buf1` to `buf2`
// - return value = 0 if they are identical
// - return value < 0 if `buf1` is less than `buf2`
// - return value > 0 if `buf1` is greater than `buf2`
__WEAK int memcmp(const void * buf1, const void * buf2, usize n) {
    const u8 * p1 = (const u8 *) buf1;
    const u8 * p2 = (const u8 *) buf2;
    for (; n; ++p1, ++p2, --n) {
        if (*p1 != *p2) {
            return (int) (*p1 - *p2);
        }
    }
    return 0;
}

__WEAK void * memcpy(void * dst, const void * src, usize n) {
    u8 * d = (u8 *) dst;
    u8 * s = (u8 *) src;
    while (n--) {
        *d++ = *s++;
    }
    return dst;
}

// buffer overlapping are handled properly
__WEAK void * memmove(void * dst, const void * src, usize n) {
    u8 * d = (u8 *) dst;
    u8 * s = (u8 *) src;
    if (d <= s) {
        while (n--) {
            *d++ = *s++;
        }
    } else {
        d += n;
        s += n;
        while (n--) {
            *--d = *--s;
        }
    }
    return dst;
}

__WEAK void * memset(void * buf, u8 x, usize n) {
    u8 * d = (u8 *) buf;
    while (n--) {
        *d++ = x;
    }
    return buf;
}
