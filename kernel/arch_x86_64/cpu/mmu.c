#include <arch_api.h>



// 页表项各字段
#define MMU_NX      0x8000000000000000UL    // (NX)  No Execute
#define MMU_ADDR    0x000ffffffffff000UL    // addr
#define MMU_P       0x0000000000000001UL    // (P)   Present


#define IDX_4K(va)  ((va >> 12) & 0x1ff)


// page table, 每个表项控制 4K
// 返回映射结束的虚拟地址
uint64_t pt_map(uint64_t pt, uint64_t va, uint64_t end, uint64_t pa) {
    uint64_t *tbl = (uint64_t*)(DIRECT_MAP_ADDR + pt);

    for (int i = IDX_4K(va); (i < 512) && (va + 0x1000 <= end); ++i) {
        tbl[i] = (pa & MMU_ADDR) | MMU_P;
        va += PAGE_SIZE;
        pa += PAGE_SIZE;
    }

    return va;
}
