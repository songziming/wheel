#ifndef ARCH_X86_64_DRVS_SERIAL_H
#define ARCH_X86_64_DRVS_SERIAL_H

#include <base.h>

extern usize serial_write(const char * s, usize len);

// requires: nothing
extern __INIT void serial_dev_init();

#endif // ARCH_X86_64_DRVS_SERIAL_H
