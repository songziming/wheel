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
int cpu_int_lock();
void cpu_int_unlock(int key);
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

// device
uint32_t pci_read(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg);
void pci_write(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg, uint32_t data);

#endif // ARCH_API_COMMON_H
