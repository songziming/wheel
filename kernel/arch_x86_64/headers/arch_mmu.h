#ifndef ARCH_MMU_H
#define ARCH_MMU_H

#include <def.h>

typedef enum mmu_attr {
    MMU_USER    = 1,    // 用户态可以访问
    MMU_WRITE   = 2,    // 可写
    MMU_EXEC    = 4,    // 可执行
} mmu_attr_t;

uint64_t mmu_table_create();
void mmu_table_delete(uint64_t tbl);

uint64_t mmu_translate(uint64_t tbl, uint64_t va, mmu_attr_t *attrs);
void mmu_map(uint64_t tbl, uint64_t va, uint64_t end, uint64_t pa, mmu_attr_t attrs);

#endif // ARCH_MMU_H
