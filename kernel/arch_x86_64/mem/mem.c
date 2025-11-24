#include "mem.h"
#include <arch_api.h>
#include <vmspace.h>
#include <pmlayout.h>
#include <early_alloc.h>
#include <debug.h>


// layout.ld
extern char _init_end;
extern char _text_addr;
extern char _text_end;
extern char _rodata_addr;
extern char _data_addr;

// 内核地址空间布局
static vmspace_t g_kernel_vm;

// 内核段
static vmrange_t g_kernel_init;
static vmrange_t g_kernel_text;
static vmrange_t g_kernel_rodata;
static vmrange_t g_kernel_data;
// static vmrange_t g_kernel_heap;
// static vmrange_t g_kernel_idmap;



INIT_TEXT void kspace_add(vmrange_t *rng, size_t va, size_t end, const char *desc) {
    va += PAGE_SIZE - 1;
    va &= ~(PAGE_SIZE - 1);
    rng->vaddr = va;
    rng->vend  = end;
    rng->desc  = desc;
    vmspace_insert(&g_kernel_vm, rng);
}

static void pages_add(size_t start, size_t end) {
    start +=   PAGE_SIZE - 1;
    start &= ~(PAGE_SIZE - 1);
    end   &= ~(PAGE_SIZE - 1);
    if (start < end) {
        logk("  + usable pages 0x%lx~0x%lx\n");
    }
}


// 初始化内存布局，规划内核虚拟地址空间
INIT_TEXT void mem_init() {
    // 不再使用 early-alloc
    size_t ro_end = (size_t)early_alloc_ro(0);
    size_t rw_end = (size_t)early_alloc_rw(0);
    early_alloc_disable();

    // 记录内核的虚拟地址空间布局
    vmspace_init(&g_kernel_vm);

    // 把内核地址空间布局记录下来
    size_t init_addr = KERNEL_TEXT_ADDR + KERNEL_LOAD_ADDR;
    kspace_add(&g_kernel_init, init_addr, (size_t)&_init_end, "init");
    kspace_add(&g_kernel_text, (size_t)&_text_addr, (size_t)&_text_end, "text");
    kspace_add(&g_kernel_rodata, (size_t)&_rodata_addr, ro_end, "rodata"); // 含 early_ro
    kspace_add(&g_kernel_data, (size_t)&_data_addr, rw_end, "data"); // 含 bss、early_rw

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

        // 跳过 1M 以下的 low mem
        if (start < 0x100000) {
            start = 0x100000;
        }
        if (start >= end) {
            continue;
        }

        // 内核必然完整包含于一段 pmrange
        if ((start <= KERNEL_LOAD_ADDR) && (kend <= end)) {
            pages_add(start, KERNEL_LOAD_ADDR);
            pages_add(kend, end);
        } else {
            pages_add(start, end);
        }
    }

    logk("kernel vmspace layout:\n");
    vmspace_show(&g_kernel_vm);
}
