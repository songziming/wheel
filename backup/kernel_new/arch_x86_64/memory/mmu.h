#ifndef MMU_H
#define MMU_H

#include <common.h>

INIT_TEXT size_t mmu_create_kernel_table();
void mmu_walk(uint64_t tbl);

#endif // MMU_H
