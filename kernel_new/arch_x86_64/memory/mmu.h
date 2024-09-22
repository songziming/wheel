#ifndef MMU_H
#define MMU_H

#include <common.h>

INIT_TEXT void mmu_init();
size_t mmu_kernel_table();
void mmu_walk(uint64_t tbl);

#endif // MMU_H
