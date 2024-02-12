#ifndef CONTEXT_H
#define CONTEXT_H

#include <def.h>
#include <spin.h>
#include <vmspace.h>
#include <arch_api.h>

typedef struct context {
    spin_t    lock;
    vmspace_t space;
    size_t    table;
} context_t;

void context_init(context_t *ctx);
void context_map(context_t *ctx, size_t va, size_t pa, size_t size, mmu_attr_t attrs, const char *desc);
void context_unmap(context_t *ctx, size_t va, size_t size);

INIT_TEXT void kernel_context_init();
context_t *get_kernel_context();

#endif // CONTEXT_H
