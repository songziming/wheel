#ifndef ARCH_MEM_H
#define ARCH_MEM_H

#include <base.h>
#include <vmspace.h>

INIT_TEXT void early_alloc_init();
INIT_TEXT void kernel_end_unlock();

INIT_TEXT void ram_range_reserve(int num);
INIT_TEXT void ram_range_set(int idx, size_t addr, size_t len);
INIT_TEXT int ram_range_has_overlap(size_t addr, size_t len);
INIT_TEXT size_t ram_range_get_limit(size_t addr);

extern vmspace_t g_kernel_vm; // 内核虚拟地址空间布局
extern uint64_t g_kernel_map; // 内核页表

typedef struct int_context int_context_t;

INIT_TEXT void mem_init();
void mem_reclaim_init();
void handle_pagefault(int_context_t *ctx);

#endif // ARCH_MEM_H
