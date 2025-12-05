#include <arch_api.h>
#include <page.h>
#include <kstring.h>
#include <debug.h>



// 页表项各字段
#define MMU_NX      0x8000000000000000UL    // (NX)  No Execute
#define MMU_ADDR    0x000ffffffffff000UL    // addr
#define MMU_P       0x0000000000000001UL    // (P)   Present
#define MMU_PS      0x0000000000000080UL    // (PS)  Page Size, is it 2M PDE?
#define MMU_US      0x0000000000000004UL    // (U/S) User Supervisor
#define MMU_RW      0x0000000000000002UL    // (R/W) Read Write

#define MMU_ATTRS (MMU_NX | MMU_US | MMU_RW) // 访问权限

// 从虚拟地址拆分出各级页表项的编号
#define IDX_4K(va)  (int)((va >> 12) & 0x1ff)
#define IDX_2M(va)  (int)((va >> 21) & 0x1ff)
#define IDX_1G(va)  (int)((va >> 30) & 0x1ff)

#define SIZE_4K (1UL << 12)
#define SIZE_2M (1UL << 21)
#define SIZE_1G (1UL << 30)

#define OFFSET_4K(x)    ((x) & (SIZE_4K - 1))
#define OFFSET_2M(x)    ((x) & (SIZE_2M - 1))
#define OFFSET_1G(x)    ((x) & (SIZE_1G - 1))


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
static uint64_t pt_map(uint64_t pt, uint64_t va, uint64_t end, uint64_t pa, uint64_t attrs) {
    uint64_t *tbl = (uint64_t*)(DIRECT_MAP_ADDR + pt);
    page_t *info = &g_pages[pt >> PAGE_SHIFT];

    for (int i = IDX_4K(va); (i < 512) && (va + 0x1000 <= end); ++i) {
        if (0 == (tbl[i] & MMU_P)) {
            ++info->ent_num;
        }
        tbl[i] = (pa & MMU_ADDR) | MMU_P;
        va += SIZE_4K;
        pa += SIZE_4K;
    }

    return va;
}


static uint64_t pt_unmap(uint64_t pt, uint64_t va, uint64_t end) {
    uint64_t *tbl = (uint64_t*)(DIRECT_MAP_ADDR + pt);
    page_t *info = &g_pages[pt >> PAGE_SHIFT];

    for (int i = i = IDX_4K(va); (i < 512) && (va + 0x1000 <= end); ++i) {
        if (tbl[i] & MMU_P) {
            --info->ent_num;
        }
        tbl[i] = 0;
        va += SIZE_4K;
    }

    return va;
}

static void pt_free(uint64_t pt) {
    page_free(pt);
}


//------------------------------------------------------------------------------
// page directory，表项可以指向 PT，也可以直接映射 2M
//------------------------------------------------------------------------------

static uint64_t pd_map(uint64_t pd, uint64_t va, uint64_t end, uint64_t pa, uint64_t attrs) {
    uint64_t *tbl = (uint64_t*)(DIRECT_MAP_ADDR + pd);
    page_t *info = &g_pages[pd >> PAGE_SHIFT];

    for (int i = IDX_2M(va); (i < 512) && (va < end); ++i) {
        if (!OFFSET_2M(va | pa) && (va + SIZE_2M <= end)) {
            if (0 == (tbl[i] & MMU_P)) {
                ++info->ent_num;
            } else if (0 == (tbl[i] & MMU_PS)) {
                // 将下一级页表回收，改为 2M 条目，总条目数量不变
                pt_free(tbl[i] & MMU_ADDR);
            }

            tbl[i] = (pa & MMU_ADDR) | MMU_P | MMU_PS | attrs;
            va += SIZE_2M;
            pa += SIZE_2M;
            continue;
        }

        // 无法使用 2M-mapping，需要建立下一级页表
        uint64_t pt = tbl[i] & MMU_ADDR;

        if (0 == (tbl[i] & MMU_P)) {
            ++info->ent_num;
            pt = alloc_table();
        } else if (tbl[i] & MMU_PS) {
            // 将 2M 表项拆分
            uint64_t old_va = va - OFFSET_2M(va);
            uint64_t old_pa = pt;
            ASSERT(0 == OFFSET_2M(old_pa));

            pt = alloc_table();
            if (old_va != va) {
                pt_map(pt, old_va, va, old_pa, tbl[i] & MMU_ATTRS);
            }
            if (end < old_va + SIZE_2M) {
                pt_map(pt, end, old_va + SIZE_2M, end - old_va + old_pa, tbl[i] & MMU_ATTRS);
            }
        }

        tbl[i] = (pt & MMU_ADDR) | MMU_P | MMU_RW | MMU_US; // 不是末级表项，使用最宽松的权限
        uint64_t len = pt_map(pt, va, end, pa, attrs);
        va += len;
        pa += len;
    }

    return va;
}
