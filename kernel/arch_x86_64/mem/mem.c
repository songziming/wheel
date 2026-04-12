#include "mem.h"
#include <arch_api.h>
#include <vmspace.h>
#include <pmlayout.h>
#include <early_alloc.h>
#include <page.h>
#include <debug.h>


// layout.ld
extern char _init_end;
extern char _text_addr;
extern char _text_end;
extern char _rodata_addr;
extern char _data_addr;

// 内核地址空间布局
vmspace_t g_kernel_vm;

// 内核段
static vmrange_t g_kernel_init;
static vmrange_t g_kernel_text;
static vmrange_t g_kernel_rodata;
static vmrange_t g_kernel_data;
// static vmrange_t g_kernel_heap;
static vmrange_t g_kernel_idmap; // 不拥有物理页，所有内存映射到 canonical hole 之下



// 记录一段虚拟地址范围
INIT_TEXT void kspace_add(vmrange_t *rng, size_t va, size_t end, const char *desc, mmu_attr_t attrs) {
    ASSERT(0 == (va & (PAGE_SIZE - 1)));
    rng->vaddr = va;
    rng->vend  = end;
    rng->desc  = desc;
    rng->attrs = attrs;
    if (va >= KERNEL_TEXT_ADDR) {
        rng->paddr = va - KERNEL_TEXT_ADDR;
    } else if (va >= DIRECT_MAP_ADDR) {
        rng->paddr = va - DIRECT_MAP_ADDR;
    } else {
        panic("do not support address 0x%zx\n", va);
    }
    vmspace_insert(&g_kernel_vm, rng);
}


// 初始化内存布局，规划内核虚拟地址空间
INIT_TEXT void mem_init() {
    // 找出物理内存的结束地址
    size_t pa_end = 0;
    for (int i = g_pmrange_num - 1; i >= 0; --i) {
        pmtype_t type = g_pmranges[i].type;
        if ((PM_AVAILABLE == type) || (PM_RECLAIMABLE == type)) {
            pa_end = g_pmranges[i].end;
            break;
        }
    }

    // 1M 以下属于 low-mem，不可分配
    page_init(0x100000, pa_end);

    // 不再使用 early-alloc
    size_t ro_end = (size_t)early_alloc_ro(0);
    size_t rw_end = (size_t)early_alloc_rw(0);
    early_alloc_disable();

    // 记录内核的虚拟地址空间布局
    vmspace_init(&g_kernel_vm);
    size_t init_addr = KERNEL_TEXT_ADDR + KERNEL_LOAD_ADDR;
    kspace_add(&g_kernel_init, init_addr, (size_t)&_init_end, "init", MMU_WRITE|MMU_EXEC);
    kspace_add(&g_kernel_text, (size_t)&_text_addr, (size_t)&_text_end, "text", MMU_EXEC);
    kspace_add(&g_kernel_rodata, (size_t)&_rodata_addr, ro_end, "rodata", MMU_NONE); // 含 early_ro
    kspace_add(&g_kernel_data, (size_t)&_data_addr, rw_end, "data", MMU_WRITE); // 含 bss、early_rw

    // TODO 划分内核堆

    // 分配 percpu 空间
    rw_end = percpu_init(rw_end);

    // 记录内核结束位置的物理
    size_t kend = rw_end - KERNEL_TEXT_ADDR;

    // 遍历物理内存，将可用部分逐一添加（跳过内核占用的部分）
    for (int i = 0; i < g_pmrange_num; ++i) {
        pmtype_t type = g_pmranges[i].type;
        if ((PM_AVAILABLE != type) && (PM_RECLAIMABLE != type)) {
            continue;
        }

        size_t start = g_pmranges[i].start;
        size_t end   = g_pmranges[i].end;

        // 内核必然完整包含于一段 pmrange
        if ((start <= KERNEL_LOAD_ADDR) && (kend <= end)) {
            pages_add(start, KERNEL_LOAD_ADDR);
            pages_add(kend, end);
        } else {
            pages_add(start, end);
        }
    }

    // 内核占据的虚拟内存不是连续的，相邻 range 之间留有 gap，将这些 gap 对应的物理页回收
    dlnode_t *dl0 = g_kernel_vm.head.next;
    dlnode_t *dl1 = dl0->next;
    while (dl1 != &g_kernel_vm.head) {
        vmrange_t *prev = containerof(dl0, vmrange_t, dl);
        vmrange_t *curr = containerof(dl1, vmrange_t, dl);
        size_t gap_addr = prev->vend - KERNEL_TEXT_ADDR;
        size_t gap_end = curr->vaddr - KERNEL_TEXT_ADDR;
        // logk("adding gap between `%s` and `%s`, va [%zx,%zx), pa [%zx,%zx)\n",
        //     prev->desc, curr->desc, prev->vend, curr->vaddr, gap_addr, gap_end);
        pages_add(gap_addr, gap_end);

        dl0 = dl1;
        dl1 = dl1->next;
    }

    // 把全部物理内存映射到 canonical hole 之后
    // MMIO 范围可能在可用内存范围之外，也要映射
    if (pa_end < (1UL << 32)) {
        pa_end = 1UL << 32;
    }
    kspace_add(&g_kernel_idmap, DIRECT_MAP_ADDR, DIRECT_MAP_ADDR + pa_end, "idmap", MMU_WRITE);

    // 创建内核页表，根据 vmspace 添加映射
    g_kernel_vm.table = mmu_create();
    for (dlnode_t *i = g_kernel_vm.head.next; i != &g_kernel_vm.head; i = i->next) {
        vmrange_t *rng = containerof(i, vmrange_t, dl);
        size_t va_end = (rng->vend + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        mmu_map(g_kernel_vm.table, rng->vaddr, va_end, rng->paddr, rng->attrs);
    }
}
