#include <arch_api.h>
#include <page.h>
#include <kstring.h>
#include <debug.h>
#include <cpu/features.h>



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
#define MMU_PAT_2M  0x0000000000001000UL    // (PAT) for 2M PDE

#define MMU_ATTRS (MMU_NX | MMU_US | MMU_RW) // 访问权限

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
    // logk("alloc page-tbl %llx (%d)\n", pa >> 12, tag);
    g_pages[pa >> PAGE_SHIFT].ent_num = 0;
    kmemset((char*)pa + DIRECT_MAP_ADDR, 0, PAGE_SIZE);
    return pa;
}

static void free_table(uint64_t tbl) {
    // logk("free page-tbl %llx\n", tbl >> 12);
    page_free(tbl);
}



// 各级页表函数：
// - XXX_map(tbl, va, end, pa) 操作一张页表及其子表，将最多 [va,end) 映射到 pa，返回新建映射的长度
// - XXX_unmap(tbl, va, end) 操作一张页表及其子表，将最多 [va,end) 清除映射，返回清除部分的长度
// 如果虚拟地址范围 [va,end) 超过了页表 tbl 的管理范围，则不操作超出的部分
//
// 上一级页表可以递归调用子表的函数
// 优先使用 2M-page、1G-page 大页
// map 和 unmap 调用可以不对称，但不建议
// 我们可以处理拆分大页的情况，但不建议


//------------------------------------------------------------------------------
// page table, 每个表项控制 4K
//------------------------------------------------------------------------------

// 返回映射结束的虚拟地址
static uint64_t pt_map(uint64_t pt, uint64_t va, uint64_t end, uint64_t pa, uint64_t bits) {
    uint64_t *tbl = (uint64_t*)(DIRECT_MAP_ADDR + pt);
    page_t *info = &g_pages[pt >> PAGE_SHIFT];

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

static uint64_t pd_map(uint64_t pd, uint64_t va, uint64_t end, uint64_t pa, uint64_t bits) {
    ASSERT(0 == OFFSET_4K(pd));

    uint64_t *tbl = (uint64_t*)(DIRECT_MAP_ADDR + pd);
    page_t *info = &g_pages[pd >> PAGE_SHIFT];

    uint64_t start = va;
    for (int i = IDX_2M(va); (i < 512) && (va < end); ++i) {
        if (!OFFSET_2M(va | pa) && (va + SIZE_2M <= end)) {
            if (0 == (tbl[i] & MMU_P)) {
                ++info->ent_num;
            } else if (0 == (tbl[i] & MMU_PS)) {
                // 将下一级页表回收，改为 2M 条目，总条目数量不变
                pt_free(tbl[i] & MMU_ADDR);
            }

            tbl[i] = (pa & MMU_ADDR) | MMU_P | MMU_PS | bits;
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
            if (va2m != va) {
                pt_map(pt, va2m, va, pa2m, tbl[i] & MMU_ATTRS);
            }
            if (end < va2m + SIZE_2M) {
                size_t end_pa = pa2m + (end - va2m);
                pt_map(pt, end, va2m + SIZE_2M, end_pa, tbl[i] & MMU_ATTRS);
            }
        }

        tbl[i] = (pt & MMU_ADDR) | MMU_P | MMU_RW | MMU_US; // 不是末级表项，使用最宽松的权限
        uint64_t len = pt_map(pt, va, end, pa, bits);
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
            if (va2m != va) {
                pt_map(pt, va2m, va, pa2m, tbl[i] & MMU_ATTRS);
                va = va2m + SIZE_2M;
            }
            if (end < va2m + SIZE_2M) {
                size_t end_pa = pa2m + (end - va2m);
                pt_map(pt, end, va2m + SIZE_2M, end_pa, tbl[i] & MMU_ATTRS);
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

static uint64_t pdp_map(uint64_t pdp, uint64_t va, uint64_t end, uint64_t pa, uint64_t bits) {
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

            tbl[i] = (pa & MMU_ADDR) | MMU_P | MMU_PS | bits;
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
            // 将 1G-page 拆分，如果头尾还剩 mapping，需要重新映射
            uint64_t va1g = va - OFFSET_1G(va);
            uint64_t pa1g = pd;
            ASSERT(0 == OFFSET_1G(pa1g));

            pd = alloc_table(__LINE__);
            if (va1g != va) {
                pd_map(pd, va1g, va, pa1g, tbl[i] & MMU_ATTRS);
            }
            if (end < va1g + SIZE_1G) {
                size_t end_pa = pa1g + (end - va1g);
                pd_map(pd, end, va1g + SIZE_1G, end_pa, tbl[i] & MMU_ATTRS);
            }
        }

        tbl[i] = (pd & MMU_ADDR) | MMU_P | MMU_RW | MMU_US; // 不是末级表项，使用最宽松的权限
        uint64_t len = pd_map(pd, va, end, pa, bits);
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
            if (va1g != va) {
                pd_map(pd, va1g, va, pa1g, tbl[i] & MMU_ATTRS);
                va = va1g + SIZE_1G;
            }
            if (end < va1g + SIZE_1G) {
                size_t end_pa = pa1g + (end - va1g);
                pd_map(pd, end, va1g + SIZE_1G, end_pa, tbl[i] & MMU_ATTRS);
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

static uint64_t pml4_map(uint64_t pml4, uint64_t va, uint64_t end, uint64_t pa, uint64_t bits) {
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
        uint64_t len = pdp_map(pdp, va, end, pa, bits);
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

// 对外公开的分页属性和 amd64 页属性不同
static mmu_attr_t bits_to_attrs(uint64_t bits) {
    mmu_attr_t attrs = MMU_NONE;
    attrs |= (MMU_RW & bits) ? MMU_WRITE : 0;
    attrs |= (MMU_NX & bits) ? 0 : MMU_EXEC;
    attrs |= (MMU_US & bits) ? MMU_USER : 0;
    return attrs;
}

static uint64_t attrs_to_bits(mmu_attr_t attrs) {
    uint64_t bits = 0;
    bits |= (attrs & MMU_USER) ? MMU_US : 0;
    bits |= (attrs & MMU_WRITE) ? MMU_RW : 0;
    bits |= (attrs & MMU_EXEC) && (g_cpu_features & CPU_FEATURE_NX) ? 0 : MMU_NX;
    return bits;
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
        *attrs = bits_to_attrs(mmu_bits);
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
        *attrs = bits_to_attrs(mmu_bits);
        return (pde & MMU_ADDR) | OFFSET_2M(va);
    }

    uint64_t *pt = (uint64_t*)((pde & MMU_ADDR) + DIRECT_MAP_ADDR);
    uint64_t pte = pt[IDX_4K(va)];
    if (0 == (pte & MMU_P)) {
        return 0;
    }

    *attrs = bits_to_attrs(mmu_bits & pte);
    return (pte & MMU_ADDR) | OFFSET_4K(va);
}

void mmu_map(size_t tbl, size_t va, size_t end, size_t pa, mmu_attr_t attrs) {
    ASSERT(0 == OFFSET_4K(tbl));
    ASSERT(0 == OFFSET_4K(va));
    ASSERT(0 == OFFSET_4K(end));
    ASSERT(0 == OFFSET_4K(pa));

    size_t len = pml4_map(tbl, va, end, pa, attrs_to_bits(attrs));
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
