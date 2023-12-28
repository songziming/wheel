#ifndef ARCH_API_H
#define ARCH_API_H

#include "def.h"
#include <arch_types.h>

void cpu_halt();
void cpu_pause();
void cpu_rfence();
void cpu_wfence();
void cpu_rwfence();

int cpu_int_lock();
void cpu_int_unlock(int key);

void emu_break();
NORETURN void emu_exit(int ret);

void *early_alloc_ro(size_t size);
void *early_alloc_rw(size_t size);

int unwind(size_t *addrs, int max);

int cpu_count();
int cpu_index();

void *pcpu_ptr(int idx, void *ptr);
void *this_ptr(void *ptr);

typedef enum mmu_attr {
    MMU_NONE    = 0,
    MMU_USER    = 1,    // 用户态可以访问
    MMU_WRITE   = 2,    // 可写
    MMU_EXEC    = 4,    // 可执行
} mmu_attr_t;

size_t mmu_table_create();
void   mmu_table_delete(size_t tbl);
size_t mmu_translate(size_t tbl, size_t va, mmu_attr_t *attrs);
void   mmu_map(size_t tbl, size_t va, size_t end, size_t pa, mmu_attr_t attrs);
void   mmu_unmap(size_t tbl, size_t va, size_t end);

void arch_tcb_init(arch_tcb_t *tcb, void *entry, size_t stacktop);

#endif // ARCH_API_H
