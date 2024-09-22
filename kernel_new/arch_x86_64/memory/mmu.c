// 页表内容管理
// 页表操作没有自旋锁保护


#include <arch_intf.h> // 页表函数定义在 arch api 里
#include <arch_impl.h>
#include <generic/cpufeatures.h>

#include <memory/page.h>
#include <library/string.h>
#include <library/debug.h>


// table          PML4 --> PDP --> PD --> PT
// entry map size           1G     2M     4K

// 混合使用不同大小的页

// 内核 PML4[256:] 指向内核 PDP
// 进程 PML4[256:] 的内核地址部分也指向内核 PDP
// 如果内核 PDP 或更下级页表发生更新，每个进程的页表自动更新
// 如果内核 PML4 更新，进程页表不会自动更新，访问时可能发生 page fault，此时再更新进程 PML4

// 内核 PDP 为空也不能删除，因为内核 PDP 被所有进程的 PML4 引用

// unmap 操作的一般都是当前页表，必须执行 invlpg


// 内核页表，pml4 的后半部分需要一开始就固定下来，所有进程共享内核部分的映射
static CONST uint64_t g_kernel_cr3 = 0;


//------------------------------------------------------------------------------
// 页表项各字段掩码
//------------------------------------------------------------------------------

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

#define SIZE_4K 0x1000UL
#define SIZE_2M 0x200000UL
#define SIZE_1G 0x40000000UL

#define OFFSET_4K(x)    ((x) & (SIZE_4K - 1))
#define OFFSET_2M(x)    ((x) & (SIZE_2M - 1))
#define OFFSET_1G(x)    ((x) & (SIZE_1G - 1))



// 创建一张空的页表，返回物理地址
static uint64_t alloc_table() {
    uint64_t pa = page_block_alloc(0, PT_PGTBL);
    if (0 == pa) {
        log("fatal: cannot allocate page for page table!\n");
        cpu_halt();
    }

    page_info_t *info = page_block_info(pa);
    info->ent_num = 0;

    // uint64_t *va = VIRT(pa);
    memset((char *)pa + DIRECT_MAP_ADDR, 0, PAGE_SIZE);
    return pa;
}



// 分别实现各级页表的函数：
//  - free 删除这一级页表
//  - map 建立映射，返回映射的大小
//  - unmap 取消映射，返回取消映射范围的结束位置


//------------------------------------------------------------------------------
// PT，每个表项对应 4K
//------------------------------------------------------------------------------

// 末级页表，没有更次一级，可以直接释放页面
static void pt_free(uint64_t pt) {
    ASSERT(0 == OFFSET_4K(pt));
    page_block_free(pt);
}

static uint64_t pt_map(uint64_t pt, uint64_t va, uint64_t end, uint64_t pa, uint64_t attrs) {
    ASSERT(0 == OFFSET_4K(pt));
    ASSERT(0 == OFFSET_4K(va));
    ASSERT(0 == OFFSET_4K(end));
    ASSERT(va <= end);
    ASSERT(0 == OFFSET_4K(pa));
    ASSERT(0 == (attrs & ~MMU_ATTRS));

    page_info_t *info = page_block_info(pt);
    uint64_t *tbl = (uint64_t *)(pt + DIRECT_MAP_ADDR);

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

static uint64_t pt_unmap(uint64_t pt, uint64_t va, uint64_t end) {
    ASSERT(0 == OFFSET_4K(pt));
    ASSERT(0 == OFFSET_4K(va));
    ASSERT(0 == OFFSET_4K(end));
    ASSERT(va <= end);

    page_info_t *info = page_block_info(pt);
    uint64_t *tbl = (uint64_t *)(pt + DIRECT_MAP_ADDR);

    for (int i = (va >> 12) & 0x1ff; (i < 512) && (va < end); ++i) {
        if (tbl[i] & MMU_P) {
            --info->ent_num;
        }
        tbl[i] = 0;
        __asm__("invlpg (%0)" :: "r"(va) : "memory");
        va += SIZE_4K;
    }

    return va;
}


//------------------------------------------------------------------------------
// PD，表项可以指向 PT，也可以直接映射 2M
//------------------------------------------------------------------------------

// PDE 可能指向 PT，也可能是 2M 表项
static void pd_free(uint64_t pd) {
    ASSERT(0 == OFFSET_4K(pd));

    uint64_t *tbl = (uint64_t *)(pd + DIRECT_MAP_ADDR);
    for (int i = 0; i < 512; ++i) {
        if ((tbl[i] & MMU_P) && !(tbl[i] & MMU_PS)) {
            pt_free(tbl[i] & MMU_ADDR);
        }
    }

    page_block_free(pd);
}

static uint64_t pd_map(uint64_t pd, uint64_t va, uint64_t end, uint64_t pa, uint64_t attrs) {
    ASSERT(0 == OFFSET_4K(pd));
    ASSERT(0 == OFFSET_4K(va));
    ASSERT(0 == OFFSET_4K(end));
    ASSERT(va <= end);
    ASSERT(0 == OFFSET_4K(pa));
    ASSERT(0 == (attrs & ~MMU_ATTRS));

    page_info_t *info = page_block_info(pd);
    uint64_t *tbl = (uint64_t *)(pd + DIRECT_MAP_ADDR);

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

    return va - start;
}

uint64_t pd_unmap(uint64_t pd, uint64_t va, uint64_t end) {
    ASSERT(0 == OFFSET_4K(pd));
    ASSERT(0 == OFFSET_4K(va));
    ASSERT(0 == OFFSET_4K(end));
    ASSERT(va <= end);

    page_info_t *info = page_block_info(pd);
    uint64_t *tbl = (uint64_t *)(pd + DIRECT_MAP_ADDR);

    for (int i = (va >> 21) & 0x1ff; (i < 512) && (va < end); ++i) {
        if (0 == (tbl[i] & MMU_P)) {
            va +=   SIZE_2M - 1;
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
            __asm__("invlpg (%0)" :: "r"(va) : "memory");
            va += SIZE_2M;
            continue;
        }

        // unmap 范围不能覆盖完整的 2M
        uint64_t pt = tbl[i] & MMU_ADDR;

        // 原本是 2M 页，unmap 没有完整清除这 2M，还要保留一些
        if (tbl[i] & MMU_PS) {
            uint64_t old_va = va - OFFSET_2M(va);
            uint64_t old_pa = pt;
            ASSERT(0 == OFFSET_2M(old_pa));

            pt = alloc_table();

            if (old_va != va) {
                pt_map(pt, old_va, va, old_pa, tbl[i] & MMU_ATTRS);
                va = old_va + SIZE_2M;
            }
            if (end < old_va + SIZE_2M) {
                pt_map(pt, end, old_va + SIZE_2M, end - old_va + old_pa, tbl[i] & MMU_ATTRS);
                va = end;
            }

            tbl[i] = (pt & MMU_ADDR) | MMU_P | MMU_US | MMU_RW;
            __asm__("invlpg (%0)" :: "r"(old_va) : "memory");
        } else {
            va = pt_unmap(pt, va, end);
        }

        // 如果次级页表内容为空，则可以将页表删除
        if (0 == page_block_info(pt)->ent_num) {
            pt_free(pt);
            tbl[i] = 0;
        }
    }

    return va;
}


//------------------------------------------------------------------------------
// PDP，表项可以指向 PD，也可以直接映射 1G
//------------------------------------------------------------------------------

// PDPE 可能指向 PD，也可能是 1G 表项
static void pdp_free(uint64_t pdp) {
    ASSERT(0 == OFFSET_4K(pdp));

    uint64_t *tbl = (uint64_t *)(pdp + DIRECT_MAP_ADDR);
    for (int i = 0; i < 512; ++i) {
        if ((tbl[i] & MMU_P) && !(tbl[i] & MMU_PS)) {
            pd_free(tbl[i] & MMU_ADDR);
        }
    }

    page_block_free(pdp);
}

static uint64_t pdp_map(uint64_t pdp, uint64_t va, uint64_t end, uint64_t pa, uint64_t attrs) {
    ASSERT(0 == OFFSET_4K(pdp));
    ASSERT(0 == OFFSET_4K(va));
    ASSERT(0 == OFFSET_4K(end));
    ASSERT(va <= end);
    ASSERT(0 == OFFSET_4K(pa));
    ASSERT(0 == (attrs & ~MMU_ATTRS));

    page_info_t *info = page_block_info(pdp);
    uint64_t *tbl = (uint64_t *)(pdp + DIRECT_MAP_ADDR);

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
        } else if (tbl[i] & MMU_PS) {
            uint64_t old_va = va - OFFSET_1G(va);
            uint64_t old_pa = pd;
            ASSERT(0 == OFFSET_1G(old_pa));

            pd = alloc_table();
            if (old_va != va) {
                pd_map(pd, old_va, va, old_pa, tbl[i] & MMU_ATTRS);
            }
            if (end < old_va + SIZE_1G) {
                pd_map(pd, end, old_va + SIZE_1G, end - old_va + old_pa, tbl[i] & MMU_ATTRS);
            }
        }

        tbl[i] = (pd & MMU_ADDR) | MMU_P | MMU_RW | MMU_US; // 不是末级表项，使用最宽松的权限
        uint64_t len = pd_map(pd, va, end, pa, attrs);
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

    page_info_t *info = page_block_info(pdp);
    uint64_t *tbl = (uint64_t *)(pdp + DIRECT_MAP_ADDR);

    for (int i = (va >> 30) & 0x1ff; (i < 512) && (va < end); ++i) {
        if (0 == (tbl[i] & MMU_P)) {
            va +=   SIZE_1G - 1;
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
            __asm__("invlpg (%0)" :: "r"(va) : "memory");
            va += SIZE_1G;
            continue;
        }

        // unmap 范围不能覆盖完整的 1G
        uint64_t pd = tbl[i] & MMU_ADDR;

        // 原本是 1G 页，unmap 没有完整清除这 1G，还要保留一些
        if (tbl[i] & MMU_PS) {
            uint64_t old_va = va - OFFSET_1G(va);
            uint64_t old_pa = pd;
            ASSERT(0 == OFFSET_1G(old_pa));

            pd = alloc_table();

            if (old_va != va) {
                pd_map(pd, old_va, va, old_pa, tbl[i] & MMU_ATTRS);
                va = old_va + SIZE_1G;
            }
            if (end < old_va + SIZE_1G) {
                pd_map(pd, end, old_va + SIZE_1G, end - old_va + old_pa, tbl[i] & MMU_ATTRS);
                va = end;
            }

            tbl[i] = (pd & MMU_ADDR) | MMU_P | MMU_US | MMU_RW;
            __asm__("invlpg (%0)" :: "r"(old_va) : "memory");
        } else {
            va = pd_unmap(pd, va, end);
        }

        // 如果下一级 PD 有效表项为零，则删除
        if (0 == page_block_info(pd)->ent_num) {
            pd_free(pd);
            tbl[i] = 0;
            --info->ent_num;
        }
    }

    return va;
}


//------------------------------------------------------------------------------
// PML4
//------------------------------------------------------------------------------

static void pml4_free(uint64_t pml4) {
    ASSERT(0 == OFFSET_4K(pml4));

    uint64_t *tbl = (uint64_t *)(pml4 + DIRECT_MAP_ADDR);
    for (int i = 0; i < 512; ++i) {
        // 如果带有 global 标记，说明被所有进程共享，不能删除
        if ((tbl[i] & MMU_P) && !(tbl[i] & MMU_G)) {
            pdp_free(tbl[i] & MMU_ADDR);
        }
    }

    page_block_free(pml4);
}

static uint64_t pml4_map(uint64_t pml4, uint64_t va, uint64_t end, uint64_t pa, uint64_t attrs) {
    ASSERT(0 == OFFSET_4K(pml4));
    ASSERT(0 == OFFSET_4K(va));
    ASSERT(0 == OFFSET_4K(end));
    ASSERT(va <= end);
    ASSERT(0 == OFFSET_4K(pa));
    ASSERT(0 == (attrs & ~MMU_ATTRS));

    page_info_t *info = page_block_info(pml4);
    uint64_t *tbl = (uint64_t *)(pml4 + DIRECT_MAP_ADDR);

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

static uint64_t pml4_unmap(uint64_t pml4, uint64_t va, uint64_t end) {
    ASSERT(0 == OFFSET_4K(pml4));
    ASSERT(0 == OFFSET_4K(va));
    ASSERT(0 == OFFSET_4K(end));
    ASSERT(va <= end);

    page_info_t *info = page_block_info(pml4);
    uint64_t *tbl = (uint64_t *)(pml4 + DIRECT_MAP_ADDR);

    for (int i = (va >> 39) & 0x1ff; (i < 512) && (va < end); ++i) {
        if (0 == (tbl[i] & MMU_P)) {
            va +=   SIZE_1G * 512 - 1;
            va &= ~(SIZE_1G * 512 - 1);
            continue;
        }

        uint64_t pdp = tbl[i] & MMU_ADDR;
        va = pdp_unmap(pdp, va, end);

        // 需要判断这个 PDP 是不是内核地址范围
        // 如果是内核空间的 PDP，即使有效元素为零也不能删除
        // 因为内核 PDP 被所有进程的 PML4 引用
        if ((i < 256) && (0 == page_block_info(pdp)->ent_num)) {
            pdp_free(pdp);
            tbl[i] = 0;
            --info->ent_num;
        }
    }

    return va;
}


//------------------------------------------------------------------------------
// 公开 API
//------------------------------------------------------------------------------

// 创建回内核页表
INIT_TEXT void mmu_init() {
    ASSERT(0 == g_kernel_cr3);

    g_kernel_cr3 = alloc_table();
    page_block_info(g_kernel_cr3)->ent_num = 256;

    // 填充 canonical hole 之后对 PDP 的映射
    // TODO 256 个 PDP 可以连续分配
    uint64_t *pml4 = (uint64_t *)(g_kernel_cr3 + DIRECT_MAP_ADDR);
    for (int i = 256; i < 512; ++i) {
        uint64_t pdp = alloc_table();
        pml4[i] = (pdp & MMU_ADDR) | MMU_G | MMU_P | MMU_US | MMU_RW;
    }
}

// 获取内核页表
size_t mmu_kernel_table() {
    ASSERT(0 != g_kernel_cr3);
    return g_kernel_cr3;
}

// 创建一套新的页表，供进程使用，内核部分继承
size_t mmu_create_table() {
    ASSERT(0 != g_kernel_cr3);
    size_t tbl = alloc_table();
    uint64_t *pml4 = (uint64_t *)(tbl + DIRECT_MAP_ADDR);
    uint64_t *kernel_pml4 = (uint64_t *)(g_kernel_cr3 + DIRECT_MAP_ADDR);
    memcpy(&pml4[256], &kernel_pml4[256], 256 * sizeof(uint64_t));
    return tbl;
}

// 删除一套页表，只能删除进程页表
void mmu_delete_table(size_t tbl) {
    ASSERT(tbl != g_kernel_cr3);
    pml4_free(tbl);
}

// 查询虚拟地址映射的物理地址，同时返回页面属性
// 多级页表中，各级表项都有属性位，不能只看最末一级
// 这部分行为要看 Intel 文档，AMD 文档说的不清楚

// TODO 需要区分映射到零地址还是没有映射
//      可以用 Present 属性位区分

size_t mmu_translate(size_t tbl, size_t va, mmu_attr_t *attrs) {
    uint64_t *pml4 = (uint64_t *)(tbl + DIRECT_MAP_ADDR);
    uint64_t pml4e = pml4[(va >> 39) & 0x1ff];
    if (0 == (pml4e & MMU_P)) {
        return 0;
    }

    *attrs = 0;
    *attrs |= (pml4e & MMU_US) ? MMU_USER  : 0;
    *attrs |= (pml4e & MMU_RW) ? MMU_WRITE : 0;
    *attrs |= (pml4e & MMU_NX) ? 0 : MMU_EXEC;

    uint64_t *pdp = (uint64_t *)((pml4e & MMU_ADDR) + DIRECT_MAP_ADDR);
    uint64_t pdpe = pdp[(va >> 30) & 0x1ff];
    if (0 == (pdpe & MMU_P)) {
        return 0;
    }

    *attrs &= (pdpe & MMU_US) ? MMU_USER  : 0;
    *attrs &= (pdpe & MMU_RW) ? MMU_WRITE : 0;
    *attrs &= (pdpe & MMU_NX) ? 0 : MMU_EXEC;

    if ((g_cpu_features & CPU_FEATURE_1G) && (pdpe & MMU_PS)) {
        ASSERT(0 == OFFSET_1G(pdpe & MMU_ADDR));
        return (pdpe & MMU_ADDR) | OFFSET_1G(va);
    }

    uint64_t *pd = (uint64_t *)((pdpe & MMU_ADDR) + DIRECT_MAP_ADDR);
    uint64_t pde = pd[(va >> 21) & 0x1ff];
    if (0 == (pde & MMU_P)) {
        return 0;
    }

    *attrs &= (pde & MMU_US) ? MMU_USER  : 0;
    *attrs &= (pde & MMU_RW) ? MMU_WRITE : 0;
    *attrs &= (pde & MMU_NX) ? 0 : MMU_EXEC;

    if (pde & MMU_PS) {
        ASSERT(0 == OFFSET_2M(pde & MMU_ADDR));
        return (pde & MMU_ADDR) | OFFSET_2M(va);
    }

    uint64_t *pt = (uint64_t *)((pde & MMU_ADDR) + DIRECT_MAP_ADDR);
    uint64_t pte = pt[(va >> 12) & 0x1ff];
    if (0 == (pte & MMU_P)) {
        return 0;
    }

    *attrs &= (pte & MMU_US) ? MMU_USER  : 0;
    *attrs &= (pte & MMU_RW) ? MMU_WRITE : 0;
    *attrs &= (pte & MMU_NX) ? 0 : MMU_EXEC;
    return (pte & MMU_ADDR) | OFFSET_4K(va);
}

void mmu_map(size_t tbl, size_t va, size_t end, size_t pa, mmu_attr_t attrs) {
    ASSERT(0 == OFFSET_4K(tbl));
    ASSERT(0 == OFFSET_4K(va));
    ASSERT(0 == OFFSET_4K(end));
    ASSERT(0 == OFFSET_4K(pa));

    uint64_t prot = 0;
    prot |= (attrs & MMU_USER) ? MMU_US : 0;
    prot |= (attrs & MMU_WRITE) ? MMU_RW : 0;
    prot |= (attrs & MMU_EXEC) && (g_cpu_features & CPU_FEATURE_NX) ? 0 : MMU_NX;

    uint64_t len = pml4_map(tbl, va, end, pa, prot);
    ASSERT(va + len == end);
    (void)len;
}

void mmu_unmap(size_t tbl, size_t va, size_t end) {
    ASSERT(0 == OFFSET_4K(tbl));
    ASSERT(0 == OFFSET_4K(va));
    ASSERT(0 == OFFSET_4K(end));

    va = pml4_unmap(tbl, va, end);
    ASSERT(va == end);
}


//------------------------------------------------------------------------------
// 打印页表内容
//------------------------------------------------------------------------------

static void mmu_show_map(uint64_t va, uint64_t pa, uint64_t size, uint64_t attrs, int nentries) {
    if (0 == size) {
        return;
    }

    char mod[4];
    mod[0] = (MMU_RW & attrs) ? 'W' : '-';
    mod[1] = (MMU_NX & attrs) ? '-' : 'X';
    mod[2] = (MMU_US & attrs) ? 'U' : '-';
    mod[3] = (MMU_G  & attrs) ? 'G' : '-';

    log("  - va %016lx..0x%016lx --> pa %016lx, %.4s, %3d entries\n", va, va + size, pa, mod, nentries);
}

void mmu_walk(uint64_t tbl) {
    uint64_t map_va    = 0; // 映射范围的起始虚拟地址
    uint64_t map_pa    = 0; // 映射范围的起始物理地址
    uint64_t map_size  = 0; // 映射范围的大小
    uint64_t map_attrs = 0;
    int      nentries  = 0; // 涉及多少条目

    log("page table 0x%lx content:\n", tbl);

    uint64_t *pml4 = (uint64_t *)(tbl + DIRECT_MAP_ADDR);
    uint64_t va4 = 0;
    for (int i = 0; i < 512; ++i, va4 += (SIZE_1G << 9)) {
        if (256 == i) {
            va4 += 0xffff000000000000; // 跳过 canonical hole
        }

        if (0 == (pml4[i] & MMU_P)) {
            mmu_show_map(map_va, map_pa, map_size, map_attrs, nentries);
            map_va    = 0;
            map_pa    = 0;
            map_size  = 0;
            map_attrs = 0;
            nentries  = 0;
            continue;
        }

        uint64_t *pdp = (uint64_t *)((pml4[i] & MMU_ADDR) + DIRECT_MAP_ADDR);
        uint64_t va3 = va4;
        for (int j = 0; j < 512; ++j, va3 += SIZE_1G) {
            if (0 == (pdp[j] & MMU_P)) {
                mmu_show_map(map_va, map_pa, map_size, map_attrs, nentries);
                map_va    = 0;
                map_pa    = 0;
                map_size  = 0;
                map_attrs = 0;
                nentries  = 0;
                continue;
            }

            if (pdp[j] & MMU_PS) { // 1G
                if ((va3 == map_va + map_size) &&
                    ((pdp[j] & MMU_ADDR) == map_pa + map_size) &&
                    ((pdp[j] & MMU_ATTRS) == map_attrs)) {
                    map_size += SIZE_1G;
                    ++nentries;
                } else {
                    mmu_show_map(map_va, map_pa, map_size, map_attrs, nentries);
                    map_va    = va3;
                    map_pa    = pdp[j] & MMU_ADDR;
                    map_size  = SIZE_1G;
                    map_attrs = pdp[j] & MMU_ATTRS;
                    nentries  = 1;
                }
                continue;
            }

            uint64_t *pd = (uint64_t *)((pdp[j] & MMU_ADDR) + DIRECT_MAP_ADDR);
            uint64_t va2 = va3;
            for (int k = 0; k < 512; ++k, va2 += SIZE_2M) {
                if (0 == (pd[k] & MMU_P)) {
                    mmu_show_map(map_va, map_pa, map_size, map_attrs, nentries);
                    map_va    = 0;
                    map_pa    = 0;
                    map_size  = 0;
                    map_attrs = 0;
                    nentries  = 0;
                    continue;
                }

                if (pd[k] & MMU_PS) { // 2M
                    if ((va2 == map_va + map_size) &&
                        ((pd[k] & MMU_ADDR) == map_pa + map_size) &&
                        ((pd[k] & MMU_ATTRS) == map_attrs)) {
                        map_size += SIZE_2M;
                        ++nentries;
                    } else {
                        mmu_show_map(map_va, map_pa, map_size, map_attrs, nentries);
                        map_va    = va2;
                        map_pa    = pd[k] & MMU_ADDR;
                        map_size  = SIZE_2M;
                        map_attrs = pd[k] & MMU_ATTRS;
                        nentries  = 1;
                    }
                    continue;
                }

                uint64_t *pt = (uint64_t *)((pd[k] & MMU_ADDR) + DIRECT_MAP_ADDR);
                uint64_t va = va2;
                for (int l = 0; l < 512; ++l, va += PAGE_SIZE) {
                    if (0 == (pt[l] & MMU_P)) {
                        mmu_show_map(map_va, map_pa, map_size, map_attrs, nentries);
                        map_va    = 0;
                        map_pa    = 0;
                        map_size  = 0;
                        map_attrs = 0;
                        nentries  = 0;
                        continue;
                    }

                    if ((va == map_va + map_size) &&
                        ((pt[l] & MMU_ADDR) == map_pa + map_size) &&
                        ((pt[l] & MMU_ATTRS) == map_attrs)) {
                        map_size += PAGE_SIZE;
                        ++nentries;
                    } else {
                        mmu_show_map(map_va, map_pa, map_size, map_attrs, nentries);
                        map_va    = va;
                        map_pa    = pt[l] & MMU_ADDR;
                        map_size  = PAGE_SIZE;
                        map_attrs = pt[l] & MMU_ATTRS;
                        nentries  = 1;
                    }
                }
            }
        }
    }

    mmu_show_map(map_va, map_pa, map_size, map_attrs, nentries);
}
