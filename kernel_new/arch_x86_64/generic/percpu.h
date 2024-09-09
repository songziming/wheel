#ifndef PERCPU_H
#define PERCPU_H

#include <common.h>

INIT_TEXT size_t percpu_reserve(size_t size, size_t align);
INIT_TEXT void percpu_init(size_t addr);

INIT_TEXT void gsbase_init(int idx);

#endif // PERCPU_H
