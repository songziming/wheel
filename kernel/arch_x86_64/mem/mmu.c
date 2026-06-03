#include <arch_api.h>
#include <page.h>
#include <task.h>
#include <kstring.h>
#include <spin.h>
#include <debug.h>
#include <cpu/features.h>

#include <console.h>
#include <kshell.h>


// 页表项各字段
#define MMU_NX      0x8000000000000000UL    // (NX)  No Execute
#define MMU_ADDR    0x000ffffffffff000UL    // addr field
#define MMU_AVL     0x0000000000000e00UL    // AVL
#define MMU_G       0x0000000000000100UL    // (G)   Global
#define MMU_PS      0x0000000000000080UL    // (PS)  Page Size, is it 2M PDE?
#define MMU_D       0x0000000000000040UL    // (D)   Dirty
#define MMU_A       0x0000000000000020UL    // (A)   Accessed
#define MMU_PCD     0x0000000000000010UL    // (PCD) Page-level Cache Disable
#define MMU_PWT     0x0000000000000008UL    // (PWD) Page-level WriteThough
#define MMU_US      0x0000000000000004UL    // (U/S) User Supervisor
#define MMU_RW      0x0000000000000002UL    // (R/W) Read Write
#define MMU_P       0x0000000000000001UL    // (P)   Present
#define MMU_PAT_4K  0x0000000000000080UL    // (PAT) for 4K PTE
#define MMU_PAT_2M  0x0000000000001000UL    // (PAT) for 2M PDE / 1G PDPE

// 各级页表通用的属性位（NX, US, RW, PCD, PWT 位置一致）
// PAT 位在各级别位置不同（4K: bit 7, 2M/1G: bit 12），由各级函数各自处理
#define MMU_ATTRS (MMU_NX | MMU_US | MMU_RW | MMU_PCD | MMU_PWT)

// 从虚拟地址拆分出各级页表项的编号
#define IDX_4K(va)      (int)((va >> 12) & 0x1ff)
#define IDX_2M(va)      (int)((va >> 21) & 0x1ff)
#define IDX_1G(va)      (int)((va >> 30) & 0x1ff)
#define IDX_PML4(va)    (int)((va >> 39) & 0x1ff)

#define SIZE_4K (1UL << 12)
#define SIZE_2M (1UL << 21)
#define SIZE_1G (1UL << 30)

#define OFFSET_4K(x)    ((x) & (SIZE_4K - 1))
#define OFFSET_2M(x)    ((x) & (SIZE_2M - 1))
#define OFFSET_1G(x)    ((x) & (SIZE_1G - 1))


#define INVLPG(va)  ASMV("invlpg (%0)" :: "r"(va) : "memory")

// 单元测试，模仿虚拟地址和物理地址的转换
#if defined(UNIT_TEST)
extern uint64_t g_direct_map_base;
#undef DIRECT_MAP_ADDR
#define DIRECT_MAP_ADDR g_direct_map_base
#endif


// 分配一张页表
static uint64_t alloc_table(int tag UNUSED) {
    uint64_t pa = PAGE_ALLOC(0, PT_PGTBL);
    if (0 == pa) {
        panic("cannot alloc for mmu");
        return 0;
    }
    g_pages[pa >> PAGE_SHIFT].ent_num = 0;
    kmemset((char*)pa + DIRECT_MAP_ADDR, 0, PAGE_SIZE);
    return pa;
}

static void free_table(uint64_t tbl) {
    page_free(tbl);
}



// 各级页表函数：
// - XXX_map(tbl, va, end, pa, bits, pat) 将 [va,end) 映射到 pa，返回新建映射的长度
// - XXX_unmap(tbl, va, end) 清除 [va,end) 的映射，返回清除的长度
// bits 包含各级通用的属性位（NX/US/RW/PCD/PWT），pat 为 PAT 标志，
// 各层级自行将 PAT 位置于正确的 bit（4K: bit 7, 2M/1G: bit 12）


//------------------------------------------------------------------------------
// page table, 每个表项控制 4K
//------------------------------------------------------------------------------

static uint64_t pt_map(uint64_t pt, uint64_t va, uint64_t end, uint64_t pa, uint64_t bits, int pat) {
    uint64_t *tbl = (uint64_t*)(DIRECT_MAP_ADDR + pt);
    page_t *info = &g_pages[pt >> PAGE_SHIFT];

    if (pat) {
        bits |= MMU_PAT_4K;
    }

    uint64_t start = va;
    for (int i = IDX_4K(va); (i < 512) && (va + 0x1000 <= end); ++i) {
        if (0 == (tbl[i] & MMU_P)) {
            ++info->ent_num;
        }
        tbl[i] = (pa & MMU_ADDR) | MMU_P | bits;
        va += SIZE_4K;
        pa += SIZE_4K;
    }

    return va - start;
}


static uint64_t pt_unmap(uint64_t pt, uint64_t va, uint64_t end) {
    uint64_t *tbl = (uint64_t*)(DIRECT_MAP_ADDR + pt);
    page_t *info = &g_pages[pt >> PAGE_SHIFT];

    uint64_t start = va;
    for (int i = i = IDX_4K(va); (i < 512) && (va + 0x1000 <= end); ++i) {
        if (tbl[i] & MMU_P) {
            --info->ent_num;
        }
        tbl[i] = 0;
        INVLPG(va);
        va += SIZE_4K;
    }

    ASSERT(va <= end);
    return va - start;
}

static void pt_free(uint64_t pt) {
    free_table(pt);
}


//------------------------------------------------------------------------------
// page directory，表项可以指向 PT，也可以直接映射 2M
//------------------------------------------------------------------------------

static uint64_t pd_map(uint64_t pd, uint64_t va, uint64_t end, uint64_t pa, uint64_t bits, int pat) {
    ASSERT(0 == OFFSET_4K(pd));

    uint64_t *tbl = (uint64_t*)(DIRECT_MAP_ADDR + pd);
    page_t *info = &g_pages[pd >> PAGE_SHIFT];

    uint64_t start = va;
    for (int i = IDX_2M(va); (i < 512) && (va < end); ++i) {
        if (!OFFSET_2M(va | pa) && (va + SIZE_2M <= end)) {
            if (0 == (tbl[i] & MMU_P)) {
                ++info->ent_num;
            } else if (0 == (tbl[i] & MMU_PS)) {
                pt_free(tbl[i] & MMU_ADDR);
            }

            uint64_t pde = (pa & MMU_ADDR) | MMU_P | MMU_PS | bits;
            if (pat) {
                pde |= MMU_PAT_2M;
            }
            tbl[i] = pde;
            va += SIZE_2M;
            pa += SIZE_2M;
            continue;
        }

        // 无法使用 2M-mapping，需要建立下一级页表
        uint64_t pt = tbl[i] & MMU_ADDR;

        if (0 == (tbl[i] & MMU_P)) {
            ++info->ent_num;
            pt = alloc_table(__LINE__);
        } else if (tbl[i] & MMU_PS) {
            // 将现有的 2M-page 拆分，如果头尾还剩 mapping，需要重新映射
            uint64_t va2m = va - OFFSET_2M(va);
            uint64_t pa2m = pt;
            ASSERT(0 == OFFSET_2M(pa2m));

            pt = alloc_table(__LINE__);
            uint64_t split_bits = tbl[i] & MMU_ATTRS;
            int      split_pat  = (tbl[i] & MMU_PAT_2M) != 0;
            if (va2m != va) {
                pt_map(pt, va2m, va, pa2m, split_bits, split_pat);
            }
            if (end < va2m + SIZE_2M) {
                size_t end_pa = pa2m + (end - va2m);
                pt_map(pt, end, va2m + SIZE_2M, end_pa, split_bits, split_pat);
            }
        }

        tbl[i] = (pt & MMU_ADDR) | MMU_P | MMU_RW | MMU_US;
        uint64_t len = pt_map(pt, va, end, pa, bits, pat);
        va += len;
        pa += len;
    }

    return va - start;
}

uint64_t pd_unmap(uint64_t pd, uint64_t va, uint64_t end) {
    ASSERT(0 == OFFSET_4K(pd));
    ASSERT(0 == OFFSET_4K(va));
    ASSERT(0 == OFFSET_4K(end));
    ASSERT(va <= end);

    uint64_t *tbl = (uint64_t*)(pd + DIRECT_MAP_ADDR);
    page_t *info = &g_pages[pd >> PAGE_SHIFT];

    uint64_t start = va;
    for (int i = (va >> 21) & 0x1ff; (i < 512) && (va < end); ++i) {
        if (0 == (tbl[i] & MMU_P)) {
            va +=   SIZE_2M;
            va &= ~(SIZE_2M - 1);
            continue;
        }

        // 如果 unmap 范围涵盖了完整 2M
        if ((0 == OFFSET_2M(va)) && (va + SIZE_2M <= end)) {
            if (0 == (tbl[i] & MMU_PS)) {
                pt_free(tbl[i] & MMU_ADDR);
            }
            --info->ent_num;
            tbl[i] = 0;
            INVLPG(va);
            va += SIZE_2M;
            continue;
        }

        // unmap 范围不能覆盖完整的 2M
        uint64_t pt = tbl[i] & MMU_ADDR;

        // 原本是 2M 页，unmap 没有完整清除这 2M，还要保留一些
        if (tbl[i] & MMU_PS) {
            uint64_t va2m = va - OFFSET_2M(va);
            uint64_t pa2m = pt;
            ASSERT(0 == OFFSET_2M(pa2m));

            pt = alloc_table(__LINE__);
            uint64_t split_bits = tbl[i] & MMU_ATTRS;
            int      split_pat  = (tbl[i] & MMU_PAT_2M) != 0;
            if (va2m != va) {
                pt_map(pt, va2m, va, pa2m, split_bits, split_pat);
                va = va2m + SIZE_2M;
            }
            if (end < va2m + SIZE_2M) {
                size_t end_pa = pa2m + (end - va2m);
                pt_map(pt, end, va2m + SIZE_2M, end_pa, split_bits, split_pat);
                va = end;
            }
            tbl[i] = (pt & MMU_ADDR) | MMU_P | MMU_US | MMU_RW;
            INVLPG(va2m);
        } else {
            va += pt_unmap(pt, va, end);
        }

        // 如果次级页表内容为空，则可以将页表删除
        if (0 == g_pages[pt >> PAGE_SHIFT].ent_num) {
            pt_free(pt);
            tbl[i] = 0;
            --info->ent_num;
        }
    }

    ASSERT(va <= end);
    return va - start;
}

static void pd_free(uint64_t pd) {
    ASSERT(0 == OFFSET_4K(pd));

    uint64_t *tbl = (uint64_t*)(pd + DIRECT_MAP_ADDR);
    for (int i = 0; i < 512; ++i) {
        if ((tbl[i] & MMU_P) && !(tbl[i] & MMU_PS)) {
            pt_free(tbl[i] & MMU_ADDR);
        }
    }

    free_table(pd);
}


//------------------------------------------------------------------------------
// page directory pointer，表项可以指向 PD，也可以直接映射 1G
//------------------------------------------------------------------------------

static uint64_t pdp_map(uint64_t pdp, uint64_t va, uint64_t end, uint64_t pa, uint64_t bits, int pat) {
    ASSERT(0 == OFFSET_4K(pdp));
    ASSERT(0 == OFFSET_4K(va));
    ASSERT(0 == OFFSET_4K(end));
    ASSERT(va <= end);
    ASSERT(0 == OFFSET_4K(pa));
    ASSERT(0 == (bits & ~MMU_ATTRS));

    uint64_t *tbl = (uint64_t*)(pdp + DIRECT_MAP_ADDR);
    page_t *info = &g_pages[pdp >> PAGE_SHIFT];

    uint64_t start = va;
    for (int i = IDX_1G(va); (i < 512) && (va < end); ++i) {
        // 判断能否使用 1G 表项
        if ((g_cpu_features & CPU_FEATURE_1G) && (0 == OFFSET_1G(va | pa)) && (va + SIZE_1G <= end)) {
            if (0 == (tbl[i] & MMU_P)) {
                ++info->ent_num;
            } else if (0 == (tbl[i] & MMU_PS)) {
                pd_free(tbl[i] & MMU_ADDR);
            }

            uint64_t pdpe = (pa & MMU_ADDR) | MMU_P | MMU_PS | bits;
            if (pat) {
                pdpe |= MMU_PAT_2M;
            }
            tbl[i] = pdpe;
            va += SIZE_1G;
            pa += SIZE_1G;
            continue;
        }

        // 无法使用 1G 表项，需要建立下一级表结构
        uint64_t pd = tbl[i] & MMU_ADDR;

        if (0 == (tbl[i] & MMU_P)) {
            ++info->ent_num;
            pd = alloc_table(__LINE__);
        } else if (tbl[i] & MMU_PS) {
            // 将 1G-page 拆分
            uint64_t va1g = va - OFFSET_1G(va);
            uint64_t pa1g = pd;
            ASSERT(0 == OFFSET_1G(pa1g));

            pd = alloc_table(__LINE__);
            uint64_t split_bits = tbl[i] & MMU_ATTRS;
            int      split_pat  = (tbl[i] & MMU_PAT_2M) != 0;
            if (va1g != va) {
                pd_map(pd, va1g, va, pa1g, split_bits, split_pat);
            }
            if (end < va1g + SIZE_1G) {
                size_t end_pa = pa1g + (end - va1g);
                pd_map(pd, end, va1g + SIZE_1G, end_pa, split_bits, split_pat);
            }
        }

        tbl[i] = (pd & MMU_ADDR) | MMU_P | MMU_RW | MMU_US;
        uint64_t len = pd_map(pd, va, end, pa, bits, pat);
        va += len;
        pa += len;
    }

    return va - start;
}

uint64_t pdp_unmap(uint64_t pdp, uint64_t va, uint64_t end) {
    ASSERT(0 == OFFSET_4K(pdp));
    ASSERT(0 == OFFSET_4K(va));
    ASSERT(0 == OFFSET_4K(end));
    ASSERT(va <= end);

    uint64_t *tbl = (uint64_t*)(pdp + DIRECT_MAP_ADDR);
    page_t *info = &g_pages[pdp >> PAGE_SHIFT];

    uint64_t start = va;
    for (int i = IDX_1G(va); (i < 512) && (va < end); ++i) {
        if (0 == (tbl[i] & MMU_P)) {
            va +=   SIZE_1G;
            va &= ~(SIZE_1G - 1);
            continue;
        }

        // 如果 unmap 范围涵盖了完整 1G
        if ((0 == OFFSET_1G(va)) && (va + SIZE_1G <= end)) {
            if (0 == (tbl[i] & MMU_PS)) {
                pd_free(tbl[i] & MMU_ADDR);
            }
            tbl[i] = 0;
            --info->ent_num;
            INVLPG(va);
            va += SIZE_1G;
            continue;
        }

        // unmap 范围不能覆盖完整的 1G
        uint64_t pd = tbl[i] & MMU_ADDR;

        // 原本是 1G 页，unmap 没有完整清除这 1G，还要保留一些
        if (tbl[i] & MMU_PS) {
            uint64_t va1g = va - OFFSET_1G(va);
            uint64_t pa1g = pd;
            ASSERT(0 == OFFSET_1G(pa1g));

            pd = alloc_table(__LINE__);
            uint64_t split_bits = tbl[i] & MMU_ATTRS;
            int      split_pat  = (tbl[i] & MMU_PAT_2M) != 0;
            if (va1g != va) {
                pd_map(pd, va1g, va, pa1g, split_bits, split_pat);
                va = va1g + SIZE_1G;
            }
            if (end < va1g + SIZE_1G) {
                size_t end_pa = pa1g + (end - va1g);
                pd_map(pd, end, va1g + SIZE_1G, end_pa, split_bits, split_pat);
                va = end;
            }

            tbl[i] = (pd & MMU_ADDR) | MMU_P | MMU_US | MMU_RW;
            INVLPG(va1g);
        } else {
            va += pd_unmap(pd, va, end);
        }

        // 如果下一级 PD 有效表项为零，则删除
        if (0 == g_pages[pd >> PAGE_SHIFT].ent_num) {
            pd_free(pd);
            tbl[i] = 0;
            --info->ent_num;
        }
    }

    ASSERT(va <= end);
    return va - start;
}

static void pdp_free(uint64_t pdp) {
    ASSERT(0 == OFFSET_4K(pdp));

    uint64_t *tbl = (uint64_t*)(pdp + DIRECT_MAP_ADDR);
    for (int i = 0; i < 512; ++i) {
        if ((tbl[i] & MMU_P) && !(tbl[i] & MMU_PS)) {
            pd_free(tbl[i] & MMU_ADDR);
        }
    }

    free_table(pdp);
}


//------------------------------------------------------------------------------
// PML4
//------------------------------------------------------------------------------

static uint64_t pml4_map(uint64_t pml4, uint64_t va, uint64_t end, uint64_t pa, uint64_t bits, int pat) {
    ASSERT(0 == OFFSET_4K(pml4));
    ASSERT(0 == OFFSET_4K(va));
    ASSERT(0 == OFFSET_4K(end));
    ASSERT(va <= end);
    ASSERT(0 == OFFSET_4K(pa));
    ASSERT(0 == (bits & ~MMU_ATTRS));

    uint64_t *tbl = (uint64_t*)(pml4 + DIRECT_MAP_ADDR);
    page_t *info = &g_pages[pml4 >> PAGE_SHIFT];

    uint64_t start = va;
    for (int i = IDX_PML4(va); (i < 512) && (va < end); ++i) {
        uint64_t pdp = tbl[i] & MMU_ADDR;

        if (0 == (tbl[i] & MMU_P)) {
            ++info->ent_num;
            pdp = alloc_table(__LINE__);
        }

        tbl[i] = (pdp & MMU_ADDR) | MMU_P | MMU_US | MMU_RW;
        uint64_t len = pdp_map(pdp, va, end, pa, bits, pat);
        va += len;
        pa += len;
    }

    return va - start;
}

static uint64_t pml4_unmap(uint64_t pml4, uint64_t va, uint64_t end) {
    ASSERT(0 == OFFSET_4K(pml4));
    ASSERT(0 == OFFSET_4K(va));
    ASSERT(0 == OFFSET_4K(end));
    ASSERT(va <= end);

    uint64_t *tbl = (uint64_t*)(pml4 + DIRECT_MAP_ADDR);
    page_t *info = &g_pages[pml4 >> PAGE_SHIFT];

    uint64_t start = va;
    for (int i = IDX_PML4(va); (i < 512) && (va < end); ++i) {
        if (0 == (tbl[i] & MMU_P)) {
            va +=   SIZE_1G * 512;
            va &= ~(SIZE_1G * 512 - 1);
            continue;
        }

        uint64_t pdp = tbl[i] & MMU_ADDR;
        va += pdp_unmap(pdp, va, end);

        // 需要判断这个 PDP 是不是内核地址范围
        // 如果是内核空间的 PDP，即使有效元素为零也不能删除
        // 因为内核 PDP 被所有进程的 PML4 引用
        if ((i < 256) && (0 == g_pages[pdp >> PAGE_SHIFT].ent_num)) {
            pdp_free(pdp);
            tbl[i] = 0;
            --info->ent_num;
        }
    }

    ASSERT(va <= end);
    return va - start;
}

static void pml4_free(uint64_t pml4) {
    ASSERT(0 == OFFSET_4K(pml4));

    uint64_t *tbl = (uint64_t*)(pml4 + DIRECT_MAP_ADDR);
    for (int i = 0; i < 512; ++i) {
        // 如果带有 global 标记，说明被所有进程共享，不能删除
        if ((tbl[i] & MMU_P) && !(tbl[i] & MMU_G)) {
            pdp_free(tbl[i] & MMU_ADDR);
        }
    }

    free_table(pml4);
}


//------------------------------------------------------------------------------
// public funcs
//------------------------------------------------------------------------------

// 创建一个全新页表，不包含内核空间
size_t mmu_create() {
    return alloc_table(__LINE__);
}

void mmu_delete(size_t tbl) {
    pml4_free(tbl);
}

static mmu_attr_t bits_to_attrs(uint64_t bits, int pat) {
    mmu_attr_t attrs = MMU_NONE;
    attrs |= (MMU_RW & bits) ? MMU_WRITE : 0;
    attrs |= (MMU_NX & bits) ? 0 : MMU_EXEC;
    attrs |= (MMU_US & bits) ? MMU_USER : 0;

    int pat_idx = (pat ? 4 : 0) | ((bits & MMU_PCD) ? 2 : 0) | ((bits & MMU_PWT) ? 1 : 0);
    switch (pat_idx) {
    case 0:                     break;  // WB
    case 1: attrs |= MMU_WT;    break;  // WT
    case 3: attrs |= MMU_UC;    break;  // UC
    case 4: attrs |= MMU_WC;    break;  // WC
    }
    return attrs;
}

// 模拟硬件的地址转换流程，获取 va 映射的 pa
size_t mmu_translate(size_t tbl, size_t va, mmu_attr_t *attrs) {
    ASSERT(0 == OFFSET_4K(tbl));
    ASSERT(NULL != attrs);

    uint64_t *pml4 = (uint64_t*)(tbl + DIRECT_MAP_ADDR);
    uint64_t pml4e = pml4[IDX_PML4(va)];
    if (0 == (pml4e & MMU_P)) {
        return 0;
    }

    uint64_t *pdp = (uint64_t*)((pml4e & MMU_ADDR) + DIRECT_MAP_ADDR);
    uint64_t pdpe = pdp[IDX_1G(va)];
    if (0 == (pdpe & MMU_P)) {
        return 0;
    }

    // 如果是 1G 大页
    uint64_t mmu_bits = pml4e & pdpe;
    if ((g_cpu_features & CPU_FEATURE_1G) && (pdpe & MMU_PS)) {
        *attrs = bits_to_attrs(mmu_bits & MMU_ATTRS, 0 != (pdpe & MMU_PAT_2M));
        return (pdpe & MMU_ADDR) | OFFSET_1G(va);
    }

    uint64_t *pd = (uint64_t*)((pdpe & MMU_ADDR) + DIRECT_MAP_ADDR);
    uint64_t pde = pd[IDX_2M(va)];
    if (0 == (pde & MMU_P)) {
        return 0;
    }

    // 如果是 2M 大页
    mmu_bits &= pde;
    if (pde & MMU_PS) {
        *attrs = bits_to_attrs(mmu_bits & MMU_ATTRS, 0 != (pde & MMU_PAT_2M));
        return (pde & MMU_ADDR) | OFFSET_2M(va);
    }

    uint64_t *pt = (uint64_t*)((pde & MMU_ADDR) + DIRECT_MAP_ADDR);
    uint64_t pte = pt[IDX_4K(va)];
    if (0 == (pte & MMU_P)) {
        return 0;
    }

    *attrs = bits_to_attrs((mmu_bits & pte) & MMU_ATTRS, 0 != (pte & MMU_PAT_4K));
    return (pte & MMU_ADDR) | OFFSET_4K(va);
}

void mmu_map(size_t tbl, size_t va, size_t end, size_t pa, mmu_attr_t attrs) {
    ASSERT(0 == OFFSET_4K(tbl));
    ASSERT(0 == OFFSET_4K(va));
    ASSERT(0 == OFFSET_4K(end));
    ASSERT(0 == OFFSET_4K(pa));

    uint64_t bits = 0;
    bits |= (attrs & MMU_USER) ? MMU_US : 0;
    bits |= (attrs & MMU_WRITE) ? MMU_RW : 0;
    bits |= (attrs & MMU_EXEC) && (g_cpu_features & CPU_FEATURE_NX) ? 0 : MMU_NX;

    int pat = 0;
    switch (attrs & 0x300) {
    case MMU_WC: pat = 1;                    break;
    case MMU_WT: bits |= MMU_PWT;            break;
    case MMU_UC: bits |= MMU_PCD | MMU_PWT;  break;
    }

    size_t len = pml4_map(tbl, va, end, pa, bits, pat);
    ASSERT(va + len == end);
    (void)len;
}

void mmu_unmap(size_t tbl, size_t va, size_t end) {
    ASSERT(0 == OFFSET_4K(tbl));
    ASSERT(0 == OFFSET_4K(va));
    ASSERT(0 == OFFSET_4K(end));

    size_t len = pml4_unmap(tbl, va, end);
    ASSERT(va + len == end);
    (void)len;
}



//------------------------------------------------------------------------------
// 多核清除 TLB 缓存
//------------------------------------------------------------------------------

// TLB 不像 L1/L2 cache，硬件不会自动保证一致
// OS 需要发送 IPI，所有 CPU 都执行 invlpg

static spin_t g_shootdown_lock = SPIN_INIT;
static _Atomic int g_shootdown_cnt;
static size_t g_shootdown_vstart;
static size_t g_shootdown_vend;

void on_ipi_invlpg() {
    for (uint64_t va = g_shootdown_vstart; va < g_shootdown_vend; va += PAGE_SIZE) {
        ASMV("invlpg (%0)" :: "r"(va) : "memory");
    }
    atomic_fetch_sub(&g_shootdown_cnt, 1);
}

// 让其他 cpu 清除映射，必须在任务里执行，不能在中断调用
// 执行之后，其他 cpu 都不再持有这段 va 的映射，只有自身 cpu 有映射
// 接下来，当前 cpu 可以放心地删除任务栈，放心地执行 vmspace_remove
// vmspace_remove 函数中，会执行 invlpg 删除当前 cpu 的映射
void tlb_shootdown(size_t vstart, size_t vend) {
    ASSERT(0 == cpu_int_depth());

    preempt_lock();
    raw_spin_take(&g_shootdown_lock);
    atomic_store(&g_shootdown_cnt, cpu_count() - 1);
    g_shootdown_vstart = vstart;
    g_shootdown_vend = vend;
    arch_send_ipi(IPI_ALL_EXCLUDING_SELF, VEC_IPI_INVLPG); // all except self

    // 等待过程保持中断开启，当时禁用抢占
    // 如果另一个 cpu 发来 shootdown-IPI，我们也能处理
    while (atomic_load(&g_shootdown_cnt) > 0) {
        cpu_pause();
    }
    raw_spin_give(&g_shootdown_lock);
    preempt_unlock();
}

//------------------------------------------------------------------------------
// 计算某个地址映射的物理地址
//------------------------------------------------------------------------------

void show_mapping(int argc, char *argv[]) {
    if (argc < 2) {
        console_printf("usage: %s VIRT_ADDR\n", argv[0]);
        return;
    }

    size_t va = str2num(argv[1]);

    mmu_attr_t attrs;
    size_t pa = mmu_translate(g_kernel_vm.table, va, &attrs);
    console_printf("physical address 0x%zx\n", pa);

    console_printf("attributes:");
    if (attrs & MMU_USER) {
        console_printf(" user");
    }
    if (attrs & MMU_WRITE) {
        console_printf(" write");
    }
    if (attrs & MMU_EXEC) {
        console_printf(" exec");
    }
    console_printf("\n");
}

KSHELL_CMD("page", show_mapping);
