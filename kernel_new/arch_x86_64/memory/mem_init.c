#include "mem_init.h"
#include "percpu.h"
#include <arch_intf.h>
#include <arch_impl.h>
#include <memory/pmlayout.h>
#include <memory/page.h>
#include <memory/early_alloc.h>
#include <memory/vmspace.h>
#include <library/string.h>
#include <library/debug.h>


// 管理内存布局


// layout.ld
extern char _init_end;
extern char _text_addr;
extern char _text_end;
extern char _rodata_addr;
extern char _data_addr;

// 记录内核的虚拟地址空间布局
vmspace_t g_kernel_space;

// 内核段
static vmrange_t g_kernel_init;
static vmrange_t g_kernel_text;
static vmrange_t g_kernel_rodata;
static vmrange_t g_kernel_data;
// static vmrange_t g_kernel_percpu; // 对应 N 个 percpu、和中间 N-1 个间隔


INIT_TEXT void add_kernel_range(vmrange_t *rng, size_t addr, size_t end, const char *desc) {
    ASSERT(addr >= KERNEL_TEXT_ADDR);
    ASSERT(addr < end);

    // 记录虚拟地址
    rng->addr = addr;
    rng->end  = end;
    rng->desc = desc;
    vmspace_insert(&g_kernel_space, rng);

    // 标记页描述符数组
    addr -= KERNEL_TEXT_ADDR;
    end  -= KERNEL_TEXT_ADDR;
    page_set_type(addr, end, PT_KERNEL);
}


// 物理内存最高地址
static INIT_TEXT size_t pm_end() {
    for (int i = pmrange_count() - 1; i >= 0; --i) {
        pmrange_t *pmr = pmrange_at_index(i);
        if ((PM_AVAILABLE != pmr->type) && (PM_RECLAIMABLE != pmr->type)) {
            continue;
        }
        return pmr->end;
    }
    return 0;
}


// 划分内存布局，启用动态内存分配
INIT_TEXT void mem_init() {
    page_desc_init(pm_end()); // 分配页描述符
    early_alloc_disable();    // 禁用临时内存分配

    // 获取内核各 section 结束位置
    size_t init_addr = KERNEL_TEXT_ADDR + KERNEL_LOAD_ADDR;
    size_t ro_end = (size_t)early_alloc_ro(0);
    size_t rw_end = (size_t)early_alloc_rw(0);

    // 把内核地址空间布局记录下来
    vmspace_init(&g_kernel_space);
    add_kernel_range(&g_kernel_init, init_addr, (size_t)&_init_end, "init");
    add_kernel_range(&g_kernel_text, (size_t)&_text_addr, (size_t)&_text_end, "text");
    add_kernel_range(&g_kernel_rodata, (size_t)&_rodata_addr, ro_end, "rodata"); // 含 early_ro
    add_kernel_range(&g_kernel_data, (size_t)&_data_addr, rw_end, "data"); // 含 bss、early_rw

    // 可用部分按页对齐
    // TODO 可以在这里创建 kernel heap

    // 给 percpu 分配空间，留出一个 guard page
    rw_end += 2 * PAGE_SIZE - 1;
    rw_end &= ~(PAGE_SIZE - 1);
    rw_end = percpu_init(rw_end);

    // 遍历 mem_block，将可用物理内存范围逐一添加（包括可回收部分）
    for (int i = 0, N = pmrange_count(); i < N; ++i) {
        pmrange_t *pmr = pmrange_at_index(i);
        if ((PM_AVAILABLE != pmr->type) && (PM_RECLAIMABLE != pmr->type)) {
            continue;
        }

        // 跳过 1M 以下的 low mem
        size_t start = pmr->start;
        size_t end = pmr->end;
        if (start < 0x100000) {
            start = 0x100000;
        }
        if (start >= end) {
            continue;
        }

        // 内核必然完整包含于一段 pmrange
        if (start <= KERNEL_LOAD_ADDR && (rw_end <= end)) {
            pages_free(start, KERNEL_LOAD_ADDR);
            pages_free(rw_end, end);
        } else {
            pages_free(start, end);
        }
    }

    // 内核占据的内存分为许多 section，之间还有 guard pages，可以回收
    for (dlnode_t *i = g_kernel_space.head.next; i != &g_kernel_space.head; i = i->next) {
        vmrange_t *prev = containerof(i->prev, vmrange_t, dl);
        vmrange_t *curr = containerof(i, vmrange_t, dl);
        pages_free(prev->end - KERNEL_TEXT_ADDR, curr->addr - KERNEL_TEXT_ADDR);
    }

    // TODO 还要把全部物理内存映射到 canonical hole 之后
    // MMIO 范围可能在可用内存范围之外，也要映射
}
