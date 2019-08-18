#include <wheel.h>

// different fields of virtual memory address
#define PML4TE_SHIFT     39                     // page-map level-4 table
#define PDPTE_SHIFT      30                     // page-directory-pointer table
#define PDTE_SHIFT       21                     // page-directory table
#define PTE_SHIFT        12                     // page table

// alignment check
#define OFFSET_4K(x)    ((x) & (0x1000UL - 1))
#define OFFSET_2M(x)    ((x) & (0x200000UL - 1))
#define OFFSET_1G(x)    ((x) & (0x40000000UL - 1))

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

// read addr field and convert to canonical form
#define SUB_TABLE(x)    ((u64 *) (((s64) (x) << 12) >> 12))

static int support_1g = NO;
static int support_nx = NO;

//------------------------------------------------------------------------------
// create new page table

static u64 * tbl_alloc() {
    pfn_t page = page_block_alloc_or_fail(ZONE_DMA|ZONE_NORMAL, 0, PT_PGTABLE);
    u64 * virt = (u64 *) phys_to_virt((u64) page << PAGE_SHIFT);
    page_array[page].ent_count = 0;
    memset(virt, 0, PAGE_SIZE);
    return virt;
}

//------------------------------------------------------------------------------
// delete table and all entries inside

static void pt_free(u64 * tbl) {
    pfn_t pf = (pfn_t) (virt_to_phys(tbl) >> PAGE_SHIFT);
    page_block_free(pf, 0);
}

static void pdt_free(u64 * tbl) {
    pfn_t pdf = (pfn_t) (virt_to_phys(tbl) >> PAGE_SHIFT);
    if (0 == page_array[pdf].ent_count) {
        page_block_free(pdf, 0);
        return;
    }
    for (int i = 0; i < 512; ++i) {
        if (0 == (tbl[i] & MMU_P)) {
            continue;
        } else if (0 != (tbl[i] & MMU_PS)) {
            continue;
        }
        pt_free(SUB_TABLE(tbl[i]));
    }
}

static void pdpt_free(u64 * tbl) {
    pfn_t pdpf = (pfn_t) (virt_to_phys(tbl) >> PAGE_SHIFT);
    if (0 == page_array[pdpf].ent_count) {
        page_block_free(pdpf, 0);
        return;
    }
    for (int i = 0; i < 512; ++i) {
        if (0 == (tbl[i] & MMU_P)) {
            continue;
        } else if ((YES == support_1g) && (0 != (tbl[i] & MMU_PS))) {
            continue;
        }
        pdt_free(SUB_TABLE(tbl[i]));
    }
}

static void pml4t_free(u64 * tbl) {
    pfn_t pml4f = (pfn_t) (virt_to_phys(tbl) >> PAGE_SHIFT);
    if (0 == page_array[pml4f].ent_count) {
        page_block_free(pml4f, 0);
        return;
    }
    for (int i = 0; i < 512; ++i) {
        if (0 == (tbl[i] & MMU_P)) {
            continue;
        }
        pdpt_free(SUB_TABLE(tbl[i]));
    }
}

//------------------------------------------------------------------------------
// address translation, return physical address

static u64 pt_translate(u64 * tbl, u64 va) {
    u64 idx = (va >> PTE_SHIFT) & 0x01ffUL;
    if (0 == (tbl[idx] & MMU_P)) {
        return NO_ADDR;
    }
    return (tbl[idx] & MMU_ADDR) | OFFSET_4K(va);
}

static u64 pdt_translate(u64 * tbl, u64 va) {
    u64 idx = (va >> PDTE_SHIFT) & 0x01ffUL;
    if (0 == (tbl[idx] & MMU_P)) {
        return NO_ADDR;
    }
    if (0 != (tbl[idx] & MMU_PS)) {
        assert(0 == OFFSET_2M(tbl[idx] & MMU_ADDR));
        return (tbl[idx] & MMU_ADDR) | OFFSET_2M(va);
    }
    return pt_translate(SUB_TABLE(tbl[idx]), va);
}

static u64 pdpt_translate(u64 * tbl, u64 va) {
    u64 idx = (va >> PDPTE_SHIFT) & 0x01ffUL;
    if (0 == (tbl[idx] & MMU_P)) {
        return NO_ADDR;
    }
    if ((YES == support_1g) && (0 != (tbl[idx] & MMU_PS))) {
        assert(0 == OFFSET_1G(tbl[idx]));
        return (tbl[idx] & MMU_ADDR);
    }
    return pdt_translate(SUB_TABLE(tbl[idx]), va);
}

static u64 pml4t_translate(u64 * tbl, u64 va) {
    u64 idx = (va >> PML4TE_SHIFT) & 0x01ffUL;
    if (0 == (tbl[idx] & MMU_P)) {
        return NO_ADDR;
    }
    return pdpt_translate(SUB_TABLE(tbl[idx]), va);
}

//------------------------------------------------------------------------------
// add new mapping, return the number of pages mapped
// overwrite existing mapping without warning

static u64 pt_map(u64 * tbl, u64 va, u64 pa, u64 fields, u64 n) {
    pfn_t pf  = (pfn_t) (virt_to_phys(tbl) >> PAGE_SHIFT);
    u64   num = 0;
    u64   idx = (va >> PTE_SHIFT) & 0x01ffUL;

    while ((n > 0) && (idx < 512)) {
        if (0 == (tbl[idx] & MMU_P)) {
            ++page_array[pf].ent_count;
        }

        tbl[idx] = (pa & MMU_ADDR) | fields | MMU_P;
        pa += PAGE_SIZE;
        --n;
        ++num;
        ++idx;
    }

    return num;
}

static u64 pdt_map(u64 * tbl, u64 va, u64 pa, u64 fields, u64 n) {
    pfn_t pdf = (pfn_t) (virt_to_phys(tbl) >> PAGE_SHIFT);
    u64   num = 0;
    u64   idx = (va >> PDTE_SHIFT) & 0x01ffUL;

    while ((n > 0) && (idx < 512)) {
        if ((n >= 512) && (0 == OFFSET_2M(va)) && (0 == OFFSET_2M(pa))) {
            if (0 == (tbl[idx] & MMU_P)) {
                ++page_array[pdf].ent_count;
            } else if (0 == (tbl[idx] & MMU_PS)) {
                pt_free(SUB_TABLE(tbl[idx]));
            }

            // create 2M entry
            tbl[idx] = (va & MMU_ADDR) | fields | MMU_PS | MMU_P;
            va  += 512 * PAGE_SIZE;
            pa  += 512 * PAGE_SIZE;
            n   -= 512;
            num += 512;
            ++idx;
            continue;
        }

        // create new page table if not present
        if (0 == (tbl[idx] & MMU_P)) {
            tbl[idx] = ((u64) tbl_alloc() & MMU_ADDR) | MMU_US | MMU_RW | MMU_P;
            ++page_array[pdf].ent_count;
        }

        // fill page table
        u64 * pt  = SUB_TABLE(tbl[idx]);
        u64 ret = pt_map(pt, va, pa, fields, n);
        va  += ret * PAGE_SIZE;
        pa  += ret * PAGE_SIZE;
        n   -= ret;
        num += ret;
        ++idx;
    }

    return num;
}

static u64 pdpt_map(u64 * tbl, u64 va, u64 pa, u64 fields, u64 n) {
    pfn_t pdpf = (pfn_t) (virt_to_phys(tbl) >> PAGE_SHIFT);
    u64   num  = 0;
    u64   idx  = (va >> PDPTE_SHIFT) & 0x01ffUL;

    while ((n > 0) && (idx < 512)) {
        if ((YES == support_1g) && (n >= 512 * 512) && (0 == OFFSET_1G(va)) && (0 == OFFSET_1G(pa))) {
            if (0 == (tbl[idx] & MMU_P)) {
                ++page_array[pdpf].ent_count;
            } else if (0 == (tbl[idx] & MMU_PS)) {
                pdt_free(SUB_TABLE(tbl[idx]));
            }

            // add 1G entry
            tbl[idx] = (pa & MMU_ADDR) | fields | MMU_PS | MMU_P;
            va  += 512 * 512 * PAGE_SIZE;
            pa  += 512 * 512 * PAGE_SIZE;
            n   -= 512 * 512;
            num += 512 * 512;
            ++idx;
            continue;
        }

        // create new page directory table if not present
        if (0 == (tbl[idx] & MMU_P)) {
            tbl[idx] = ((u64) tbl_alloc() & MMU_ADDR) | MMU_US | MMU_RW | MMU_P;
            ++page_array[pdpf].ent_count;
        }

        // fill page directory table
        u64 * pdt = SUB_TABLE(tbl[idx]);
        usize ret = pdt_map(pdt, va, pa, fields, n);
        va  += ret * PAGE_SIZE;
        pa  += ret * PAGE_SIZE;
        n   -= ret;
        num += ret;
        ++idx;
    }

    return num;
}

static u64 pml4t_map(u64 * tbl, u64 va, u64 pa, u64 fields, u64 n) {
    pfn_t pml4f = (pfn_t) (virt_to_phys(tbl) >> PAGE_SHIFT);
    u64   num   = 0;
    u64   idx   = (va >> PML4TE_SHIFT) & 0x01ffUL;

    while ((n > 0) && (idx < 512)) {
        // create new page directory pointer table if not present
        if (0 == (tbl[idx] & MMU_P)) {
            tbl[idx] = ((u64) tbl_alloc() & MMU_ADDR) | MMU_US | MMU_RW | MMU_P;
            ++page_array[pml4f].ent_count;
        }

        // fill page directory pointer table
        u64 * pdpt = SUB_TABLE(tbl[idx]);
        usize ret  = pdpt_map(pdpt, va, pa, fields, n);
        va  += ret * PAGE_SIZE;
        pa  += ret * PAGE_SIZE;
        n   -= ret;
        num += ret;
        ++idx;
    }

    return num;
}

//------------------------------------------------------------------------------
// clear existing mapping, return the number of pages unmapped
// free the table if all entries are unmapped

static u64 pt_unmap(u64 * tbl, u64 va, u64 n) {
    pfn_t pf  = (pfn_t) (virt_to_phys(tbl) >> PAGE_SHIFT);
    u64   num = 0;
    u64   idx = (va >> PTE_SHIFT) & 0x01ffUL;

    while ((n > 0) && (idx < 512)) {
        if (0 != (tbl[idx] & MMU_P)) {
            --page_array[pf].ent_count;
        }
        tbl[idx] = 0;
        n   -= 1;
        num += 1;
        ++idx;
    }

    return num;
}

static u64 pdt_unmap(u64 * tbl, u64 va, u64 n) {
    pfn_t pdf = (pfn_t) (virt_to_phys(tbl) >> PAGE_SHIFT);
    u64   num = 0;
    u64   idx = (va >> PDTE_SHIFT) & 0x01ffUL;

    while ((n > 0) && (idx < 512)) {
        // how many pages intersects with this entry
        u64 skip = OFFSET_2M(va) >> PAGE_SHIFT;
        u64 cnt  = MIN(512 - skip, n);

        if ((0 != (tbl[idx] & MMU_P)) && (0 != (tbl[idx] & MMU_PS))) {
            // unmap complete 2M big page
            if ((0 == skip) && (512 == cnt)) {
                tbl[idx] = 0;
                --page_array[pdf].ent_count;
                va  += 512 * PAGE_SIZE;
                n   -= 512;
                num += 512;
                ++idx;
                continue;
            }

            // retrive old mapping
            u64 pa     = tbl[idx] & MMU_ADDR;
            u64 fields = tbl[idx] & (MMU_US | MMU_RW | MMU_NX);

            // create new page table
            u64 * pt = tbl_alloc();
            tbl[idx] = ((u64) pt & MMU_ADDR) | MMU_US | MMU_RW | MMU_P;

            // add back extra ranges
            if (0 != skip) {
                pt_map(pt, va - skip * PAGE_SIZE, pa - skip * PAGE_SIZE, fields, skip);
            }
            if ((skip + cnt) < 512) {
                pt_map(pt, va + cnt * PAGE_SIZE, pa + cnt * PAGE_SIZE, fields, 512 - (skip + cnt));
            }

            va  += cnt * PAGE_SIZE;
            n   -= cnt;
            num += cnt;
            ++idx;
            continue;
        }

        if (0 != (tbl[idx] & MMU_P)) {
            u64 * pt = SUB_TABLE(tbl[idx]);
            pfn_t pf = (pfn_t) (virt_to_phys(pt) >> PAGE_SHIFT);

            u64 ret = pt_unmap(pt, va, n);
            if (0 == page_array[pf].ent_count) {
                page_block_free(pf, 0);
                tbl[idx] = 0;
                --page_array[pdf].ent_count;
            }

            va  += ret * PAGE_SIZE;
            n   -= ret;
            num += ret;
            ++idx;
        } else {
            va  += cnt * PAGE_SIZE;
            n   -= cnt;
            num += cnt;
            ++idx;
        }
    }

    return num;
}

static u64 pdpt_unmap(u64 * tbl, u64 va, u64 n) {
    pfn_t pdpf = (pfn_t) (virt_to_phys(tbl) >> PAGE_SHIFT);
    u64   num  = 0;
    u64   idx  = (va >> PDPTE_SHIFT) & 0x01ffUL;

    while ((n > 0) && (idx < 512)) {
        // intersection with this entry
        u64 skip = OFFSET_1G(va) >> PAGE_SHIFT;
        u64 cnt  = MIN(512 * 512 - skip, n);

        if ((YES == support_1g) && (0 != (tbl[idx] & MMU_P)) && (0 != (tbl[idx] & MMU_PS))) {
            // unmap complete 1G big page
            if ((0 == skip) && (512 * 512 == cnt)) {
                tbl[idx] = 0;
                --page_array[pdpf].ent_count;
                va  += 512 * 512 * PAGE_SIZE;
                n   -= 512 * 512;
                num += 512 * 512;
                ++idx;
                continue;
            }

            // retrive old mapping
            u64 pa     = tbl[idx] & MMU_ADDR;
            u64 fields = tbl[idx] & (MMU_US | MMU_RW | MMU_NX);

            // create new page directory table
            u64 * pdt = tbl_alloc();
            tbl[idx] = ((u64) pdt & MMU_ADDR) | MMU_US | MMU_RW | MMU_P;

            // add back extra ranges
            if (0 != skip) {
                pdt_map(pdt, va - skip * PAGE_SIZE, pa - skip * PAGE_SIZE, fields, skip);
            }
            if ((skip + cnt) < 512 * 512) {
                pdt_map(pdt, va + cnt * PAGE_SIZE, pa + cnt * PAGE_SIZE, fields, 512 * 512 - (skip + cnt));
            }

            va  += cnt * PAGE_SIZE;
            n   -= cnt;
            num += cnt;
            ++idx;
            continue;
        }

        if (0 != (tbl[idx] & MMU_P)) {
            u64 * pdt = SUB_TABLE(tbl[idx]);
            pfn_t pdf = (pfn_t) (virt_to_phys(pdt) >> PAGE_SHIFT);

            u64 ret = pdt_unmap(pdt, va, n);
            if (0 == page_array[pdf].ent_count) {
                page_block_free(pdf, 0);
                tbl[idx] = 0;
                --page_array[pdpf].ent_count;
            }

            va  += ret * PAGE_SIZE;
            n   -= ret;
            num += ret;
            ++idx;
        } else {
            va  += cnt * PAGE_SIZE;
            n   -= cnt;
            num += cnt;
            ++idx;
        }
    }

    return num;
}

static u64 pml4t_unmap(u64 * tbl, u64 va, u64 n) {
    pfn_t pml4f = (pfn_t) (virt_to_phys(tbl) >> PAGE_SHIFT);
    u64   num   = 0;
    u64   idx   = (va >> PML4TE_SHIFT) & 0x01ffUL;

    while ((n > 0) && (idx < 512)) {
        // intersection with this entry
        u64 skip = (va & (0x8000000000UL - 1)) >> PAGE_SHIFT;
        u64 cnt  = MIN(512 * 512 * 512 - skip, n);

        if (0 != (tbl[idx] & MMU_P)) {
            u64 * pdpt = SUB_TABLE(tbl[idx]);
            pfn_t pdpf = (pfn_t) (virt_to_phys(pdpt) >> PAGE_SHIFT);

            u64 ret = pdpt_unmap(pdpt, va, n);
            if (0 == page_array[pdpf].ent_count) {
                page_block_free(pdpf, 0);
                tbl[idx] = 0;
                --page_array[pml4f].ent_count;
            }

            va  += ret * PAGE_SIZE;
            n   -= ret;
            num += ret;
            ++idx;
        } else {
            va  += cnt * PAGE_SIZE;
            n   -= cnt;
            num += cnt;
            ++idx;
        }
    }

    return num;
}

//------------------------------------------------------------------------------
// public functions

static u64 kernel_ctx;

usize mmu_ctx_get() {
    return (usize) read_cr3();
}

void mmu_ctx_set(usize ctx) {
    write_cr3((u64) ctx);
}

// create a new context table, allocate space for top-level table
usize mmu_ctx_create() {
    pfn_t pml4t = page_block_alloc_or_fail(ZONE_DMA|ZONE_NORMAL, 0, PT_PGTABLE);
    usize ctx   = (usize) pml4t << PAGE_SHIFT;
    memset(phys_to_virt(ctx), 0, PAGE_SIZE / 2);        // user space
    memcpy(phys_to_virt(ctx)        + PAGE_SIZE / 2,    // kernel space
           phys_to_virt(kernel_ctx) + PAGE_SIZE / 2,
           PAGE_SIZE / 2);
    return ctx;
}

usize mmu_translate(usize ctx, usize va) {
    return pml4t_translate((u64 *) ctx, va);
}

void mmu_map(usize ctx, usize va, usize pa, usize n, u32 attr) {
    u64 fields = 0;
    if (0 == (attr & MMU_KERNEL)) {
        fields |= MMU_US;
    }
    if (0 == (attr & MMU_RDONLY)) {
        fields |= MMU_RW;
    }
    if (0 != (attr & MMU_NOEXEC)) {
        fields |= MMU_NX;
    }
    pml4t_map((u64 *) ctx, va, pa, fields, n);
}

void mmu_unmap(usize ctx, usize va, usize n) {
    pml4t_unmap((u64 *) ctx, va, n);
}

//------------------------------------------------------------------------------
// prepare page table for kernel space

// defined in `layout.ld`
extern u8 _init_end;
extern u8 _text_end;
extern u8 _rodata_end;

__INIT void kernel_ctx_init() {
    usize virt, phys, mark;

    u64 * pml4t = tbl_alloc();
    kernel_ctx = (usize) pml4t;

    // boot, trampoline and init sections
    virt = KERNEL_VMA;
    phys = KERNEL_LMA;
    mark = ROUND_UP(&_init_end, PAGE_SIZE);
    dbg_print("mapping 0x%llx~0x%llx.\n", virt, mark);
    mmu_map(kernel_ctx, virt, phys, (mark - virt) >> PAGE_SHIFT, MMU_KERNEL);

    // kernel code section
    virt = mark;
    phys = virt - KERNEL_VMA + KERNEL_LMA;
    mark = ROUND_UP(&_text_end, PAGE_SIZE);
    dbg_print("mapping 0x%llx~0x%llx.\n", virt, mark);
    mmu_map(kernel_ctx, virt, phys, (mark - virt) >> PAGE_SHIFT, MMU_RDONLY|MMU_KERNEL);

    // kernel read only data section
    virt = mark;
    phys = virt - KERNEL_VMA + KERNEL_LMA;
    mark = ROUND_UP(&_rodata_end, PAGE_SIZE);
    dbg_print("mapping 0x%llx~0x%llx.\n", virt, mark);
    mmu_map(kernel_ctx, virt, phys, (mark - virt) >> PAGE_SHIFT, MMU_RDONLY|MMU_NOEXEC|MMU_KERNEL);

    // kernel data section
    virt = mark;
    phys = virt - KERNEL_VMA + KERNEL_LMA;
    mark = ROUND_UP(&page_array[page_count], PAGE_SIZE);
    dbg_print("mapping 0x%llx~0x%llx.\n", virt, mark);
    mmu_map(kernel_ctx, virt, phys, (mark - virt) >> PAGE_SHIFT, MMU_NOEXEC|MMU_KERNEL);

    // map all physical memory to higher half
    // TODO: only map present pages, and add IO/Local APIC in driver
    // dbg_print("mapping 0x%llx~0x%llx.\n", MAPPED_ADDR, MAPPED_ADDR + (1UL << 32));
    // mmu_map(kernel_ctx, MAPPED_ADDR, 0, 1UL << 20, MMU_NOEXEC|MMU_KERNEL);

    // switch to kernel context
    // mmu_ctx_set(kernel_ctx);
}

__INIT void kernel_ctx_load() {
    mmu_ctx_set(kernel_ctx);
}


















































#if 0

// kernel space page table
usize kernel_ctx = 0UL;

//------------------------------------------------------------------------------
// private functions for creating page entry

// create a 4k mapping entry in the page table
static void mmu_map_4k(usize ctx, u64 va, u64 pa, u64 fields) {
    u64 pe    = (va >> 12) & 0x01ff;    // index of page entry
    u64 pde   = (va >> 21) & 0x01ff;    // index of page-directory entry
    u64 pdpe  = (va >> 30) & 0x01ff;    // index of page-directory-pointer entry
    u64 pml4e = (va >> 39) & 0x01ff;    // index of page-map level-4 entry

    // PML4 table is always present
    u64 * pml4t = (u64 *) phys_to_virt(ctx);
    pfn_t pml4f = (pfn_t) (ctx >> PAGE_SHIFT);
    assert(PT_PGTABLE == page_array[pml4f].type);

    // check if PDP is present
    if (0 == (pml4t[pml4e] & MMU_ADDR)) {
        pfn_t pfn = page_block_alloc_or_fail(ZONE_DMA|ZONE_NORMAL, 0, PT_PGTABLE);
        page_array[pfn].ent_count = 0;
        memset(phys_to_virt((u64) pfn << PAGE_SHIFT), 0, PAGE_SIZE);
        pml4t[pml4e] = ((u64) pfn << PAGE_SHIFT) & MMU_ADDR;
    }
    if (0 == (pml4t[pml4e] & MMU_P)) {
        ++page_array[pml4f].ent_count;
    }
    pml4t[pml4e] |= MMU_US | MMU_RW | MMU_P;
    u64 * pdpt = (u64 *) phys_to_virt(pml4t[pml4e] & MMU_ADDR);
    pfn_t pdpf = (pfn_t) (pml4t[pml4e] >> PAGE_SHIFT);
    assert(PT_PGTABLE == page_array[pdpf].type);

    // check if PD is present
    if (0 == (pdpt[pdpe] & MMU_ADDR)) {
        pfn_t pfn = page_block_alloc_or_fail(ZONE_DMA|ZONE_NORMAL, 0, PT_PGTABLE);
        page_array[pfn].ent_count = 0;
        memset(phys_to_virt((u64) pfn << PAGE_SHIFT), 0, PAGE_SIZE);
        pdpt[pdpe] = ((u64) pfn << PAGE_SHIFT) & MMU_ADDR;
    }
    if (0 == (pdpt[pdpe] & MMU_P)) {
        ++page_array[pdpf].ent_count;
    }
    pdpt[pdpe] |= MMU_US | MMU_RW | MMU_P;
    u64 * pdt = (u64 *) phys_to_virt(pdpt[pdpe] & MMU_ADDR);
    pfn_t pdf = (pfn_t) (pdpt[pdpe] >> PAGE_SHIFT);
    assert(PT_PGTABLE == page_array[pdf].type);

    // check if page table is present
    if (0 == (pdt[pde] & MMU_ADDR)) {
        pfn_t pfn = page_block_alloc_or_fail(ZONE_DMA|ZONE_NORMAL, 0, PT_PGTABLE);
        page_array[pfn].ent_count = 0;
        memset(phys_to_virt((u64) pfn << PAGE_SHIFT), 0, PAGE_SIZE);
        pdt[pde] = ((u64) pfn << PAGE_SHIFT) & MMU_ADDR;
    }
    if (0 == (pdt[pde] & MMU_P)) {
        ++page_array[pdf].ent_count;
    }
    pdt[pde] |= MMU_US | MMU_RW | MMU_P;
    u64 * pt = (u64 *) phys_to_virt(pdt[pde] & MMU_ADDR);
    pfn_t pf = (pfn_t) (pdt[pde] >> PAGE_SHIFT);
    assert(PT_PGTABLE == page_array[pf].type);

    // check if page entry is not mapped
    if (0 == (pt[pe] & MMU_P)) {
        ++page_array[pf].ent_count;
    }
    pt[pe] = (pa & MMU_ADDR) | fields | MMU_P;

    // clear TLB entry
    if (read_cr3() == ctx) {
        ASM("invlpg (%0)" :: "r"(va));
    }
}

// create a 2m mapping entry in the page table
static void mmu_map_2m(usize ctx, u64 va, u64 pa, u64 fields) {
    u64 pde   = (va >> 21) & 0x01ff;    // index of page-directory entry
    u64 pdpe  = (va >> 30) & 0x01ff;    // index of page-directory-pointer entry
    u64 pml4e = (va >> 39) & 0x01ff;    // index of page-map level-4 entry

    // pml4 table is always present
    u64 * pml4t = (u64 *) phys_to_virt(ctx);
    pfn_t pml4f = (pfn_t) (ctx >> PAGE_SHIFT);
    assert(PT_PGTABLE == page_array[pml4f].type);

    // check if PDP is present
    if (0 == (pml4t[pml4e] & MMU_ADDR)) {
        pfn_t pfn = page_block_alloc_or_fail(ZONE_DMA|ZONE_NORMAL, 0, PT_PGTABLE);
        page_array[pfn].ent_count = 0;
        memset(phys_to_virt((u64) pfn << PAGE_SHIFT), 0, PAGE_SIZE);
        pml4t[pml4e] = ((u64) pfn << PAGE_SHIFT) & MMU_ADDR;
    }
    if (0 == (pml4t[pml4e] & MMU_P)) {
        ++page_array[pml4f].ent_count;
    }
    pml4t[pml4e] |= MMU_US | MMU_RW | MMU_P;
    u64 * pdpt = (u64 *) phys_to_virt(pml4t[pml4e] & MMU_ADDR);
    pfn_t pdpf = (pfn_t) (pml4t[pml4e] >> PAGE_SHIFT);
    assert(PT_PGTABLE == page_array[pdpf].type);

    // check if PD is present
    if (0 == (pdpt[pdpe] & MMU_ADDR)) {
        pfn_t pfn = page_block_alloc_or_fail(ZONE_DMA|ZONE_NORMAL, 0, PT_PGTABLE);
        page_array[pfn].ent_count = 0;
        memset(phys_to_virt((u64) pfn << PAGE_SHIFT), 0, PAGE_SIZE);
        pdpt[pdpe] = ((u64) pfn << PAGE_SHIFT) & MMU_ADDR;
    }
    if (0 == (pdpt[pdpe] & MMU_P)) {
        ++page_array[pdpf].ent_count;
    }
    pdpt[pdpe] |= MMU_US | MMU_RW | MMU_P;
    u64 * pdt = (u64 *) phys_to_virt(pdpt[pdpe] & MMU_ADDR);
    pfn_t pdf = (pfn_t) (pdpt[pdpe] >> PAGE_SHIFT);
    assert(PT_PGTABLE == page_array[pdf].type);

    // check if PDE is already mapped
    if (0 == (pdt[pde] & MMU_P)) {
        ++page_array[pdf].ent_count;
    }
    pdt[pde] = (pa & MMU_ADDR) | fields | MMU_PS | MMU_P;

    // clear TLB entry
    if (read_cr3() == ctx) {
        ASM("invlpg (%0)" :: "r"(va));
    }
}

//------------------------------------------------------------------------------
// public functions

usize mmu_ctx_get() {
    return (usize) read_cr3();
}

void mmu_ctx_set(usize ctx) {
    write_cr3((u64) ctx);
}

// create a new context table, allocate space for top-level table
usize mmu_ctx_create() {
    pfn_t pml4t = page_block_alloc_or_fail(ZONE_DMA|ZONE_NORMAL, 0, PT_PGTABLE);
    usize ctx   = (usize) pml4t << PAGE_SHIFT;
    memset(phys_to_virt(ctx), 0, PAGE_SIZE / 2);        // user space
    memcpy(phys_to_virt(ctx)        + PAGE_SIZE / 2,    // kernel space
           phys_to_virt(kernel_ctx) + PAGE_SIZE / 2,
           PAGE_SIZE / 2);
    return ctx;
}

// perform address translation by checking page table
// return NO_ADDR if mapping for `va` is not present
usize mmu_translate(usize ctx, usize va) {
    u64 pe    = (va >> 12) & 0x01ff;
    u64 pde   = (va >> 21) & 0x01ff;
    u64 pdpe  = (va >> 30) & 0x01ff;
    u64 pml4e = (va >> 39) & 0x01ff;

    u64 * pml4t = (u64 *) phys_to_virt(ctx);
    assert(PT_PGTABLE == page_array[ctx >> PAGE_SHIFT].type);
    if (0 == (pml4t[pml4e] & MMU_P)) {
        return NO_ADDR;
    }

    u64 * pdpt = (u64 *) phys_to_virt(pml4t[pml4e] & MMU_ADDR);
    assert(PT_PGTABLE == page_array[pml4t[pml4e] >> PAGE_SHIFT].type);
    if (0 == (pdpt[pdpe] & MMU_P)) {
        return NO_ADDR;
    }

    u64 * pdt = (u64 *) phys_to_virt(pdpt[pdpe] & MMU_ADDR);
    assert(PT_PGTABLE == page_array[pdpt[pdpe] >> PAGE_SHIFT].type);
    if (0 == (pdt[pde] & MMU_P)) {
        return NO_ADDR;
    }

    if (0 != (pdt[pde] & MMU_PS)) {
        u64 base = pdt[pde] & MMU_ADDR;
        assert(0 == (base & (0x200000 - 1)));
        return base + (va & (0x200000 - 1));
    }

    u64 * pt = (u64 *) phys_to_virt(pdt[pde] & MMU_ADDR);
    assert(PT_PGTABLE == page_array[pdt[pde] >> PAGE_SHIFT].type);
    if (0 == (pt[pe] & MMU_P)) {
        return NO_ADDR;
    }

    return (pt[pe] & MMU_ADDR) + (va & (0x1000 - 1));
}

// create mapping from va to pa, overwriting existing mapping
void mmu_map(usize ctx, usize va, usize pa, usize n, u32 attr) {
    u64 v = (u64) va;
    u64 p = (u64) pa;

    assert(IS_ALIGNED(v, PAGE_SIZE));
    assert(IS_ALIGNED(p, PAGE_SIZE));

    u64 fields = 0;
    if (0 == (attr & MMU_KERNEL)) {
        fields |= MMU_US;
    }
    if (0 == (attr & MMU_RDONLY)) {
        fields |= MMU_RW;
    }
    if (0 != (attr & MMU_NOEXEC)) {
        fields |= MMU_NX;
    }

    while (n) {
        if ((n >= 512)              &&
            IS_ALIGNED(v, 0x200000) &&
            IS_ALIGNED(p, 0x200000)) {
            // use 2M pages whenever possible
            mmu_map_2m(ctx, v, p, fields);
            v += 0x200000;
            p += 0x200000;
            n -= 512;
        } else {
            mmu_map_4k(ctx, v, p, fields);
            v += PAGE_SIZE;
            p += PAGE_SIZE;
            n -= 1;
        }
    }
}

void mmu_unmap(usize ctx, usize va, usize n) {
    u64 * pml4t = (u64 *) phys_to_virt(ctx);
    pfn_t pml4f = (pfn_t) (ctx >> PAGE_SHIFT);
    assert(PT_PGTABLE == page_array[pml4f].type);

    usize end   = va + n * PAGE_SIZE;
    for (; va < end; va += PAGE_SIZE) {
        u64 pte   = (va >> 12) & 0x01ff;
        u64 pde   = (va >> 21) & 0x01ff;
        u64 pdpe  = (va >> 30) & 0x01ff;
        u64 pml4e = (va >> 39) & 0x01ff;

        if (0 == (pml4t[pml4e] & MMU_P)) {
            continue;
        }

        u64 * pdpt = (u64 *) phys_to_virt(pml4t[pml4e] & MMU_ADDR);
        pfn_t pdpf = (pfn_t) (pml4t[pml4e] >> PAGE_SHIFT);
        if (0 == (pdpt[pdpe] & MMU_P)) {
            continue;
        }

        u64 * pdt = (u64 *) phys_to_virt(pdpt[pdpe] & MMU_ADDR);
        if (0 == (pdt[pde] & MMU_P)) {
            continue;
        }

        if (0 != (pdt[pde] & MMU_PS)) {
            // 2M page size, first retrieve current mapping
            u64 va_2m  = va & ~(0x200000 - 1);
            u64 pa_2m  = pdt[pde] & MMU_ADDR;
            u64 fields = pdt[pde] & (MMU_US | MMU_RW | MMU_NX);
            assert(0 == (pa_2m & (0x200000 - 1)));

            // remove current mapping
            pdt[pde] &= ~MMU_P;
            // --page_array[]
            // if (YES == free) {
            //     page_block_free((pfn_t) (pdt[pde] >> PAGE_SHIFT), 0);
            //     pdt[pde] = 0;
            // }

            // if unmap range is less than 2M, add back rest range
            if (pte > 0) {
                mmu_map(ctx, va_2m, pa_2m, pte, fields);
            }
            if (end < va + PAGE_SIZE * 512) {
                pa_2m += end - va_2m;
                usize n = (va + PAGE_SIZE * 512 - end) >> PAGE_SHIFT;
                mmu_map(ctx, end, pa_2m, n, fields);
            }

            continue;
        }

        u64 * pt = (u64 *) phys_to_virt(pdt[pde] & MMU_ADDR);
        if (0 == (pt[pte] & MMU_P)) {
            continue;
        }
    }
}

//------------------------------------------------------------------------------
// create page table for kernel range

#endif
