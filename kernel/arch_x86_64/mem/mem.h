#ifndef ARCH_X86_64_MEM_MEM_H
#define ARCH_X86_64_MEM_MEM_H

#include <wheel.h>

typedef struct vmrange vmrange_t;

extern size_t g_percpu_base;
extern size_t g_percpu_step;

INIT_TEXT size_t kspace_add(vmrange_t *rng, size_t va, size_t len, const char *desc);

INIT_TEXT size_t percpu_init(size_t va);

INIT_TEXT void mem_init();

#endif // ARCH_X86_64_MEM_MEM_H
