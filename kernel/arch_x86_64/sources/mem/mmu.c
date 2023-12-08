// 页表内容管理

#include <arch_mmu.h>
#include <wheel.h>
#include <arch_cpu.h>
#include <page.h>
#include <str.h>




//------------------------------------------------------------------------------
// 定义这些宏，便于单元测试 mock
//------------------------------------------------------------------------------

#ifndef VIRT
#define VIRT(pa) (void *)((pa) | DIRECT_MAP_ADDR)
#endif

#ifndef PAGE_ALLOC
#define PAGE_ALLOC() page_alloc(PT_PGTBL)
#endif

#ifndef PAGE_FREE
#define PAGE_FREE(p) page_free(p)
#endif

#ifndef PAGE_INFO
#define PAGE_INFO(p) page_block_info(p)
#endif

#ifndef SUPPORT_1G
#define SUPPORT_1G (g_cpu_features & CPU_FEATURE_1G)
#endif

#ifndef SUPPORT_NX
#define SUPPORT_NX (g_cpu_features & CPU_FEATURE_NX)
#endif


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






// 页描述符中，记录页表的表项数量，作为引用计数
// 页表层次：PML4 --> PDP --> PD --> PT

// AMD64 可以使用多种大小的页：4K、2M、1G
// 为了节省空间，优先使用更大的页
// 但是随着修改，不断映射、清除、再映射，页表碎片化逐渐严重
// 我们需要识别以下情况：
//  - 一张页表，如果所有表项都无效，则这张页表可以删除
//  - 一张页表，如果所有表项都存在，且映射的物理地址连续，属性一致，则这张页表可以合并为一个更大的页


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
    uint64_t pa = PAGE_ALLOC();
    page_info_t *info = PAGE_INFO(pa);
    info->ent_num = 0;

    uint64_t *va = VIRT(pa);
    bset(va, 0, PAGE_SIZE);
    return pa;
}

// // 获取下一级页表，如果不存在则创建
// static uint64_t get_subtbl(uint64_t *tbl, uint64_t idx) {
//     ASSERT(NULL != tbl);
//     ASSERT(idx < 512);

//     if (tbl[idx] & MMU_P) {
//         return tbl[idx] & MMU_ADDR;
//     }

//     uint64_t pa = alloc_table();
//     tbl[idx] = (pa & MMU_ADDR) | MMU_P | MMU_RW | MMU_RW;
//     return pa;
// }




// 创建一套新的页表
uint64_t mmu_table_create() {
    return alloc_table();
}


// 查询虚拟地址映射的物理地址，同时返回页面属性
// 多级页表中，各级表项都有属性位，不能只看最末一级
// 这部分行为要看 Intel 文档，AMD 文档说的不清楚
uint64_t mmu_translate(uint64_t tbl, uint64_t va, mmu_attr_t *attrs) {
    uint64_t *pml4 = VIRT(tbl);
    uint64_t pml4e = pml4[(va >> 39) & 0x1ff];
    if (0 == (pml4e & MMU_P)) {
        return INVALID_ADDR;
    }

    *attrs = 0;
    *attrs |= (pml4e & MMU_US) ? MMU_USER  : 0;
    *attrs |= (pml4e & MMU_RW) ? MMU_WRITE : 0;
    *attrs |= (pml4e & MMU_NX) ? 0 : MMU_EXEC;

    uint64_t *pdp = VIRT(pml4e & MMU_ADDR);
    uint64_t pdpe = pdp[(va >> 30) & 0x1ff];
    if (0 == (pdpe & MMU_P)) {
        return INVALID_ADDR;
    }

    *attrs &= (pdpe & MMU_US) ? MMU_USER  : 0;
    *attrs &= (pdpe & MMU_RW) ? MMU_WRITE : 0;
    *attrs &= (pdpe & MMU_NX) ? 0 : MMU_EXEC;

    if (SUPPORT_1G && (pdpe & MMU_PS)) {
        ASSERT(0 == OFFSET_1G(pdpe & MMU_ADDR));
        return (pdpe & MMU_ADDR) | OFFSET_1G(va);
    }

    uint64_t *pd = VIRT(pdpe & MMU_ADDR);
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

    uint64_t *pt = VIRT(pde & MMU_ADDR);
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
static void pt_free(uint64_t pt) {
    ASSERT(0 == OFFSET_4K(pt));
    PAGE_FREE(pt);
}

// PDE 可能指向 PT，也可能是 2M 表项
static void pd_free(uint64_t pd) {
    ASSERT(0 == OFFSET_4K(pd));

    // 如果页表内容为空，则无需遍历，可以直接释放
    page_info_t *info = PAGE_INFO(pd);
    if (0 == info->ent_num) {
        PAGE_FREE(pd);
    }

    uint64_t *tbl = VIRT(pd);
    for (int i = 0; i < 512; ++i) {
        if ((tbl[i] & MMU_P) && !(tbl[i] & MMU_PS)) {
            pt_free(tbl[i] & MMU_ADDR);
        }
    }
}

// PDPE 可能指向 PD，也可能是 1G 表项
static void pdp_free(uint64_t pdp) {
    ASSERT(0 == OFFSET_4K(pdp));

    // 如果有效条目数量为零，则无需遍历，直接释放页面
    page_info_t *info = PAGE_INFO(pdp);
    if (0 == info->ent_num) {
        PAGE_FREE(pdp);
    }

    uint64_t *tbl = VIRT(pdp);
    for (int i = 0; i < 512; ++i) {
        if ((tbl[i] & MMU_P) && !(tbl[i] & MMU_PS)) {
            pd_free(tbl[i] & MMU_ADDR);
        }
    }
}

static void pml4_free(uint64_t pml4) {
    ASSERT(0 == OFFSET_4K(pml4));

    page_info_t *info = PAGE_INFO(pml4);
    if (0 == info->ent_num) {
        PAGE_FREE(pml4);
    }

    uint64_t *tbl = VIRT(pml4);
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

static uint64_t pt_map(uint64_t pt, uint64_t va, uint64_t end, uint64_t pa, uint64_t attrs) {
    ASSERT(0 == OFFSET_4K(pt));
    ASSERT(0 == OFFSET_4K(va));
    ASSERT(0 == OFFSET_4K(end));
    ASSERT(va <= end);
    ASSERT(0 == OFFSET_4K(pa));
    ASSERT(0 == (attrs & ~MMU_ATTRS));

    page_info_t *info = PAGE_INFO(pt);
    uint64_t *tbl = VIRT(pt);

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

// 判断 PT 能否合并为一个 2M 页
int pt_contiuous(uint64_t pt) {
    ASSERT(0 == OFFSET_4K(pt));

    page_info_t *info = PAGE_INFO(pt);
    if (512 != info->ent_num) {
        return 0;
    }

    uint64_t *tbl = VIRT(pt);
    uint64_t pa = tbl[0] & MMU_ADDR;
    if (OFFSET_2M(pa)) {
        return 0;
    }

    pa += SIZE_4K;
    uint64_t attrs = tbl[0] & MMU_ADDR;
    for (int i = 1; i < 512; ++i, pa += SIZE_4K) {
        if (pa != (tbl[i] & MMU_ADDR)) {
            return 0;
        }
        if (attrs != (tbl[i] & MMU_ADDR)) {
            return 0;
        }
    }

    return 1;
}

static uint64_t pd_map(uint64_t pd, uint64_t va, uint64_t end, uint64_t pa, uint64_t attrs) {
    ASSERT(0 == OFFSET_4K(pd));
    ASSERT(0 == OFFSET_4K(va));
    ASSERT(0 == OFFSET_4K(end));
    ASSERT(va <= end);
    ASSERT(0 == OFFSET_4K(pa));
    ASSERT(0 == (attrs & ~MMU_ATTRS));

    page_info_t *info = PAGE_INFO(pd);
    uint64_t *tbl = VIRT(pd);

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
            // 原本是 2M，且 mapping 没有完整覆盖这 2M，还要保留一部分
            // 保留的部分可以是开头，也可以是结尾
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

// 判断 PD 能否合并为一个 1G 页
int pd_continuous(uint64_t pd) {
    ASSERT(0 == OFFSET_4K(pd));

    page_info_t *info = PAGE_INFO(pd);
    if (512 != info->ent_num) {
        return 0;
    }

    uint64_t *tbl = VIRT(pd);
    uint64_t pa = tbl[0] & MMU_PS;
    uint64_t attrs = tbl[0] & MMU_ATTRS;

    if (0 == (tbl[0] & MMU_PS)) {
        if (!pt_contiuous(pa)) {
            return 0;
        }
        uint64_t *pt = VIRT(pa);
        pa = pt[0] & MMU_ADDR;
        attrs = pt[0] & MMU_ATTRS;
    }

    if (OFFSET_1G(pa)) {
        return 0;
    }

    pa += SIZE_2M;
    for (int i = 1; i < 512; ++i, pa += SIZE_2M) {
        uint64_t this_addr = tbl[i] & MMU_ADDR;
        uint64_t this_attrs = tbl[i] & MMU_ATTRS;

        if (0 == (tbl[i] & MMU_PS)) {
            if (!pt_contiuous(this_addr)) {
                return 0;
            }
            uint64_t *pt = VIRT(this_addr);
            this_addr = pt[0] & MMU_ADDR;
            this_attrs = pt[0] & MMU_ATTRS;
        }

        if (pa != this_addr) {
            return 0;
        }
        if (attrs != this_attrs) {
            return 0;
        }
    }

    return 1;
}

static uint64_t pdp_map(uint64_t pdp, uint64_t va, uint64_t end, uint64_t pa, uint64_t attrs) {
    ASSERT(0 == OFFSET_4K(pdp));
    ASSERT(0 == OFFSET_4K(va));
    ASSERT(0 == OFFSET_4K(end));
    ASSERT(va <= end);
    ASSERT(0 == OFFSET_4K(pa));
    ASSERT(0 == (attrs & ~MMU_ATTRS));

    page_info_t *info = PAGE_INFO(pdp);
    uint64_t *tbl = VIRT(pdp);

    uint64_t start = va;
    for (int i = (va >> 30) & 0x1ff; (i < 512) && (va < end); ++i) {
        // 判断能否使用 1G 表项
        if (SUPPORT_1G && (0 == OFFSET_1G(va | pa)) && (va + SIZE_1G <= end)) {
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

static uint64_t pml4_map(uint64_t pml4, uint64_t va, uint64_t end, uint64_t pa, uint64_t attrs) {
    ASSERT(0 == OFFSET_4K(pml4));
    ASSERT(0 == OFFSET_4K(va));
    ASSERT(0 == OFFSET_4K(end));
    ASSERT(va <= end);
    ASSERT(0 == OFFSET_4K(pa));
    ASSERT(0 == (attrs & ~MMU_ATTRS));

    page_info_t *info = PAGE_INFO(pml4);
    uint64_t *tbl = VIRT(pml4);

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
    prot |= (attrs & MMU_EXEC) && SUPPORT_NX ? 0 : MMU_NX;

    uint64_t mapped = pml4_map(tbl, va, end, pa, prot);
    ASSERT(va + mapped == end);
}


//------------------------------------------------------------------------------
// 删除映射
//------------------------------------------------------------------------------

// 每个函数返回解除映射后的结束地址，而不是长度

static uint64_t pt_unmap(uint64_t pt, uint64_t va, uint64_t end) {
    ASSERT(0 == OFFSET_4K(pt));
    ASSERT(0 == OFFSET_4K(va));
    ASSERT(0 == OFFSET_4K(end));
    ASSERT(va <= end);

    page_info_t *info = PAGE_INFO(pt);
    uint64_t *tbl = VIRT(pt);

    for (int i = (va >> 12) & 0x1ff; (i < 512) && (va < end); ++i) {
        if (tbl[i] & MMU_P) {
            --info->ent_num;
        }
        tbl[i] = 0;
        va += SIZE_4K;
    }

    return va;
}

uint64_t pd_unmap(uint64_t pd, uint64_t va, uint64_t end) {
    ASSERT(0 == OFFSET_4K(pd));
    ASSERT(0 == OFFSET_4K(va));
    ASSERT(0 == OFFSET_4K(end));
    ASSERT(va <= end);

    page_info_t *info = PAGE_INFO(pd);
    uint64_t *tbl = VIRT(pd);

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
            }
            if (end < old_va + SIZE_2M) {
                pt_map(pt, end, old_va + SIZE_2M, end - old_va + old_pa, tbl[i] & MMU_ATTRS);
            //     va = end;
            // } else {
            //     va +=   SIZE_2M - 1;
            //     va &= ~(SIZE_2M - 1);
            }

            tbl[i] = (pt & MMU_ADDR) | MMU_P | MMU_US | MMU_RW;
        }

        va = pt_unmap(pt, va, end);

        // 如果次级页表内容为空，则可以将页表删除
        if (0 == PAGE_INFO(pt)->ent_num) {
            pt_free(pt);
            tbl[i] = 0;
        }
    }

    return va;
}

