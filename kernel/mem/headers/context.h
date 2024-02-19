#ifndef CONTEXT_H
#define CONTEXT_H

#include <def.h>
#include <spin.h>
#include <arch_api.h>
#include <page.h>
#include <dllist.h>


typedef struct vmrange {
    dlnode_t    dl;
    size_t      addr;   // 虚拟地址
    size_t      end;    // 虚拟地址结束
    size_t      pa;     // 映射的物理地址
    mmu_attr_t  attrs;  // 映射属性
    const char *desc;
} vmrange_t;

typedef struct context {
    spin_t    spin;
    dlnode_t  head; // vmrange 链表头节点
    size_t    table;
} context_t;

// size_t context_search(context_t *ctx, size_t start, size_t end, size_t size);
// void context_insert(context_t *ctx, vmrange_t *rng);
// void context_remove(context_t *ctx, vmrange_t *rng);
// vmrange_t *context_locate(context_t *ctx, size_t va);

// void context_map(context_t *ctx, size_t va, size_t pa, size_t size, mmu_attr_t attrs, const char *desc);
// void context_unmap(context_t *ctx, size_t va, size_t size);

void context_init(context_t *ctx);
void *context_alloc(context_t *ctx, int rank, page_type_t type, mmu_attr_t attrs, const char *desc);
void context_free(context_t *ctx, void *ptr);



INIT_TEXT void kernel_context_init();
context_t *get_kernel_context();
INIT_TEXT vmrange_t *kernel_context_mark(vmrange_t *rng, size_t va, size_t end, size_t pa, mmu_attr_t attrs, const char *desc);
INIT_TEXT void kernel_context_map_all();
void kernel_context_unmap(vmrange_t *rng);

#endif // CONTEXT_H
