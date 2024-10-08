#ifndef ARCH_INTF_H
#define ARCH_INTF_H

#include "common.h"

void emu_exit(int ret);
void emu_break();
int arch_unwind(size_t *addrs, int max);

void cpu_halt();
void cpu_pause();
void cpu_rfence();
void cpu_wfence();
void cpu_rwfence();

int cpu_int_depth();
int cpu_int_lock();
void cpu_int_unlock(int key);

//------------------------------------------------------------------------------
// 原子操作
//------------------------------------------------------------------------------

uint8_t atomic8_add(volatile uint8_t *ptr, uint8_t val);
uint16_t atomic16_add(volatile uint16_t *ptr, uint16_t val);

int32_t atomic32_get(volatile int32_t *ptr);
int32_t atomic32_set(volatile int32_t *ptr, int32_t val);
int32_t atomic32_add(volatile int32_t *ptr, int32_t val);
int32_t atomic32_cas(volatile int32_t *ptr, int32_t cmp, int32_t val);

int64_t atomic64_get(volatile int64_t *ptr);
int64_t atomic64_set(volatile int64_t *ptr, int64_t val);
int64_t atomic64_add(volatile int64_t *ptr, int32_t val);
int64_t atomic64_cas(volatile int64_t *ptr, int64_t cmp, int64_t val);

intptr_t atomic_get(volatile intptr_t *ptr);
intptr_t atomic_set(volatile intptr_t *ptr, intptr_t val);
intptr_t atomic_add(volatile intptr_t *ptr, intptr_t val);
intptr_t atomic_cas(volatile intptr_t *ptr, intptr_t cmp, intptr_t val);

//------------------------------------------------------------------------------
// 多核
//------------------------------------------------------------------------------

int cpu_count();
int cpu_index();

void *percpu_ptr(int idx, void *ptr);
void *thiscpu_ptr(void *ptr);

//------------------------------------------------------------------------------
// 页表操作
//------------------------------------------------------------------------------

typedef enum mmu_attr {
    MMU_NONE    = 0,
    MMU_USER    = 1,    // 用户态可以访问
    MMU_WRITE   = 2,    // 可写
    MMU_EXEC    = 4,    // 可执行
} mmu_attr_t;

size_t mmu_create_table();
void   mmu_delete_table(size_t tbl);
size_t mmu_translate(size_t tbl, size_t va, mmu_attr_t *attrs);
void   mmu_map(size_t tbl, size_t va, size_t end, size_t pa, mmu_attr_t attrs);
void   mmu_unmap(size_t tbl, size_t va, size_t end);

//------------------------------------------------------------------------------
// 多任务
//------------------------------------------------------------------------------

typedef struct task task_t;

void arch_regs_init(task_t *tid, size_t entry, void *arg1, void *arg2, void *arg3, void *arg4);
void arch_task_switch();
void arch_ipi_resched(int cpu);
void notify_resched(cpuset_t cpus);

#endif // ARCH_INTF_H
