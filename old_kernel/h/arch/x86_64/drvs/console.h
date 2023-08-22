#ifndef ARCH_X86_64_DRVS_CONSOLE_H
#define ARCH_X86_64_DRVS_CONSOLE_H

#include <base.h>

extern usize console_write(const char * buf, usize len);

// requires: nothing
extern __INIT void console_dev_init();

#endif // ARCH_X86_64_DRVS_CONSOLE_H
