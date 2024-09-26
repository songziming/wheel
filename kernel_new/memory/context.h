#ifndef CONTEXT_H
#define CONTEXT_H

#include <common.h>
#include <memory/vmspace.h>

typedef struct context {
    vmspace_t space;
    size_t    table;
} context_t;

context_t *kernel_context();

void context_init(context_t *ctx);
void *context_alloc(context_t *ctx, vmrange_t *rng, int rank);

#endif // CONTEXT_H
