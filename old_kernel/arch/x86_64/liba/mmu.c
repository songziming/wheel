#include <wheel.h>

// different fields of virtual memory address
#define PML4TE_SHIFT     39                     // page-map level-4 table
#define PDPTE_SHIFT      30                     // page-directory-pointer table
#define PDTE_SHIFT       21                     // page-directory table
#define PTE_SHIFT        12                     // page table

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

// alignment check
#define OFFSET_4K(x)    ((x) & (0x1000UL - 1))
#define OFFSET_2M(x)    ((x) & (0x200000UL - 1))
#define OFFSET_1G(x)    ((x) & (0x40000000UL - 1))

// page tables use physical address, the translation is very simple
#define VIRT(x)         ((x) |  0xffff800000000000UL)
#define PHYS(x)         ((x) & ~0xffff800000000000UL)

static int support_pcid = NO;
static int support_nx   = NO;
static int support_1g   = NO;

//------------------------------------------------------------------------------
// create new page table, return physical address

static u64 tbl_alloc() {
    pfn_t page = page_block_alloc_or_fail(ZONE_DMA|ZONE_NORMAL, 0, PT_PGTABLE);
    u64   phys = (u64) page << PAGE_SHIFT;
    u64 * virt = (u64 *) VIRT(phys);
    page_array[page].ent_count = 0;
    memset(virt, 0, PAGE_SIZE);
    return phys;
}

//------------------------------------------------------------------------------
// delete table and all entries inside, all tables are physical address

static void pt_free(u64 tbl) {
    page_block_free((pfn_t) (tbl >> PAGE_SHIFT), 0);
}

static void pdt_free(u64 tbl) {
    pfn_t pdf = (pfn_t) (tbl >> PAGE_SHIFT);
    u64 * pdt = (u64 *) VIRT(tbl);
    if (0 == page_array[pdf].ent_count) {
        page_block_free(pdf, 0);
        return;
    }
    for (int i = 0; i < 512; ++i) {
        if (0 == (pdt[i] & MMU_P)) {
            continue;
        } else if (0 != (pdt[i] & MMU_PS)) {
            continue;
        }
        pt_free(pdt[i] & MMU_ADDR);
    }
}

static void pdpt_free(u64 tbl) {
    pfn_t pdpf = (pfn_t) (tbl >> PAGE_SHIFT);
    u64 * pdpt = (u64 *) VIRT(tbl);
    if (0 == page_array[pdpf].ent_count) {
        page_block_free(pdpf, 0);
        return;
    }
    for (int i = 0; i < 512; ++i) {
        if (0 == (pdpt[i] & MMU_P)) {
            continue;
        } else if ((YES == support_1g) && (0 != (pdpt[i] & MMU_PS))) {
            continue;
        }
        pdt_free(pdpt[i] & MMU_ADDR);
    }
}

static void pml4t_free(u64 tbl) {
    pfn_t pml4f = (pfn_t) (tbl >> PAGE_SHIFT);
    u64 * pml4t = (u64 *) VIRT(tbl);
    if (0 == page_array[pml4f].ent_count) {
        page_block_free(pml4f, 0);
        return;
    }
    for (int i = 0; i < 512; ++i) {
        if (0 == (pml4t[i] & MMU_P)) {
            continue;
        }
        pdpt_free(pml4t[i] & MMU_ADDR);
    }
}

//------------------------------------------------------------------------------
// address translation, return physical address

static u64 pt_translate(u64 tbl, u64 va) {
    u64 * pt  = (u64 *) VIRT(tbl);
    u64   idx = (va >> PTE_SHIFT) & 0x01ffUL;
    if (0 == (pt[idx] & MMU_P)) {
        return NO_ADDR;
    }
    return (pt[idx] & MMU_ADDR) | OFFSET_4K(va);
}

static u64 pdt_translate(u64 tbl, u64 va) {
    u64 * pdt = (u64 *) VIRT(tbl);
    u64   idx = (va >> PDTE_SHIFT) & 0x01ffUL;
    if (0 == (pdt[idx] & MMU_P)) {
        return NO_ADDR;
    }
    if (0 != (pdt[idx] & MMU_PS)) {
        assert(0 == OFFSET_2M(pdt[idx] & MMU_ADDR));
        return (pdt[idx] & MMU_ADDR) | OFFSET_2M(va);
    }
    return pt_translate(pdt[idx] & MMU_ADDR, va);
}

static u64 pdpt_translate(u64 tbl, u64 va) {
    u64 * pdpt = (u64 *) VIRT(tbl);
    u64   idx  = (va >> PDPTE_SHIFT) & 0x01ffUL;
    if (0 == (pdpt[idx] & MMU_P)) {
        return NO_ADDR;
    }
    if ((YES == support_1g) && (0 != (pdpt[idx] & MMU_PS))) {
        assert(0 == OFFSET_1G(pdpt[idx] & MMU_ADDR));
        return (pdpt[idx] & MMU_ADDR) | OFFSET_1G(va);
    }
    return pdt_translate(pdpt[idx] & MMU_ADDR, va);
}

static u64 pml4t_translate(u64 tbl, u64 va) {
    u64 * pml4t = (u64 *) VIRT(tbl);
    u64   idx   = (va >> PML4TE_SHIFT) & 0x01ffUL;
    if (0 == (pml4t[idx] & MMU_P)) {
        return NO_ADDR;
    }
    return pdpt_translate(pml4t[idx], va);
}

//------------------------------------------------------------------------------
// add new mapping, return the number of pages mapped
// overwrite existing mapping without warning
// won't merge into 2M and 1G large pages

static u64 pt_map(u64 tbl, u64 va, u64 pa, u64 fields, u64 n) {
    u64 * pt  = (u64 *) VIRT(tbl);
    pfn_t pf  = (pfn_t) (tbl >> PAGE_SHIFT);
    u64   idx = (va >> PTE_SHIFT) & 0x01ffUL;
    u64   num = 0;

    while ((n > 0) && (idx < 512)) {
        if (0 == (pt[idx] & MMU_P)) {
            ++page_array[pf].ent_count;
        }

        pt[idx] = (pa & MMU_ADDR) | fields | MMU_P;
        pa += PAGE_SIZE;
        --n;
        ++num;
        ++idx;
    }

    return num;
}

static u64 pdt_map(u64 tbl, u64 va, u64 pa, u64 fields, u64 n) {
    u64 * pdt = (u64 *) VIRT(tbl);
    pfn_t pdf = (pfn_t) (tbl >> PAGE_SHIFT);
    u64   idx = (va >> PDTE_SHIFT) & 0x01ffUL;
    u64   num = 0;

    while ((n > 0) && (idx < 512)) {
        if ((n >= 512) && (0 == OFFSET_2M(va)) && (0 == OFFSET_2M(pa))) {
            if (0 == (pdt[idx] & MMU_P)) {
                ++page_array[pdf].ent_count;
            } else if (0 == (pdt[idx] & MMU_PS)) {
                pt_free(pdt[idx] & MMU_ADDR);
            }

            // create 2M entry
            pdt[idx] = (pa & MMU_ADDR) | fields | MMU_PS | MMU_P;
            va  += 512 * PAGE_SIZE;
            pa  += 512 * PAGE_SIZE;
            n   -= 512;
            num += 512;
            ++idx;
            continue;
        }

        // create new page table if not present
        if (0 == (pdt[idx] & MMU_P)) {
            pdt[idx] = (tbl_alloc() & MMU_ADDR) | MMU_US | MMU_RW | MMU_P;
            ++page_array[pdf].ent_count;
        }

        // fill page table
        u64 ret = pt_map(pdt[idx] & MMU_ADDR, va, pa, fields, n);
        va  += ret * PAGE_SIZE;
        pa  += ret * PAGE_SIZE;
        n   -= ret;
        num += ret;
        ++idx;
    }

    return num;
}

static u64 pdpt_map(u64 tbl, u64 va, u64 pa, u64 fields, u64 n) {
    u64 * pdpt = (u64 *) VIRT(tbl);
    pfn_t pdpf = (pfn_t) (tbl >> PAGE_SHIFT);
    u64   idx  = (va >> PDPTE_SHIFT) & 0x01ffUL;
    u64   num  = 0;

    while ((n > 0) && (idx < 512)) {
        if ((YES == support_1g) && (n >= 512 * 512) && (0 == OFFSET_1G(va)) && (0 == OFFSET_1G(pa))) {
            if (0 == (pdpt[idx] & MMU_P)) {
                ++page_array[pdpf].ent_count;
            } else if (0 == (pdpt[idx] & MMU_PS)) {
                pdt_free(pdpt[idx] & MMU_ADDR);
            }

            // add 1G entry
            pdpt[idx] = (pa & MMU_ADDR) | fields | MMU_PS | MMU_P;
            va  += 512 * 512 * PAGE_SIZE;
            pa  += 512 * 512 * PAGE_SIZE;
            n   -= 512 * 512;
            num += 512 * 512;
            ++idx;
            continue;
        }

        // create new page directory table if not present
        if (0 == (pdpt[idx] & MMU_P)) {
            pdpt[idx] = (tbl_alloc() & MMU_ADDR) | MMU_US | MMU_RW | MMU_P;
            ++page_array[pdpf].ent_count;
        }

        // fill page directory table
        u64 ret = pdt_map(pdpt[idx] & MMU_ADDR, va, pa, fields, n);
        va  += ret * PAGE_SIZE;
        pa  += ret * PAGE_SIZE;
        n   -= ret;
        num += ret;
        ++idx;
    }

    return num;
}

static u64 pml4t_map(u64 tbl, u64 va, u64 pa, u64 fields, u64 n) {
    u64 * pml4t = (u64 *) VIRT(tbl);
    pfn_t pml4f = (pfn_t) (tbl >> PAGE_SHIFT);
    u64   idx   = (va >> PML4TE_SHIFT) & 0x01ffUL;
    u64   num   = 0;

    while ((n > 0) && (idx < 512)) {
        // create new page directory pointer table if not present
        if (0 == (pml4t[idx] & MMU_P)) {
            pml4t[idx] = (tbl_alloc() & MMU_ADDR) | MMU_US | MMU_RW | MMU_P;
            ++page_array[pml4f].ent_count;
        }

        // fill page directory pointer table
        u64 ret  = pdpt_map(pml4t[idx] & MMU_ADDR, va, pa, fields, n);
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

static u64 pt_unmap(u64 tbl, u64 va, u64 n) {
    u64 * pt  = (u64 *) VIRT(tbl);
    pfn_t pf  = (pfn_t) (tbl >> PAGE_SHIFT);
    u64   idx = (va >> PTE_SHIFT) & 0x01ffUL;
    u64   num = 0;

    while ((n > 0) && (idx < 512)) {
        if (0 != (pt[idx] & MMU_P)) {
            --page_array[pf].ent_count;
        }
        pt[idx] = 0;
        n   -= 1;
        num += 1;
        ++idx;
    }

    return num;
}

static u64 pdt_unmap(u64 tbl, u64 va, u64 n) {
    u64 * pdt = (u64 *) VIRT(tbl);
    pfn_t pdf = (pfn_t) (tbl >> PAGE_SHIFT);
    u64   idx = (va >> PDTE_SHIFT) & 0x01ffUL;
    u64   num = 0;

    while ((n > 0) && (idx < 512)) {
        // how many pages intersects with this entry
        u64 skip = OFFSET_2M(va) >> PAGE_SHIFT;
        u64 cnt  = MIN(512 - skip, n);

        if ((0 != (pdt[idx] & MMU_P)) && (0 != (pdt[idx] & MMU_PS))) {
            // unmap complete 2M big page
            if ((0 == skip) && (512 == cnt)) {
                pdt[idx] = 0;
                --page_array[pdf].ent_count;
                va  += 512 * PAGE_SIZE;
                n   -= 512;
                num += 512;
                ++idx;
                continue;
            }

            // retrive old mapping and create new page table
            u64 pa     = pdt[idx] & MMU_ADDR;
            u64 fields = pdt[idx] & (MMU_US | MMU_RW | MMU_NX | MMU_G);
            pdt[idx]   = (tbl_alloc() & MMU_ADDR) | MMU_US | MMU_RW | MMU_P;

            // add back extra ranges
            if (0 != skip) {
                u64 va2 = va - skip * PAGE_SIZE;
                u64 pa2 = pa - skip * PAGE_SIZE;
                pt_map(pdt[idx] & MMU_ADDR, va2, pa2, fields, skip);
            }
            if ((skip + cnt) < 512) {
                u64 va2 = va + cnt * PAGE_SIZE;
                u64 pa2 = pa + cnt * PAGE_SIZE;
                pt_map(pdt[idx] & MMU_ADDR, va2, pa2, fields, 512 - (skip + cnt));
            }

            va  += cnt * PAGE_SIZE;
            n   -= cnt;
            num += cnt;
            ++idx;
            continue;
        }

        if (0 != (pdt[idx] & MMU_P)) {
            pfn_t pf  = (pfn_t) ((pdt[idx] & MMU_ADDR) >> PAGE_SHIFT);
            u64   ret = pt_unmap(pdt[idx] & MMU_ADDR, va, n);
            if (0 == page_array[pf].ent_count) {
                page_block_free(pf, 0);
                pdt[idx] = 0;
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

static u64 pdpt_unmap(u64 tbl, u64 va, u64 n) {
    u64 * pdpt = (u64 *) VIRT(tbl);
    pfn_t pdpf = (pfn_t) (tbl >> PAGE_SHIFT);
    u64   idx  = (va >> PDPTE_SHIFT) & 0x01ffUL;
    u64   num  = 0;

    while ((n > 0) && (idx < 512)) {
        // intersection with this entry
        u64 skip = OFFSET_1G(va) >> PAGE_SHIFT;
        u64 cnt  = MIN(512 * 512 - skip, n);

        if ((YES == support_1g) && (0 != (pdpt[idx] & MMU_P)) && (0 != (pdpt[idx] & MMU_PS))) {
            // unmap complete 1G big page
            if ((0 == skip) && (512 * 512 == cnt)) {
                pdpt[idx] = 0;
                --page_array[pdpf].ent_count;
                va  += 512 * 512 * PAGE_SIZE;
                n   -= 512 * 512;
                num += 512 * 512;
                ++idx;
                continue;
            }

            // retrive old mapping and create new page directory table
            u64 pa     = pdpt[idx] & MMU_ADDR;
            u64 fields = pdpt[idx] & (MMU_US | MMU_RW | MMU_NX | MMU_G);
            pdpt[idx]  = (tbl_alloc() & MMU_ADDR) | MMU_US | MMU_RW | MMU_P;

            // add back extra ranges
            if (0 != skip) {
                u64 va2 = va - skip * PAGE_SIZE;
                u64 pa2 = pa - skip * PAGE_SIZE;
                pdt_map(pdpt[idx] & MMU_ADDR, va2, pa2, fields, skip);
            }
            if ((skip + cnt) < 512 * 512) {
                u64 va2 = va + cnt * PAGE_SIZE;
                u64 pa2 = pa + cnt * PAGE_SIZE;
                pdt_map(pdpt[idx] & MMU_ADDR, va2, pa2, fields, 512 * 512 - (skip + cnt));
            }

            va  += cnt * PAGE_SIZE;
            n   -= cnt;
            num += cnt;
            ++idx;
            continue;
        }

        if (0 != (pdpt[idx] & MMU_P)) {
            pfn_t pdf = (pfn_t) ((pdpt[idx] & MMU_ADDR) >> PAGE_SHIFT);
            u64   ret = pdt_unmap(pdpt[idx] & MMU_ADDR, va, n);
            if (0 == page_array[pdf].ent_count) {
                page_block_free(pdf, 0);
                pdpt[idx] = 0;
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

static u64 pml4t_unmap(u64 tbl, u64 va, u64 n) {
    u64 * pml4t = (u64 *) VIRT(tbl);
    pfn_t pml4f = (pfn_t) (tbl >> PAGE_SHIFT);
    u64   idx   = (va >> PML4TE_SHIFT) & 0x01ffUL;
    u64   num   = 0;

    while ((n > 0) && (idx < 512)) {
        // intersection with this entry
        u64 skip = (va & (0x8000000000UL - 1)) >> PAGE_SHIFT;
        u64 cnt  = MIN(512 * 512 * 512 - skip, n);

        if (0 != (pml4t[idx] & MMU_P)) {
            pfn_t pdpf = (pfn_t) ((pml4t[idx] & MMU_ADDR) >> PAGE_SHIFT);
            u64   ret  = pdpt_unmap(pml4t[idx] & MMU_ADDR, va, n);
            if (0 == page_array[pdpf].ent_count) {
                // TODO: we should never free pdpt of the last 256 entry
                //       they refer to the kernel space range
                page_block_free(pdpf, 0);
                pml4t[idx] = 0;
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

static u64   kernel_ctx   = 0;
static u64 * kernel_pml4t = NULL;

usize mmu_ctx_get() {
    return (usize) read_cr3() & MMU_ADDR;
}

void mmu_ctx_set(usize ctx) {
    write_cr3((u64) ctx & MMU_ADDR);
}

// create a new context table, allocate space for top-level table
usize mmu_ctx_create() {
    u64   phys  = tbl_alloc();
    u64 * pml4t = (u64 *) VIRT(phys);
    memcpy(&pml4t[256], &kernel_pml4t[256], 256 * sizeof(u64));
    return phys;
}

void mmu_ctx_delete(usize ctx) {
    pml4t_free(ctx);
}

usize mmu_translate(usize ctx, usize va) {
    return pml4t_translate(ctx, va);
}

void mmu_map(usize ctx, usize va, usize pa, u32 attr, usize n) {
    u64 fields = 0;
    if (0 == (attr & MMU_KERNEL)) {
        fields |= MMU_US;
    }
    if (0 == (attr & MMU_RDONLY)) {
        fields |= MMU_RW;
    }
    if ((YES == support_nx) && (0 != (attr & MMU_NOEXEC))) {
        fields |= MMU_NX;
    }
    pml4t_map(ctx, va, pa, fields, n);
}

void mmu_unmap(usize ctx, usize va, usize n) {
    pml4t_unmap(ctx, va, n);
}

void tlb_flush(usize va, usize n) {
    for (unsigned int i = 0; i < n; ++i) {
        ASM("invlpg (%0)" :: "r"(va));
        va += PAGE_SIZE;
    }
}

//------------------------------------------------------------------------------
// prepare page table for kernel space

// defined in `layout.ld`
extern u8 _init_end;
extern u8 _text_end;
extern u8 _rodata_end;

__INIT void mmu_lib_init() {
    if (0 == cpu_activated) {
        u32 a, b, c, d;

        // processor info
        a = 1;
        cpuid(&a, &b, &c, &d);
        support_pcid = (c & (1U << 17)) ? YES : NO;

        // extended processor info
        a = 0x80000001U;
        cpuid(&a, &b, &c, &d);
        support_nx = (c & (1U << 11)) ? YES : NO;
        support_1g = (d & (1U << 26)) ? YES : NO;
    }

    if (YES == support_pcid) {
        // u64 cr4 = read_cr4();
        // write_cr4(cr4 | (1U << 17));
    }

    if (YES == support_nx) {
        u64 efer = read_msr(0xc0000080U);
        efer |= (1UL << 11);
        write_msr(0xc0000080U, efer);
    }
}

__INIT void kernel_ctx_init() {
    usize virt, phys, mark;

    kernel_ctx   = tbl_alloc();
    kernel_pml4t = (u64 *) VIRT(kernel_ctx);

    u64 mmu_nx = (YES == support_nx) ? MMU_NX : 0;

    // boot, trampoline and init sections
    virt = KERNEL_VMA;
    phys = KERNEL_LMA;
    mark = ROUND_UP(&_init_end, PAGE_SIZE);
    // dbg_print("[~] map  0x%016llx~0x%016llx (init)\n", virt, mark - 1);
    pml4t_map(kernel_ctx, virt, phys, MMU_RW|MMU_G, (mark - virt) >> PAGE_SHIFT);

    // kernel code section
    virt = mark;
    phys = virt - KERNEL_VMA + KERNEL_LMA;
    mark = ROUND_UP(&_text_end, PAGE_SIZE);
    // dbg_print("[~] map  0x%016llx~0x%016llx (code)\n", virt, mark - 1);
    pml4t_map(kernel_ctx, virt, phys, MMU_G, (mark - virt) >> PAGE_SHIFT);

    // kernel read only data section
    virt = mark;
    phys = virt - KERNEL_VMA + KERNEL_LMA;
    mark = ROUND_UP(&_rodata_end, PAGE_SIZE);
    // dbg_print("[~] map  0x%016llx~0x%016llx (rodata)\n", virt, mark - 1);
    pml4t_map(kernel_ctx, virt, phys, mmu_nx|MMU_G, (mark - virt) >> PAGE_SHIFT);

    // kernel data section
    virt = mark;
    phys = virt - KERNEL_VMA + KERNEL_LMA;
    mark = ROUND_UP(&page_array[page_count], PAGE_SIZE);
    // dbg_print("[~] map  0x%016llx~0x%016llx (data)\n", virt, mark - 1);
    pml4t_map(kernel_ctx, virt, phys, mmu_nx|MMU_RW|MMU_G, (mark - virt) >> PAGE_SHIFT);

    // map all physical memory to higher half
    // TODO: only map present pages, and IO spaces
    virt = MAPPED_ADDR;
    phys = 0;
    mark = MAPPED_ADDR + (1UL << 32);
    // dbg_print("[~] map  0x%016llx~0x%016llx (all memory)\n", virt, mark - 1);
    pml4t_map(kernel_ctx, virt, phys, mmu_nx|MMU_RW|MMU_G, (mark - virt) >> PAGE_SHIFT);

    // switch to kernel context
    mmu_ctx_set(kernel_ctx);
}

__INIT void kernel_ctx_load() {
    mmu_ctx_set(kernel_ctx);
}
