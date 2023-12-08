#ifndef MEM_EARLY_HEAP_H
#define MEM_EARLY_HEAP_H

#include <base.h>

typedef struct early_heap {
    uint8_t *buff;
    uint8_t *ptr;
    uint8_t *end;
} early_heap_t;

INIT_TEXT void early_heap_init(early_heap_t *heap, void *buff, void *end);
INIT_TEXT void early_heap_set_end(early_heap_t *heap, void *end);
INIT_TEXT void *early_heap_get_ptr(early_heap_t *heap);
INIT_TEXT void *early_heap_alloc(early_heap_t *heap, size_t size);

#endif // MEM_EARLY_HEAP_H
