// 页表内容管理

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

// // 简单地映射，参考 DIRECT_MAP_ADDR
// #define VIRT(x)         ((x) |  0xffff800000000000UL)
// #define PHYS(x)         ((x) & ~0xffff800000000000UL)


// 属性位
#define MMU_ATTRS   MMU_NX | MMU_US | MMU_RW





// 页描述符中，记录页表的表项数量，作为引用计数
// 页表层次：PML4 --> PDP --> PD --> PT


// 创建一个空的页表
uint64_t pagetbl_create() {
    uint64_t  pa = page_alloc();
    uint64_t *va = (uint64_t *)(pa | DIRECT_MAP_ADDR);
    bset(va, 0, PAGE_SIZE);
    return pa;
}

// 获取下一级页表，如果不存在则创建
uint64_t *get_subtbl(uint64_t *tbl, uint64_t idx) {
    ASSERT(NULL != tbl);
    ASSERT(idx < 512);

    if (tbl[idx] & MMU_P) {
        return (uint64_t *)((tbl[idx] & MMU_ADDR) | DIRECT_MAP_ADDR);
    }

    uint64_t  pa = page_alloc();
    uint64_t *va = (uint64_t *)(pa | DIRECT_MAP_ADDR);
    tbl[idx] = (pa & MMU_ADDR) | MMU_P | MMU_RW | MMU_RW;
    bset(va, 0, PAGE_SIZE);
    return va;
}




// 64-bit 工作模式下：CR0.PG=1，CR4.PAE=1，IA32_EFER.LME=1
// 此时有两种分页机制：
//  - cr4.LA57=0，使用四级分页，48-bit 线性地址映射到 52-bit 物理地址
//  - cr4.LA57=1，使用五级分页，57-bit 线性地址映射到 52-bit 物理地址（支持更大的物理内存）

// 最顶层的页表都是由 cr3 保存地址，但 cr3[11:0] 的取值有不同含义：
//  - cr4.PCID=0，包含两个属性位 PWT、PCD，控制访问页表时的缓存策略
//  - cr4.PCID=1，cr3 低 12-bit 表示当前的 PCID


// Intel 线性地址包含 protection key，位于最后一级页表条目符号扩展部分。
// 启用 protection key 的条件是 cr4.PKE=1，cr4.PKS=1



// 查询虚拟地址映射的物理地址，同时返回页面属性
// 多级页表中，各级表项都有属性位，不能只看最末一级
// 这部分行为要看 Intel 文档，AMD 文档说的不清楚
// 各级 U/S 都是 1，线性地址才是用户态可读的（特权代码一直可读）
// 各级 R/W 都是 1，线性地址才是用户态可读可写的（特权代码是否可写取决于 cr0.WP）
// 各级 NX 都是 0，线性地址才是可执行的

uint64_t mmu_translate(uint64_t tbl, uint64_t va, uint64_t *attrs) {
    uint64_t *pml4 = (uint64_t *)(tbl | DIRECT_MAP_ADDR);
    uint64_t pml4e = pml4[(va >> 39) & 0x1ff];
    if (0 == (pml4e & MMU_P)) {
        return INVALID_ADDR;
    }

    *attrs = pml4e & MMU_ATTRS;

    uint64_t *pdp = (uint64_t *)((pml4e & MMU_ADDR) | DIRECT_MAP_ADDR);
    uint64_t pdpe = pdp[(va >> 30) & 0x1ff];
    if (0 == (pdpe & MMU_P)) {
        return INVALID_ADDR;
    }

    *attrs &= (pdpe & MMU_ATTRS);

    if ((g_cpu_features & CPU_FEATURE_1G) && (pdpe & MMU_PS)) {
        ASSERT(0 == OFFSET_1G(pdpe & MMU_ADDR));
        return (pdpe & MMU_ADDR) | OFFSET_1G(va);
    }

    uint64_t *pd = (uint64_t *)((pdpe & MMU_ADDR) | DIRECT_MAP_ADDR);
    uint64_t pde = pd[(va >> 21) & 0x1ff];
    if (0 == (pde & MMU_P)) {
        return INVALID_ADDR;
    }

    *attrs &= (pde & MMU_ATTRS);

    if (pde & MMU_PS) {
        ASSERT(0 == OFFSET_2M(pde & MMU_ADDR));
        return (pde & MMU_ADDR) | OFFSET_2M(va);
    }

    uint64_t *pt = (uint64_t *)((pde & MMU_ADDR) | DIRECT_MAP_ADDR);
    uint64_t pte = pt[(va >> 12) & 0x1ff];
    if (0 == (pte & MMU_P)) {
        return INVALID_ADDR;
    }

    *attrs &= (pte & MMU_ATTRS);
    return (pte & MMU_ADDR) | OFFSET_4K(va);
}



//------------------------------------------------------------------------------
// 每一级页表都有自己的增删改查操作函数
// 上层页表的函数可能要调用下层页表结构的函数

// 末级页表建立映射关系
// 返回值表示建立了多少个页的映射关系，乘以页大小
// 也就是影射了多大的内存
uint64_t pt_map(uint64_t *pt, uint64_t va, uint64_t pa, uint64_t size, uint64_t attrs) {
    ASSERT(NULL != pt);
    ASSERT(0 == OFFSET_4K(va));
    ASSERT(0 == OFFSET_4K(pa));
    ASSERT(0 == OFFSET_4K(size));
    ASSERT(0 != size);
    ASSERT(0 == (attrs & MMU_ATTRS));

    int pti = (va >> 12) & 0x1ff;
    uint64_t off = 0;
    for (; (pti < 512) && (off < size); ++pti, off += PAGE_SIZE) {
        ASSERT(0 == (pt[pti] & MMU_P));
        pt[pti] = ((pa + off) & MMU_ADDR) | MMU_P | attrs;
    }

    return off;
}


uint64_t pd_map(uint64_t *pd, uint64_t va, uint64_t pa, uint64_t size, uint64_t attrs) {
    ASSERT(NULL != pd);
    ASSERT(0 == OFFSET_4K(va));
    ASSERT(0 == OFFSET_4K(pa));
    ASSERT(0 == OFFSET_4K(size));
    ASSERT(0 != size);
    ASSERT(0 == (attrs & MMU_ATTRS));

    uint64_t old_size = size;

    for (int pdi = (va >> 21) & 0x1ff; (pdi < 512) && size; ++pdi) {
        if ((0 == OFFSET_2M(va | pa)) && (size >= SIZE_2M)) {
            if (0 == (pd[pdi] & MMU_PS)) {
                // uint64_t sub = (pd[pdi] & MMU_ADDR) >> PAGE_SHIFT;
                // ASSERT(0 == g_pages[sub].ent_cnt);
                page_free(pd[pdi] & MMU_ADDR);
            }
            pd[pdi] = (pa & MMU_ADDR) | MMU_P | MMU_PS | attrs;
            va += SIZE_2M;
            pa += SIZE_2M;
            size -= SIZE_2M;
        } else {
            uint64_t len = pt_map(get_subtbl(pd, pdi), va, pa, size, attrs);
            va += len;
            pa += len;
            size -= len;
        }
    }

    return old_size - size;
}



#if 0

// 建立一段连续映射关系
// 如果已有映射，则覆盖，但不会执行 invlpg，导致缓存不会刷新
void mmu_map(uint64_t tbl, uint64_t va, uint64_t pa, uint64_t attrs, int n) {
    attrs &= MMU_ATTRS;

    uint64_t pml4i = (va >> 39) & 0x1ff;
    uint64_t pdpi  = (va >> 30) & 0x1ff;
    uint64_t pdi   = (va >> 21) & 0x1ff;
    uint64_t pti   = (va >> 12) & 0x1ff;

    uint64_t *pml4 = (uint64_t *)(tbl | DIRECT_MAP_ADDR);
    uint64_t *pdp;
    uint64_t *pd;
    uint64_t *pt;

    if (pml4[pml4i] & MMU_P) {
        pdp = (uint64_t *)(pml4[pml4i] & MMU_ADDR);
    } else {
        uint64_t sub = (uint64_t)page_alloc() << PAGE_SHIFT;
        pml4[pml4i] = (sub & MMU_ADDR) | MMU_P | attrs;
        pdp = (uint64_t *)(sub | DIRECT_MAP_ADDR);
        bset(pdp, 0, PAGE_SIZE);
    }

    // 如果可以使用 1G 大页，然而表项已经指向下一级的 pd
    // 那首先应该回收下一级页表，然后再创建新的条目

    if (pdp[pdpi] & MMU_P) {
        pd = (uint64_t *)(pdp[pdpi] & MMU_ADDR);
    } else if ((g_cpu_features & CPU_FEATURE_1G) && !OFFSET_1G(va | pa) && (n >= 0x40000)) {
        pdp[pdpi] = (pa & MMU_ADDR) | MMU_P | MMU_PS | attrs;
        va += 0x40000000;
        pa += 0x40000000;
        n  -= 0x40000;
    } else {
        uint64_t sub = (uint64_t)page_alloc() << PAGE_SHIFT;
        pdp[pdpi] = (sub & MMU_ADDR) | MMU_P | attrs;
        pd = (uint64_t *)(sub | DIRECT_MAP_ADDR);
        bset(pd, 0, PAGE_SIZE);
    }
}

#endif
