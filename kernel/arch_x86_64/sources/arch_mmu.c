// 页表内容管理

#include <arch_mmu.h>
#include <wheel.h>
#include <arch_cpu.h>
#include <page.h>
#include <str.h>




// different fields of virtual memory address
#define PML4T_SHIFT     39                     // page-map level-4 table
#define PDPT_SHIFT      30                     // page-directory-pointer table
#define PDT_SHIFT       21                     // page-directory table
#define PT_SHIFT        12                     // page table

// bits of a page entry
#define MMU_NX          0x8000000000000000UL    // (NX)  No Execute
#define MMU_ADDR        0x000ffffffffff000UL    // addr field
#define MMU_AVL         0x0000000000000e00UL    // AVL
#define MMU_G           0x0000000000000100UL    // (G)   Global
#define MMU_PS          0x0000000000000080UL    // (PS)  Page Size, is it 2M PDE?
#define MMU_D           0x0000000000000040UL    // (D)   Dirty
#define MMU_A           0x0000000000000020UL    // (A)   Accessed
#define MMU_PCD         0x0000000000000010UL    // (PCD) Page-level Cache Disable
#define MMU_PWT         0x0000000000000008UL    // (PWD) Page-level WriteThough
#define MMU_US          0x0000000000000004UL    // (U/S) User Supervisor
#define MMU_RW          0x0000000000000002UL    // (R/W) Read Write
#define MMU_P           0x0000000000000001UL    // (P)   Present
#define MMU_PAT_4K      0x0000000000000080UL    // (PAT) for 4K PTE
#define MMU_PAT_2M      0x0000000000001000UL    // (PAT) for 2M PDE

#define SIZE_4K 0x1000UL
#define SIZE_2M 0x200000UL
#define SIZE_1G 0x40000000UL

// alignment check
#define OFFSET_4K(x)    ((x) & (SIZE_4K - 1))
#define OFFSET_2M(x)    ((x) & (SIZE_2M - 1))
#define OFFSET_1G(x)    ((x) & (SIZE_1G - 1))

// 属性位
#define MMU_ATTRS   (MMU_NX | MMU_US | MMU_RW)





// 页描述符中，记录页表的表项数量，作为引用计数
// 页表层次：PML4 --> PDP --> PD --> PT


// 64-bit 工作模式下：CR0.PG=1，CR4.PAE=1，IA32_EFER.LME=1
// 此时有两种分页机制：
//  - cr4.LA57=0，使用四级分页，48-bit 线性地址映射到 52-bit 物理地址
//  - cr4.LA57=1，使用五级分页，57-bit 线性地址映射到 52-bit 物理地址（支持更大的物理内存）

// 最顶层的页表都是由 cr3 保存地址，但 cr3[11:0] 的取值有不同含义：
//  - cr4.PCID=0，包含两个属性位 PWT、PCD，控制访问页表时的缓存策略
//  - cr4.PCID=1，cr3 低 12-bit 表示当前的 PCID

// 各级 U/S 都是 1，线性地址才是用户态可读的（特权代码一直可读）
// 各级 R/W 都是 1，线性地址才是用户态可读可写的（特权代码是否可写取决于 cr0.WP）
// 各级 NX 都是 0，线性地址才是可执行的

// Intel 线性地址包含 protection key，位于最后一级页表条目符号扩展部分。
// 启用 protection key 的条件是 cr4.PKE=1，cr4.PKS=1





// 创建一张空的页表，返回物理地址
static uint64_t alloc_table() {
    uint64_t pa = page_alloc(PT_PGTBL);
    page_info_t *info = page_block_info(pa);
    info->ent_num = 0;

    uint64_t *va = (uint64_t *)(pa | DIRECT_MAP_ADDR);
    bset(va, 0, PAGE_SIZE);
    return pa;
}

// 获取下一级页表，如果不存在则创建
uint64_t get_subtbl(uint64_t *tbl, uint64_t idx) {
    ASSERT(NULL != tbl);
    ASSERT(idx < 512);

    if (tbl[idx] & MMU_P) {
        return tbl[idx] & MMU_ADDR;
    }

    uint64_t pa = alloc_table();
    tbl[idx] = (pa & MMU_ADDR) | MMU_P | MMU_RW | MMU_RW;
    return pa;
}




// 创建一套新的页表
uint64_t mmu_table_create() {
    return alloc_table();
}


// 查询虚拟地址映射的物理地址，同时返回页面属性
// 多级页表中，各级表项都有属性位，不能只看最末一级
// 这部分行为要看 Intel 文档，AMD 文档说的不清楚
uint64_t mmu_translate(uint64_t tbl, uint64_t va, mmu_attr_t *attrs) {
    uint64_t *pml4 = (uint64_t *)(tbl | DIRECT_MAP_ADDR);
    uint64_t pml4e = pml4[(va >> 39) & 0x1ff];
    if (0 == (pml4e & MMU_P)) {
        return INVALID_ADDR;
    }

    *attrs = 0;
    *attrs |= (pml4e & MMU_US) ? MMU_USER  : 0;
    *attrs |= (pml4e & MMU_RW) ? MMU_WRITE : 0;
    *attrs |= (pml4e & MMU_NX) ? 0 : MMU_EXEC;

    uint64_t *pdp = (uint64_t *)((pml4e & MMU_ADDR) | DIRECT_MAP_ADDR);
    uint64_t pdpe = pdp[(va >> 30) & 0x1ff];
    if (0 == (pdpe & MMU_P)) {
        return INVALID_ADDR;
    }

    *attrs &= (pdpe & MMU_US) ? MMU_USER  : 0;
    *attrs &= (pdpe & MMU_RW) ? MMU_WRITE : 0;
    *attrs &= (pdpe & MMU_NX) ? 0 : MMU_EXEC;

    if ((g_cpu_features & CPU_FEATURE_1G) && (pdpe & MMU_PS)) {
        ASSERT(0 == OFFSET_1G(pdpe & MMU_ADDR));
        return (pdpe & MMU_ADDR) | OFFSET_1G(va);
    }

    uint64_t *pd = (uint64_t *)((pdpe & MMU_ADDR) | DIRECT_MAP_ADDR);
    uint64_t pde = pd[(va >> 21) & 0x1ff];
    if (0 == (pde & MMU_P)) {
        return INVALID_ADDR;
    }

    *attrs &= (pde & MMU_US) ? MMU_USER  : 0;
    *attrs &= (pde & MMU_RW) ? MMU_WRITE : 0;
    *attrs &= (pde & MMU_NX) ? 0 : MMU_EXEC;

    if (pde & MMU_PS) {
        ASSERT(0 == OFFSET_2M(pde & MMU_ADDR));
        return (pde & MMU_ADDR) | OFFSET_2M(va);
    }

    uint64_t *pt = (uint64_t *)((pde & MMU_ADDR) | DIRECT_MAP_ADDR);
    uint64_t pte = pt[(va >> 12) & 0x1ff];
    if (0 == (pte & MMU_P)) {
        return INVALID_ADDR;
    }

    *attrs &= (pte & MMU_US) ? MMU_USER  : 0;
    *attrs &= (pte & MMU_RW) ? MMU_WRITE : 0;
    *attrs &= (pte & MMU_NX) ? 0 : MMU_EXEC;
    return (pte & MMU_ADDR) | OFFSET_4K(va);
}





//------------------------------------------------------------------------------
// 逐级删除页表
//------------------------------------------------------------------------------

// 末级页表，没有更次一级，可以直接释放页面
void pt_free(uint64_t pt) {
    ASSERT(0 == OFFSET_4K(pt));
    page_free(pt);
}

// PDE 可能指向 PT，也可能是 2M 表项
void pd_free(uint64_t pd) {
    ASSERT(0 == OFFSET_4K(pd));

    // 如果页表内容为空，则无需遍历，可以直接释放
    page_info_t *info = page_block_info(pd);
    if (0 == info->ent_num) {
        page_free(pd);
    }

    uint64_t *tbl = (uint64_t *)(pd | DIRECT_MAP_ADDR);
    for (int i = 0; i < 512; ++i) {
        if ((tbl[i] & MMU_P) && !(tbl[i] & MMU_PS)) {
            pt_free(tbl[i] & MMU_ADDR);
        }
    }
}

// PDPE 可能指向 PD，也可能是 1G 表项
void pdp_free(uint64_t pdp) {
    ASSERT(0 == OFFSET_4K(pdp));

    // 如果有效条目数量为零，则无需遍历，直接释放页面
    page_info_t *info = page_block_info(pdp);
    if (0 == info->ent_num) {
        page_free(pdp);
    }

    uint64_t *tbl = (uint64_t *)(pdp | DIRECT_MAP_ADDR);
    for (int i = 0; i < 512; ++i) {
        if ((tbl[i] & MMU_P) && !(tbl[i] & MMU_PS)) {
            pd_free(tbl[i] & MMU_ADDR);
        }
    }
}

void pml4_free(uint64_t pml4) {
    ASSERT(0 == OFFSET_4K(pml4));

    page_info_t *info = page_block_info(pml4);
    if (0 == info->ent_num) {
        page_free(pml4);
    }

    uint64_t *tbl = (uint64_t *)(pml4 | DIRECT_MAP_ADDR);
    for (int i = 0; i < 512; ++i) {
        if (tbl[i] & MMU_P) {
            pdp_free(tbl[i] & MMU_ADDR);
        }
    }
}

void mmu_table_delete(uint64_t tbl) {
    pml4_free(tbl);
}


//------------------------------------------------------------------------------
// 建立映射关系
//------------------------------------------------------------------------------

// 各级函数返回成功建立映射关系的大小
// 可能覆盖现有 mapping

uint64_t pt_map(uint64_t pt, uint64_t va, uint64_t end, uint64_t pa, uint64_t attrs) {
    ASSERT(0 == OFFSET_4K(pt));
    ASSERT(0 == OFFSET_4K(va));
    ASSERT(0 == OFFSET_4K(end));
    ASSERT(va <= end);
    ASSERT(0 == OFFSET_4K(pa));
    ASSERT(0 == (attrs & ~MMU_ATTRS));

    page_info_t *info = page_block_info(pt);
    uint64_t *tbl = (uint64_t *)(pt | DIRECT_MAP_ADDR);

    uint64_t start = va;
    for (int i = (va >> 12) & 0x1ff; (i < 512) && (va < end); ++i) {
        if (0 == (tbl[i] & MMU_P)) {
            ++info->ent_num;
        }
        tbl[i] = (pa & MMU_ADDR) | MMU_P | attrs;
        va += PAGE_SIZE;
        pa += PAGE_SIZE;
    }

    return va - start;
}

uint64_t pd_map(uint64_t pd, uint64_t va, uint64_t end, uint64_t pa, uint64_t attrs) {
    ASSERT(0 == OFFSET_4K(pd));
    ASSERT(0 == OFFSET_4K(va));
    ASSERT(0 == OFFSET_4K(end));
    ASSERT(va <= end);
    ASSERT(0 == OFFSET_4K(pa));
    ASSERT(0 == (attrs & ~MMU_ATTRS));

    page_info_t *info = page_block_info(pd);
    uint64_t *tbl = (uint64_t *)(pd | DIRECT_MAP_ADDR);

    uint64_t start = va;
    for (int i = (va >> 21) & 0x1ff; (i < 512) && (va < end); ++i) {
        // 判断能否使用 2M 表项，首选按 2M 映射
        if ((0 == OFFSET_2M(va | pa)) && (va + SIZE_2M <= end)) {
            if (0 == (tbl[i] & MMU_P)) {
                ++info->ent_num;
            } else if (0 == (tbl[i] & MMU_PS)) {
                pt_free(tbl[i] & MMU_ADDR); // 将原本映射的下级 PT 删除
            }

            tbl[i] = (pa & MMU_ADDR) | MMU_P | MMU_PS | attrs;
            va += SIZE_2M;
            pa += SIZE_2M;
            continue;
        }

        // 无法使用 2M 表项，需要建立下一级表结构
        uint64_t pt = tbl[i] & MMU_ADDR;

        if (0 == (tbl[i] & MMU_P)) {
            // 原本没有表项，需要创建新的
            ++info->ent_num;
            pt = alloc_table();
        } else if ((tbl[i] & MMU_PS) && (end - va < SIZE_2M)) {
            // 原本是 2M，且 mapping 没有完整覆盖这 2M，还要保留一部分
            ASSERT(0 == OFFSET_2M(va | pt));
            uint64_t remain = pt + (end - va);
            pt = alloc_table();
            pt_map(pt, end, va + SIZE_2M, remain, tbl[i] & MMU_ATTRS);
        }

        tbl[i] = (pt & MMU_ADDR) | MMU_P | MMU_RW | MMU_US; // 不是末级表项，使用最宽松的权限
        uint64_t len = pt_map(pt, va, end, pa, attrs);
        va += len;
        pa += len;
    }

    return va - start;
}

uint64_t pdp_map(uint64_t pdp, uint64_t va, uint64_t end, uint64_t pa, uint64_t attrs) {
    ASSERT(0 == OFFSET_4K(pdp));
    ASSERT(0 == OFFSET_4K(va));
    ASSERT(0 == OFFSET_4K(end));
    ASSERT(va <= end);
    ASSERT(0 == OFFSET_4K(pa));
    ASSERT(0 == (attrs & ~MMU_ATTRS));

    page_info_t *info = page_block_info(pdp);
    uint64_t *tbl = (uint64_t *)(pdp | DIRECT_MAP_ADDR);

    uint64_t start = va;
    for (int i = (va >> 30) & 0x1ff; (i < 512) && (va < end); ++i) {
        // 判断能否使用 1G 表项
        if ((g_cpu_features & CPU_FEATURE_1G) && (0 == OFFSET_1G(va | pa)) && (va + SIZE_1G <= end)) {
            if (0 == (tbl[i] & MMU_P)) {
                ++info->ent_num;
            } else if (0 == (tbl[i] & MMU_PS)) {
                pd_free(tbl[i] & MMU_ADDR);
            }

            tbl[i] = (pa & MMU_ADDR) | MMU_P | MMU_PS | attrs;
            va += SIZE_1G;
            pa += SIZE_1G;
            continue;
        }

        // 无法使用 1G 表项，需要建立下一级表结构
        uint64_t pd = tbl[i] & MMU_ADDR;

        if (0 == (tbl[i] & MMU_P)) {
            ++info->ent_num;
            pd = alloc_table();
        } else if ((tbl[i] & MMU_PS) && (end - va < SIZE_1G)) {
            // 原有表项为 1G，且原本 mapping 还要保留一部分
            ASSERT(0 == OFFSET_1G(va | pd));
            uint64_t remain = pd + (end - va);
            pd = alloc_table();
            pd_map(pd, end, va + SIZE_1G, remain, tbl[i] & MMU_ATTRS);
        }

        tbl[i] = (pd & MMU_ADDR) | MMU_P | MMU_RW | MMU_US; // 不是末级表项，使用最宽松的权限
        uint64_t len = pd_map(pd, va, end, pa, attrs);
        va += len;
        pa += len;
    }

    return va - start;
}

uint64_t pml4_map(uint64_t pml4, uint64_t va, uint64_t end, uint64_t pa, uint64_t attrs) {
    ASSERT(0 == OFFSET_4K(pml4));
    ASSERT(0 == OFFSET_4K(va));
    ASSERT(0 == OFFSET_4K(end));
    ASSERT(va <= end);
    ASSERT(0 == OFFSET_4K(pa));
    ASSERT(0 == (attrs & ~MMU_ATTRS));

    page_info_t *info = page_block_info(pml4);
    uint64_t *tbl = (uint64_t *)(pml4 | DIRECT_MAP_ADDR);

    uint64_t start = va;
    for (int i = (va >> 39) & 0x1ff; (i < 512) && (va < end); ++i) {
        uint64_t pdp = tbl[i] & MMU_ADDR;

        if (0 == (tbl[i] & MMU_P)) {
            ++info->ent_num;
            pdp = alloc_table();
        }

        tbl[i] = (pdp & MMU_ADDR) | MMU_P | MMU_US | MMU_RW;
        uint64_t len = pdp_map(pdp, va, end, pa, attrs);
        va += len;
        pa += len;
    }

    return va - start;
}

void mmu_map(uint64_t tbl, uint64_t va, uint64_t end, uint64_t pa, mmu_attr_t attrs) {
    ASSERT(0 == OFFSET_4K(tbl));
    ASSERT(0 == OFFSET_4K(va));
    ASSERT(0 == OFFSET_4K(end));
    ASSERT(0 == OFFSET_4K(pa));

    uint64_t prot = 0;
    prot |= (attrs & MMU_USER) ? MMU_US : 0;
    prot |= (attrs & MMU_WRITE) ? MMU_RW : 0;
    prot |= (attrs & MMU_EXEC) && (g_cpu_features & CPU_FEATURE_NX) ? 0 : MMU_NX;

    uint64_t mapped = pml4_map(tbl, va, end, pa, prot);
    ASSERT(va + mapped == end);
}


//------------------------------------------------------------------------------
// 删除映射
//------------------------------------------------------------------------------

// uint64_t pt_unmap(uint64_t pt, uint64_t va, uint64_t end) {
//     //
// }

