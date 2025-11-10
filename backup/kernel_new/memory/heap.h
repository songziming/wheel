#ifndef HEAP_H
#define HEAP_H

#include <library/spin.h>
#include <library/rbtree.h>

typedef struct heap {
    spin_t spin;
    rbtree_t sizetree;
    char *buff;
    char *end;
} heap_t;


void heap_init(heap_t *heap, void *buff, size_t size);
MALLOC void *heap_alloc(heap_t *heap, size_t size);
void heap_free(heap_t *heap, void *ptr);

INIT_TEXT void kernel_heap_init(void *buff, size_t size);
MALLOC void *kernel_heap_alloc(size_t size);
void kernel_heap_free(void *ptr);

#endif // HEAP_H
