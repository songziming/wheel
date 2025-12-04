#include <arch_api.h>
#include <page.h>
#include <kstring.h>
#include <debug.h>



// 页表项各字段
#define MMU_NX      0x8000000000000000UL    // (NX)  No Execute
#define MMU_ADDR    0x000ffffffffff000UL    // addr
#define MMU_P       0x0000000000000001UL    // (P)   Present

// 从虚拟地址拆分出各级页表项的编号
#define IDX_4K(va)  ((va >> 12) & 0x1ff)
#define IDX_2M(va)  ((va >> 21) & 0x1ff)
#define IDX_1G(va)  ((va >> 30) & 0x1ff)


// 分配一张页表
static uint64_t alloc_table() {
    uint64_t pa = page_alloc(0, PT_PGTBL);
    if (0 == pa) {
        panic("cannot alloc for mmu");
        return 0;
    }
    g_pages[pa >> PAGE_SHIFT].ent_num = 0;
    kmemset((char*)pa + DIRECT_MAP_ADDR, 0, PAGE_SIZE);
    return pa;
}


// page table, 每个表项控制 4K
// 返回映射结束的虚拟地址
uint64_t pt_map(uint64_t pt, uint64_t va, uint64_t end, uint64_t pa) {
    uint64_t *tbl = (uint64_t*)(DIRECT_MAP_ADDR + pt);
    page_t *info = &g_pages[pt >> PAGE_SHIFT];

    for (int i = IDX_4K(va); (i < 512) && (va + 0x1000 <= end); ++i) {
        tbl[i] = (pa & MMU_ADDR) | MMU_P;
        va += PAGE_SIZE;
        pa += PAGE_SIZE;
    }

    return va;
}
