#ifndef ARCH_MEM_H
#define ARCH_MEM_H

// TODO 分解为多个子文件，放在 mem 目录下

#include <def.h>
#include <arch_extra.h>
#include <context.h>


// 临时内存分配
INIT_TEXT void early_rw_unlock();
INIT_TEXT void early_alloc_disable();


// 物理内存布局
typedef enum pmtype {
    PM_RESERVED = 0,
    PM_AVAILABLE,
    PM_RECLAIMABLE,
} pmtype_t;
typedef struct pmrange {
    pmtype_t type;
    size_t addr;
    size_t end;
} pmrange_t;
INIT_TEXT void pmmap_init_mb1(uint32_t mmap, uint32_t len);
INIT_TEXT void pmmap_init_mb2(void *tag);
pmrange_t *pmmap_locate(size_t ptr);
void pmmap_show();


// Per-CPU
extern PCPU_BSS vmrange_t g_range_pcpu_vars;
extern PCPU_BSS vmrange_t g_range_pcpu_nmi;
extern PCPU_BSS vmrange_t g_range_pcpu_df;
extern PCPU_BSS vmrange_t g_range_pcpu_pf;
extern PCPU_BSS vmrange_t g_range_pcpu_mc;
extern PCPU_BSS vmrange_t g_range_pcpu_int;
INIT_TEXT void pcpu_allocate(size_t kend);
INIT_TEXT void gsbase_init(int idx);


// MMU
void mmu_walk(uint64_t tbl);
INIT_TEXT void kernel_pgtable_init();


// 内存布局初始化
INIT_TEXT void mem_init();
void reclaim_init();


// 处理异常
void handle_pagefault(int vec, arch_regs_t *f);


#endif // ARCH_MEM_H
