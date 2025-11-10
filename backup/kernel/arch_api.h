#ifndef ARCH_API_H
#define ARCH_API_H

// 这里定义的接口需要各 arch 模块实现

#include "def.h"
#include <arch_extra.h>


// 有些宏必须由 arch_extra.h 提供
#ifndef THISCPU_GET
#error "macro THISCPU_GET not defined in arch_extra.h"
#endif
#ifndef THISCPU_SET
#error "macro THISCPU_SET not defined in arch_extra.h"
#endif



//------------------------------------------------------------------------------
// 基本
//------------------------------------------------------------------------------

void cpu_halt();
void cpu_pause();
void cpu_rfence();
void cpu_wfence();
void cpu_rwfence();

int cpu_int_depth();
int cpu_int_lock();
void cpu_int_unlock(int key);

uint32_t atomic32_get(volatile uint32_t *ptr);
uint32_t atomic32_inc(volatile uint32_t *ptr);
// uint32_t thiscpu_atomic32_get(volatile uint32_t *ptr);
// uint32_t thiscpu_atomic32_inc(volatile uint32_t *ptr);


//------------------------------------------------------------------------------
// 调试
//------------------------------------------------------------------------------

NORETURN void emu_exit(int ret);
void emu_break();

int arch_unwind(size_t *addrs, int max);


//------------------------------------------------------------------------------
// 早期内存分配
//------------------------------------------------------------------------------

INIT_TEXT void *early_alloc_ro(size_t size);
INIT_TEXT void *early_alloc_rw(size_t size);


//------------------------------------------------------------------------------
// 多核
//------------------------------------------------------------------------------

int cpu_count();
int cpu_index();

int is_pcpu_var(void *ptr);
void *pcpu_ptr(int idx, void *ptr);
void *this_ptr(void *ptr);


//------------------------------------------------------------------------------
// 页表操作
//------------------------------------------------------------------------------

typedef enum mmu_attr {
    MMU_NONE    = 0,
    MMU_USER    = 1,    // 用户态可以访问
    MMU_WRITE   = 2,    // 可写
    MMU_EXEC    = 4,    // 可执行
} mmu_attr_t;

size_t get_kernel_pgtable();

size_t mmu_create_table();
void   mmu_table_delete(size_t tbl);
size_t mmu_translate(size_t tbl, size_t va, mmu_attr_t *attrs);
void   mmu_map(size_t tbl, size_t va, size_t end, size_t pa, mmu_attr_t attrs);
void   mmu_unmap(size_t tbl, size_t va, size_t end);


//------------------------------------------------------------------------------
// 多任务支持
//------------------------------------------------------------------------------

void arch_tcb_init(arch_tcb_t *tcb, size_t entry, size_t stacktop, size_t args[4]);
void arch_task_switch();
void arch_send_resched(int cpu);
void arch_send_stopall();

#endif // ARCH_API_H
