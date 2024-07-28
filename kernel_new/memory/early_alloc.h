#ifndef EARLY_ALLOC_H
#define EARLY_ALLOC_H

#include <common.h>

INIT_TEXT void *early_alloc_ro(size_t n);
INIT_TEXT void *early_alloc_rw(size_t n);

#endif // EARLY_ALLOC_H
