#include "mem.h"
#include "percpu.h"
#include <arch_intf.h>
#include <arch_impl.h>
#include <memory/mem_block.h>
#include <memory/page.h>
#include <memory/early_alloc.h>
#include <memory/vm_space.h>
#include <library/string.h>
#include <library/debug.h>


// 管理内存布局


// layout.ld
extern char _pcpu_addr;
extern char _pcpu_data_end;
extern char _pcpu_bss_end;
extern char _init_end;
extern char _text_addr;
extern char _text_end;
extern char _rodata_addr;
extern char _data_addr;

// 记录内核的虚拟地址空间布局
static vmspace_t g_kernel_space;
static vmrange_t g_kernel_init;
static vmrange_t g_kernel_text;
static vmrange_t g_kernel_rodata;
static vmrange_t g_kernel_data;
static vmrange_t g_kernel_percpu;   // 对应 N 个 percpu、和中间 N-1 个间隔

// 记录 percpu 部分的次级结构，重复 N 份，包含在 kernel-space 里的一个 range
static vmspace_t g_percpu_space;
static PCPU_BSS vmrange_t g_percpu_vars; // data + bss
static PCPU_BSS vmrange_t g_percpu_nmi;  // NMI IST
static PCPU_BSS vmrange_t g_percpu_df;   // #DF IST
static PCPU_BSS vmrange_t g_percpu_pf;   // #PF IST
static PCPU_BSS vmrange_t g_percpu_mc;   // #MC IST
static PCPU_BSS vmrange_t g_percpu_int;  // int stack


static INIT_TEXT void mark_kernel_range(vmrange_t *rng, void *addr, void *end, const char *desc) {
    rng->addr = (size_t)addr;
    rng->end  = (size_t)end;
    rng->desc = desc;
    vm_insert(&g_kernel_space, rng);

    // TODO 还要标记页描述符数组
    pfn_t page_start = ((size_t)addr - KERNEL_TEXT_ADDR) >> PAGE_SHIFT;
    pfn_t page_end   = ((size_t)end - KERNEL_TEXT_ADDR) >> PAGE_SHIFT;
    page_set_type(page_start, page_end, PT_KERNEL);
}

static INIT_TEXT void mark_percpu_range(vmrange_t *rng, size_t size, size_t align, const char *desc) {
    rng->addr = percpu_reserve(size, align);
    rng->end  = rng->addr + size;
    rng->desc = desc;
    vm_insert(&g_percpu_space, rng);
}

// static INIT_TEXT void reserve_percpu_data() {
//     percpu_reserve(0, 0);
// }

// static INIT_TEXT void reserve_percpu_range(vmrange_t *rng, size_t size, size_t align, const char *desc) {
//     rng->addr = percpu_reserve(size, align);
//     rng->end  = rng->addr + size;
//     rng->desc = desc;
//     vm_insert(&g_percpu_space, rng);
// }

// 划分内存布局，启用动态内存分配
INIT_TEXT void mem_init() {
    // 初始化页描述符数组
    // 只有 mem_block 最清楚物理内存分布，应该让 mem_block 申请并填充 page desc
    size_t top = mem_block_top();
    top += PAGE_SIZE - 1;
    top &= ~(PAGE_SIZE - 1);

    // 分配页描述符
    page_init(top);

    // 禁用临时内存分配
    early_alloc_disable();

    // 获取内核各 section 结束位置
    char *init_addr = (char *)KERNEL_TEXT_ADDR + KERNEL_LOAD_ADDR;
    char *ro_end = early_alloc_ro(0);
    size_t rw_end = (size_t)early_alloc_rw(0);

    // 把内核地址空间布局记录下来
    vm_init(&g_kernel_space);
    mark_kernel_range(&g_kernel_init, init_addr, &_init_end, "init");
    mark_kernel_range(&g_kernel_text, &_text_addr, &_text_end, "text");
    mark_kernel_range(&g_kernel_rodata, &_rodata_addr, ro_end, "rodata"); // 含 early_ro
    mark_kernel_range(&g_kernel_data, &_data_addr, (void *)rw_end, "data"); // 含 BSS、early_rw

    // 可用部分按页对齐
    // TODO 可以在这里创建 kernel heap

    // 划分 percpu area
    intptr_t copy_size = &_pcpu_data_end - &_pcpu_addr;
    intptr_t zero_size = &_pcpu_bss_end  - &_pcpu_data_end;
    // size_t vars_size = (size_t)(&_pcpu_bss_end  - &_pcpu_data_end)

    // 计算 percpu 各段起始偏移
    // size_t offset_vars = percpu_reserve(copy_size + zero_size, 0);
    // size_t offset_nmi  = percpu_reserve(INT_STACK_SIZE, PAGE_SIZE);
    // size_t offset_df   = percpu_reserve(INT_STACK_SIZE, PAGE_SIZE);
    // size_t offset_pf   = percpu_reserve(INT_STACK_SIZE, PAGE_SIZE);
    // size_t offset_mc   = percpu_reserve(INT_STACK_SIZE, PAGE_SIZE);
    // size_t offset_int  = percpu_reserve(INT_STACK_SIZE, PAGE_SIZE);
    vm_init(&g_percpu_space);
    mark_percpu_range(&g_percpu_vars, copy_size + zero_size, 0, "vars");
    mark_percpu_range(&g_percpu_nmi, INT_STACK_SIZE, PAGE_SIZE, "nmi stack");
    mark_percpu_range(&g_percpu_df,  INT_STACK_SIZE, PAGE_SIZE, "#df stack");
    mark_percpu_range(&g_percpu_pf,  INT_STACK_SIZE, PAGE_SIZE, "#pf stack");
    mark_percpu_range(&g_percpu_mc,  INT_STACK_SIZE, PAGE_SIZE, "#mc stack");
    mark_percpu_range(&g_percpu_int, INT_STACK_SIZE, PAGE_SIZE, "int stack");

    size_t dist = percpu_align_to_l1(); // 相邻两个 percpu 的距离

    // 给 percpu 分配空间，留出一个 guard page
    rw_end += 2 * PAGE_SIZE - 1;
    rw_end &= ~(PAGE_SIZE - 1);
    // vmrange_t *last = containerof(g_percpu_space.head.prev, vmrange_t, dl);
    // size_t pcpu_size = dist * (cpu_count() - 1) + last->end;

    // percpu_allocate(rw_end);
    g_kernel_percpu.addr = rw_end;
    g_kernel_percpu.end = rw_end + percpu_allocate(rw_end);
    g_kernel_percpu.desc = "percpu";
    vm_insert(&g_kernel_space, &g_kernel_percpu);

    // 建立每个 percpu 的内容，可以使用
    for (int i = 0; i < cpu_count(); ++i) {
        memcpy((char *)rw_end, &_pcpu_addr, copy_size);
        memset((char *)rw_end + copy_size, 0, zero_size);
        // mark_kernel_range()
        rw_end += dist;
    }


    log("kernel ");
    vm_show(&g_kernel_space); // 打印内核地址空间
    log("percpu ");
    vm_show(&g_percpu_space);
}
