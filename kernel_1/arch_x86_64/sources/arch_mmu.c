// 页表编辑操作
// 需要用到物理页分配

#include <arch_interface.h>
#include <arch_debug.h>
#include <liba/cpuid.h>
#include <debug.h>
#include <page.h>
#include <libk.h>


// 64-bit 模型采用四级页表结构：
//  - PML4
//  - PDP
//  - PD
//  - PT

#define PML4I_SHIFT 39      // page-map level-4 offset
#define PDPI_SHIFT  30      // page-directory-pointer offset
#define PDI_SHIFT   21      // page-directory offset
#define PTI_SHIFT   12      // page-table offset

#define SIZE_2M     0x200000L
#define SIZE_1G     0x40000000L
#define OFFSET_4K   (PAGE_SIZE - 1)
#define OFFSET_2M   (SIZE_2M - 1)
#define OFFSET_1G   (SIZE_1G - 1)


// 页表项各属性位掩码
// 每一级页表项都有属性位，实际权限是各级属性与运算得出
// 我们将非末级页表项属性完全开启，实际效果由末级决定
#define MMU_NX          0x8000000000000000UL    // (NX)  No Execute
#define MMU_ADDR        0x000ffffffffff000UL    // 下一级页表地址
#define MMU_AVL         0x0000000000000e00UL    // AVL
#define MMU_G           0x0000000000000100UL    // (G)   Global
#define MMU_PS          0x0000000000000080UL    // (PS)  Page Size, 2M or 1G
#define MMU_D           0x0000000000000040UL    // (D)   Dirty
#define MMU_A           0x0000000000000020UL    // (A)   Accessed
#define MMU_PCD         0x0000000000000010UL    // (PCD) Page-level Cache Disable
#define MMU_PWT         0x0000000000000008UL    // (PWD) Page-level WriteThough
#define MMU_US          0x0000000000000004UL    // (U/S) User Supervisor
#define MMU_RW          0x0000000000000002UL    // (R/W) Read Write
#define MMU_P           0x0000000000000001UL    // (P)   Present
#define MMU_PAT_4K      0x0000000000000080UL    // (PAT) for 4K PTE
#define MMU_PAT_2M      0x0000000000001000UL    // (PAT) for 2M PDE

// 我们关心的属性位
#define ATTR_MASK (MMU_RW|MMU_US|MMU_NX|MMU_G)


// 将平台无关的页属性转换为 PAE 页属性位图
static uint64_t flags2attrs(uint32_t flags) {
    uint64_t attrs = 0;

    if (PAGE_WRITE & flags) {
        attrs |= MMU_RW;
    }
    if (!(PAGE_EXEC & flags) && (g_cpu_features & CPU_FEATURE_NX)) {
        attrs |= MMU_NX;
    }
    if (PAGE_USER & flags) {
        attrs |= MMU_US;
    }
    if (PAGE_GLOBAL & flags) {
        attrs |= MMU_G;
    }

    return attrs;
}

// 末级页表项转换为页属性
static uint32_t attrs2flags(uint64_t ent) {
    uint32_t flags = 0;

    if (MMU_RW & ent) {
        flags |= PAGE_WRITE;
    }
    if (!(MMU_NX & ent)) {
        flags |= PAGE_EXEC;
    }
    if (MMU_US & ent) {
        flags |= PAGE_USER;
    }
    if (MMU_G & ent) {
        flags |= PAGE_GLOBAL;
    }

    return flags;
}

// 获取子表，如果不存在则创建
static uint64_t *get_subtable_or_create(uint64_t *tbl, int idx, uint64_t attrs) {
    ASSERT(idx >= 0);
    ASSERT(idx < 512);

    if (tbl[idx] & MMU_P) {
        return (uint64_t *)(DIRECT_MAP_BASE + (MMU_ADDR & tbl[idx]));
    }

    tbl[idx] = (uint64_t)page_alloc_or_die(PT_PAGETBL) << PAGE_SHIFT;
    tbl[idx] |= MMU_P | attrs;

    uint64_t *sub = (uint64_t *)(DIRECT_MAP_BASE + (tbl[idx] & MMU_ADDR));
    memset(sub, 0, PAGE_SIZE);
    return sub;
}


//------------------------------------------------------------------------------
// 创建空的页表
//------------------------------------------------------------------------------

size_t mmu_create() {
    uint64_t tbl = (uint64_t)page_alloc_or_die(PT_PAGETBL) << PAGE_SHIFT;
    uint64_t *pml4 = (uint64_t *)(DIRECT_MAP_BASE + tbl);
    memset(pml4, 0, PAGE_SIZE);
    return tbl;
}


//------------------------------------------------------------------------------
// 模拟硬件的分页流程，寻找映射的物理页
//------------------------------------------------------------------------------

// 如果这个虚拟地址没有被映射，则返回 INVALID_ADDR，同时返回页面属性
size_t mmu_translate(size_t tbl, size_t va, uint32_t *flags) {
    ASSERT(0 == (tbl & (PAGE_SIZE - 1)));
    ASSERT(NULL != flags);

    uint16_t pml4i = (va >> PML4I_SHIFT) & 0x1ff;
    uint16_t pdpi  = (va >> PDPI_SHIFT)  & 0x1ff;
    uint16_t pdi   = (va >> PDI_SHIFT)   & 0x1ff;
    uint16_t pti   = (va >> PTI_SHIFT)   & 0x1ff;

    uint64_t *pml4 = (uint64_t *)(tbl + DIRECT_MAP_BASE);
    uint64_t pml4e = pml4[pml4i];
    if (0 == (MMU_P & pml4e)) {
        return INVALID_ADDR;
    }

    uint64_t *pdp = (uint64_t *)((MMU_ADDR & pml4e) + DIRECT_MAP_BASE);
    uint64_t pdpe = pdp[pdpi];
    if (0 == (MMU_P & pdpe)) {
        return INVALID_ADDR;
    }

    // 是否为 1G-page
    if (MMU_PS & pdpe) {
        ASSERT(0 == (OFFSET_1G & MMU_ADDR & pdpe));
        *flags = attrs2flags(pdpe);
        return (MMU_ADDR & pdpe) + (OFFSET_1G & va);
    }

    uint64_t *pd = (uint64_t *)((MMU_ADDR & pdpe) + DIRECT_MAP_BASE);
    uint64_t pde = pd[pdi];
    if (0 == (MMU_P & pde)) {
        return INVALID_ADDR;
    }

    // 是否为 2M-page
    if (MMU_PS & pde) {
        ASSERT(0 == (OFFSET_2M & MMU_ADDR & pde));
        *flags = attrs2flags(pde);
        return (MMU_ADDR & pde) + (OFFSET_2M & va);
    }

    uint64_t *pt = (uint64_t *)((MMU_ADDR & pde) + DIRECT_MAP_BASE);
    uint64_t pte = pt[pti];
    if (0 == (MMU_P & pte)) {
        return INVALID_ADDR;
    }

    *flags = attrs2flags(pte);
    return (MMU_ADDR & pte) + (OFFSET_4K & va);
}


//------------------------------------------------------------------------------
// 建立映射关系
//------------------------------------------------------------------------------

// 将 va 映射到 pa，如果已存在则更新，不存在则创建
static void mmu_map_4k(uint64_t tbl, uint64_t va, uint64_t pa, uint64_t attrs) {
    ASSERT(0 == (tbl & OFFSET_4K));
    ASSERT(0 == (va  & OFFSET_4K));
    ASSERT(0 == (pa  & OFFSET_4K));

    // 各级页表的表项编号
    uint16_t pml4i = (va >> PML4I_SHIFT) & 0x1ff;
    uint16_t pdpi  = (va >> PDPI_SHIFT)  & 0x1ff;
    uint16_t pdi   = (va >> PDI_SHIFT)   & 0x1ff;
    uint16_t pti   = (va >> PTI_SHIFT)   & 0x1ff;

    // 获取各级表结构
    uint64_t *pml4 = (uint64_t *)(DIRECT_MAP_BASE + tbl);
    uint64_t *pdp = get_subtable_or_create(pml4, pml4i, MMU_P|MMU_RW|MMU_US);
    uint64_t *pd  = get_subtable_or_create(pdp,  pdpi,  MMU_P|MMU_RW|MMU_US);
    uint64_t *pt  = get_subtable_or_create(pd,   pdi,   MMU_P|MMU_RW|MMU_US);

    pt[pti] = (pa & MMU_ADDR) | MMU_P | attrs;
}

static void mmu_map_2m(uint64_t tbl, uint64_t va, uint64_t pa, uint64_t attrs) {
    ASSERT(0 == (tbl & OFFSET_4K));
    ASSERT(0 == (va  & OFFSET_2M));
    ASSERT(0 == (pa  & OFFSET_2M));

    // 各级页表的表项编号
    uint16_t pml4i = (va >> PML4I_SHIFT) & 0x1ff;
    uint16_t pdpi  = (va >> PDPI_SHIFT)  & 0x1ff;
    uint16_t pdi   = (va >> PDI_SHIFT)   & 0x1ff;

    // 获取各级表结构
    uint64_t *pml4 = (uint64_t *)(DIRECT_MAP_BASE + tbl);
    uint64_t *pdp = get_subtable_or_create(pml4, pml4i, MMU_P|MMU_RW|MMU_US);
    uint64_t *pd  = get_subtable_or_create(pdp,  pdpi,  MMU_P|MMU_RW|MMU_US);

    pd[pdi] = (pa & MMU_ADDR) | MMU_P | MMU_PS | attrs;
}

static void mmu_map_1g(uint64_t tbl, uint64_t va, uint64_t pa, uint64_t attrs) {
    ASSERT(0 == (tbl & OFFSET_4K));
    ASSERT(0 == (va  & OFFSET_1G));
    ASSERT(0 == (pa  & OFFSET_1G));

    // 各级页表的表项编号
    uint16_t pml4i = (va >> PML4I_SHIFT) & 0x1ff;
    uint16_t pdpi  = (va >> PDPI_SHIFT)  & 0x1ff;

    // 获取各级表结构
    uint64_t *pml4 = (uint64_t *)(DIRECT_MAP_BASE + tbl);
    uint64_t *pdp = get_subtable_or_create(pml4, pml4i, MMU_P|MMU_RW|MMU_US);

    pdp[pdpi] = (pa & MMU_ADDR) | MMU_P | MMU_PS | attrs;
}

static void mmu_map_range(size_t tbl, size_t va, size_t pa, size_t npages, uint64_t attrs) {
    ASSERT(0 == (tbl & OFFSET_4K));
    ASSERT(0 == (va  & OFFSET_4K));
    ASSERT(0 == (pa  & OFFSET_4K));

    uint64_t end_va = va + (npages << PAGE_SHIFT);
    uint64_t phi = va ^ pa; // 虚拟地址和物理地址的相位差

    uint64_t start_2m = (va + OFFSET_2M) & ~OFFSET_2M; // 向后 2M 对齐
    uint64_t start_1g = (va + OFFSET_1G) & ~OFFSET_1G; // 向后 1G 对齐

    if ((OFFSET_2M & phi) || (start_2m > end_va)) {
        start_2m = end_va;  // 无法使用 2M-page
        start_1g = end_va;  // 显然也无法使用 1G-page
    } else if (!(CPU_FEATURE_1G & g_cpu_features) || (OFFSET_1G & phi) || (start_1g > end_va)) {
        start_1g = end_va;  // 无法使用 1G-page
    }

    for (; va < start_2m; va += PAGE_SIZE, pa += PAGE_SIZE) {
        mmu_map_4k(tbl, va, pa, attrs);
    }
    for (; va + OFFSET_2M < start_1g; va += SIZE_2M, pa += SIZE_2M) {
        mmu_map_2m(tbl, va, pa, attrs);
    }
    for (; va + OFFSET_1G < end_va; va += SIZE_1G, pa += SIZE_1G) {
        mmu_map_1g(tbl, va, pa, attrs);
    }
    for (; va + OFFSET_2M < end_va; va += SIZE_2M, pa += SIZE_2M) {
        mmu_map_2m(tbl, va, pa, attrs);
    }
    for (; va < end_va; va += PAGE_SIZE, pa += PAGE_SIZE) {
        mmu_map_4k(tbl, va, pa, attrs);
    }
}

void mmu_map(size_t tbl, size_t va, size_t pa, size_t npages, uint32_t flags) {
    uint64_t attrs = flags2attrs(flags);
    mmu_map_range(tbl, va, pa, npages, attrs);
}


//------------------------------------------------------------------------------
// 清除映射关系
//------------------------------------------------------------------------------

// TODO 目前的 unmap 实现不会释放空的子表

// 一次调用清除一个页表项的映射，可能为 4K、2M、1G
// 如果遇到 1G、2M 条目超过了 unmap 范围，需要将剩余部分重新映射
uint64_t mmu_unmap_range(uint64_t tbl, uint64_t va, uint64_t size) {
    ASSERT(0 == (tbl & OFFSET_4K));
    ASSERT(0 == (va  & OFFSET_4K));

    uint16_t pml4i = (va >> PML4I_SHIFT) & 0x1ff;
    uint16_t pdpi  = (va >> PDPI_SHIFT)  & 0x1ff;
    uint16_t pdi   = (va >> PDI_SHIFT)   & 0x1ff;
    uint16_t pti   = (va >> PTI_SHIFT)   & 0x1ff;

    uint64_t *pml4 = (uint64_t *)(DIRECT_MAP_BASE + tbl);
    if (0 == (MMU_P & pml4[pml4i])) {
        return SIZE_1G * 512;
    }

    uint64_t *pdp = (uint64_t *)(DIRECT_MAP_BASE + (MMU_ADDR & pml4[pml4i]));
    if (0 == (MMU_P & pdp[pdpi])) {
        return SIZE_1G;
    }

    // 遇到了 1G-page
    if (MMU_PS & pdp[pdpi]) {
        uint64_t pa = MMU_ADDR & pdp[pdpi];
        uint64_t attrs = ATTR_MASK & pdp[pdpi];
        ASSERT(0 == (va & OFFSET_1G));
        ASSERT(0 == (pa & OFFSET_1G));

        pdp[pdpi] = 0;
        if (size >= SIZE_1G) {
            return SIZE_1G;
        }

        uint64_t rest_pages = (SIZE_1G - size) >> PAGE_SHIFT;
        mmu_map_range(tbl, va + size, pa + size, rest_pages, attrs);
        return size;
    }

    uint64_t *pd = (uint64_t *)(DIRECT_MAP_BASE + (MMU_ADDR & pdp[pdpi]));
    if (0 == (pd[pdi] & MMU_P)) {
        return SIZE_2M;
    }

    // 遇到了 2M-page
    if (pd[pdi] & MMU_PS) {
        uint64_t pa = MMU_ADDR & pdp[pdpi];
        uint64_t attrs = ATTR_MASK & pdp[pdpi];
        ASSERT(0 == (va & OFFSET_2M));
        ASSERT(0 == (pa & OFFSET_2M));

        pd[pdi] = 0;
        if (size >= SIZE_2M) {
            return SIZE_2M;
        }

        uint64_t rest_pages = (SIZE_2M - size) >> PAGE_SHIFT;
        mmu_map_range(tbl, va + size, pa + size, rest_pages, attrs);
        return size;
    }

    uint64_t *pt = (uint64_t *)(DIRECT_MAP_BASE + (MMU_ADDR & pd[pdi]));
    pt[pti] = 0;
    return PAGE_SIZE;
}


// 将页表项设为无效，flush 表示是否执行 invlpg 指令
void mmu_unmap(size_t tbl, size_t va, size_t npages, flush_flag_t flush) {
    ASSERT(0 == (tbl & OFFSET_4K));
    ASSERT(0 == (va  & OFFSET_4K));

    uint64_t end_va = va + (npages << PAGE_SHIFT);

    while (va < end_va) {
        va += mmu_unmap_range(tbl, va, end_va - va);
        if (FLUSH_TLB == flush) {
            __asm__("invlpg (%0)" :: "r"(va));
        }
    }
}


//------------------------------------------------------------------------------
// 显示页表内容
//------------------------------------------------------------------------------

static void mmu_show_map(uint64_t va, uint64_t pa, uint64_t size, uint64_t attr, int nitems) {
    if (0 == size) {
        return;
    }

    char mod[4];
    mod[0] = (MMU_RW & attr) ? 'W' : '-';
    mod[1] = (MMU_NX & attr) ? '-' : 'X';
    mod[2] = (MMU_US & attr) ? 'U' : '-';
    mod[3] = (MMU_G  & attr) ? 'G' : '-';

    dbg_print("va:0x%016lx~0x%016lx --> pa:0x%016lx, %.4s, %d items\n",
            va, va + size, pa, mod, nitems);
}

// 打印映射关系
void mmu_show(uint64_t tbl) {
    uint64_t prev_va   = 0; // 前一段映射的起始虚拟地址
    uint64_t prev_pa   = 0; // 前一段映射的起始物理地址
    uint64_t prev_size = 0; // 前一段映射的大小
    uint64_t prev_attr = 0;
    int      nitems    = 0; // 涉及多少条目

    uint64_t *pml4 = (uint64_t *)(DIRECT_MAP_BASE + tbl);
    uint64_t va4 = 0;
    for (int i = 0; i < 512; ++i, va4 += (SIZE_1G << 9)) {
        if (256 == i) {
            // 跳过 canonical hole
            va4 += 0xffff000000000000;
        }

        if (0 == (pml4[i] & MMU_P)) {
            mmu_show_map(prev_va, prev_pa, prev_size, prev_attr, nitems);
            prev_va   = 0;
            prev_pa   = 0;
            prev_size = 0;
            prev_attr = 0;
            nitems    = 0;
            continue;
        }

        uint64_t *pdp = (uint64_t *)(DIRECT_MAP_BASE + (pml4[i] & MMU_ADDR));
        uint64_t va3 = va4;
        for (int j = 0; j < 512; ++j, va3 += SIZE_1G) {
            if (0 == (pdp[j] & MMU_P)) {
                mmu_show_map(prev_va, prev_pa, prev_size, prev_attr, nitems);
                prev_va   = 0;
                prev_pa   = 0;
                prev_size = 0;
                prev_attr = 0;
                nitems    = 0;
                continue;
            }

            if (pdp[j] & MMU_PS) { // 1G
                if ((va3 == prev_va + prev_size) &&
                    ((pdp[j] & MMU_ADDR) == prev_pa + prev_size) &&
                    ((pdp[j] & ATTR_MASK) == prev_attr)) {
                    prev_size += SIZE_1G;
                    ++nitems;
                } else {
                    mmu_show_map(prev_va, prev_pa, prev_size, prev_attr, nitems);
                    prev_va   = va3;
                    prev_pa   = pdp[j] & MMU_ADDR;
                    prev_size = SIZE_1G;
                    prev_attr = pdp[j] & ATTR_MASK;
                    nitems    = 1;
                }
                continue;
            }

            uint64_t *pd = (uint64_t *)(DIRECT_MAP_BASE + (pdp[j] & MMU_ADDR));
            uint64_t va2 = va3;
            for (int k = 0; k < 512; ++k, va2 += SIZE_2M) {
                if (0 == (pd[k] & MMU_P)) {
                    mmu_show_map(prev_va, prev_pa, prev_size, prev_attr, nitems);
                    prev_va   = 0;
                    prev_pa   = 0;
                    prev_size = 0;
                    prev_attr = 0;
                    nitems    = 0;
                    continue;
                }

                if (pd[k] & MMU_PS) { // 2M
                    if ((va2 == prev_va + prev_size) &&
                        ((pd[k] & MMU_ADDR) == prev_pa + prev_size) &&
                        ((pd[k] & ATTR_MASK) == prev_attr)) {
                        prev_size += SIZE_2M;
                        ++nitems;
                    } else {
                        mmu_show_map(prev_va, prev_pa, prev_size, prev_attr, nitems);
                        prev_va   = va2;
                        prev_pa   = pd[k] & MMU_ADDR;
                        prev_size = SIZE_2M;
                        prev_attr = pd[k] & ATTR_MASK;
                        nitems    = 1;
                    }
                    continue;
                }

                uint64_t *pt = (uint64_t *)(DIRECT_MAP_BASE + (pd[k] & MMU_ADDR));
                uint64_t va = va2;
                for (int l = 0; l < 512; ++l, va += PAGE_SIZE) {
                    if (0 == (pt[l] & MMU_P)) {
                        mmu_show_map(prev_va, prev_pa, prev_size, prev_attr, nitems);
                        prev_va   = 0;
                        prev_pa   = 0;
                        prev_size = 0;
                        prev_attr = 0;
                        nitems    = 0;
                        continue;
                    }

                    if ((va == prev_va + prev_size) &&
                        ((pt[l] & MMU_ADDR) == prev_pa + prev_size) &&
                        ((pt[l] & ATTR_MASK) == prev_attr)) {
                        prev_size += PAGE_SIZE;
                        ++nitems;
                    } else {
                        mmu_show_map(prev_va, prev_pa, prev_size, prev_attr, nitems);
                        prev_va   = va;
                        prev_pa   = pt[l] & MMU_ADDR;
                        prev_size = PAGE_SIZE;
                        prev_attr = pt[l] & ATTR_MASK;
                        nitems    = 1;
                    }
                }
            }
        }
    }

    mmu_show_map(prev_va, prev_pa, prev_size, prev_attr, nitems);
}
