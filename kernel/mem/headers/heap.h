#ifndef HEAP_H
#define HEAP_H

#include <spin.h>
#include <rbtree.h>


typedef struct mem_heap {
    spin_t spin;
    rbtree_t sizetree;
} mem_heap_t;


void heap_init(mem_heap_t *heap, void *buff, size_t size);
void *heap_alloc(mem_heap_t *heap, size_t size);
void heap_free(mem_heap_t *heap, void *ptr);

INIT_TEXT void kernel_heap_init();
void *kernel_heap_alloc(size_t size);
void kernel_heap_free(void *ptr);

#endif // HEAP_H
