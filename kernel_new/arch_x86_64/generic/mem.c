#include "mem.h"
#include <arch_impl.h>
#include <memory/mem_block.h>
#include <memory/page.h>
#include <memory/early_alloc.h>
#include <memory/vm_space.h>


// 管理内存布局



// layout.ld
extern char _init_end;
extern char _text_addr, _text_end;
extern char _rodata_addr;
extern char _data_addr;

// 记录内核的虚拟地址空间布局
static vmspace_t g_kernel_space;

// 记录内核地址空间里的不同区域
static vmrange_t g_kernel_init;
static vmrange_t g_kernel_text;
static vmrange_t g_kernel_rodata;
static vmrange_t g_kernel_data;


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
    // TODO 代码段、数据段的布局是固定的
    vm_init(&g_kernel_space);
    mark_kernel_range(&g_kernel_init, init_addr, &_init_end, "init");
    mark_kernel_range(&g_kernel_text, &_text_addr, &_text_end, "text");
    mark_kernel_range(&g_kernel_rodata, &_rodata_addr, ro_end, "rodata"); // 含 early_ro
    mark_kernel_range(&g_kernel_data, &_data_addr, (void *)rw_end, "data"); // 含 BSS、early_rw

    // 可用部分按页对齐，留出一个 guard page
    rw_end += 2 * PAGE_SIZE - 1;
    rw_end &= ~(PAGE_SIZE - 1);
    // TODO 可以在这里创建 kernel heap

    // 划分 percpu area


    vm_show(&g_kernel_space); // 打印内核地址空间
}
