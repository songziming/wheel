#ifndef MISC_CTYPE_H
#define MISC_CTYPE_H

#include <base.h>

static inline __PURE int isalnum(int c) {
    return (('0' <= c) && (c <= '9'))
        || (('A' <= c) && (c <= 'Z'))
        || (('a' <= c) && (c <= 'z'));
}

static inline __PURE int isalpha(int c) {
    return (('A' <= c) && (c <= 'Z'))
        || (('a' <= c) && (c <= 'z'));
}

static inline __PURE int iscntrl(int c) {
    return ((c < 0x20) || (0x7f == c));
}

static inline __PURE int isdigit(int c) {
    return (('0' <= c) && (c <= '9'));
}

static inline __PURE int isgraph(int c) {
    return ((0x21 <= c) && (c <= 0x7e));
}

static inline __PURE int islower(int c) {
    return (('a' <= c) && (c <= 'z'));
}

static inline __PURE int isprint(int c) {
    return ((0x20 <= c) && (c <= 0x7e));
}

static inline __PURE int ispunct(int c) {
    return ((0x21 <= c) && (c <= 0x2f))
        || ((0x3a <= c) && (c <= 0x40))
        || ((0x5b <= c) && (c <= 0x60))
        || ((0x7b <= c) && (c <= 0x7e));
}

static inline __PURE int isspace(int c) {
    return ((0x09 <= c) && (c <= 0x0d))
        ||  (0x20 == c);
}

static inline __PURE int isupper(int c) {
    return (('A' <= c) && (c <= 'Z'));
}

static inline __PURE int isxdigit(int c) {
    return (('0' <= c) && (c <= '9'))
        || (('A' <= c) && (c <= 'F'))
        || (('a' <= c) && (c <= 'f'));
}

static inline __PURE int toupper(int c) {
    return c & ~0x20;
}

static inline __PURE int tolower(int c) {
    return c | 0x20;
}

#endif // MISC_CTYPE_H
