#ifndef MISC_STRING_H
#define MISC_STRING_H

#include <base.h>

extern __WEAK usize  strlen (const char * s);
extern __WEAK int    strcmp (const char * s1, const char * s2);
extern __WEAK int    strncmp(const char * s1, const char * s2, usize n);
extern __WEAK char * strcpy (char * dst, const char * src);
extern __WEAK char * strncpy(char * dst, const char * src, usize n);
extern __WEAK int    memcmp (const void * buf1, const void * buf2, usize n);
extern __WEAK void * memcpy (void * dst, const void * src, usize n);
extern __WEAK void * memmove(void * dst, const void * src, usize n);
extern __WEAK void * memset (void * buf, u8 x, usize n);

#endif // MISC_STRING_H
