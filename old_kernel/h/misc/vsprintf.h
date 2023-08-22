#ifndef MISC_VSPRINTF_H
#define MISC_VSPRINTF_H

#include <base.h>

extern usize vsnprintf(char * buf, usize size, const char * fmt, va_list args);
extern usize  snprintf(char * buf, usize size, const char * fmt, ...);

#endif // MISC_VSPRINTF_H
