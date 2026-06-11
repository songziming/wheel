#ifndef ARCH_API_COMMON_H
#define ARCH_API_COMMON_H

#include <stddef.h>
#include <stdint.h>


//
// 本文件定义的函数需要各 arch 实现
//

// get arch-info
size_t arch_cacheline_size();

// SMP & percpu-data
int cpu_count();
int cpu_index();
void *percpu_ptr(int idx, void *p);
void *thiscpu_ptr(void *p);
#define PERCPU(i,x) ((__typeof__(x))percpu_ptr(i,x))
#define THISCPU(x) ((__typeof__(x))thiscpu_ptr(x))

// 中断
int cpu_int_depth();
int cpu_int_disable();
void cpu_int_restore(int key);
void arch_send_ipi(int cpu, int vec);

// 页表操作
typedef enum mmu_attr {
    MMU_NONE    = 0,
    MMU_USER    = 1,    // 用户态可以访问
    MMU_WRITE   = 2,    // 可写
    MMU_EXEC    = 4,    // 可执行

    // 缓存类型（互斥，默认 WB，无需设置）
    MMU_WC      = 0x100,    // Write-Combining
    MMU_WT      = 0x200,    // Write-Through
    MMU_UC      = 0x300,    // Uncacheable
} mmu_attr_t;
size_t mmu_create();
void   mmu_delete(size_t tbl);
size_t mmu_translate(size_t tbl, size_t va, mmu_attr_t *attrs);
void   mmu_map(size_t tbl, size_t va, size_t end, size_t pa, mmu_attr_t attrs);
void   mmu_unmap(size_t tbl, size_t va, size_t end);
void tlb_shootdown(size_t vstart, size_t vend);

// 多任务支持
typedef struct task task_t;
void arch_task_init(task_t *task, size_t entry, size_t stack_top,
    size_t arg1, size_t arg2, size_t arg3, size_t arg4);
void arch_task_switch();
void arch_enter_ring3(size_t entry, size_t stack_top);


#endif // ARCH_API_COMMON_H
