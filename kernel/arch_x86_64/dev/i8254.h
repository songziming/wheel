#ifndef ARCH_X86_64_DEV_I8254_H
#define ARCH_X86_64_DEV_I8254_H

#include <wheel.h>

INIT_TEXT void i8254_init();
INIT_TEXT void i8254_disable();

#endif // ARCH_X86_64_DEV_I8254_H
