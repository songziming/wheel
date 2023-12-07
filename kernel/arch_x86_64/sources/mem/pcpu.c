#include <arch_mem.h>
#include <wheel.h>
#include <vmspace.h>
#include <str.h>


// 除了提供 per-cpu 变量支持，还包括异常栈、中断栈
// 因为这些栈也是每个处理器私有的


extern char _pcpu_addr, _pcpu_data_end, _pcpu_bss_end;


static CONST size_t g_pcpu_addr;  // 第一个pcpu区域的起始地址
static CONST size_t g_pcpu_size;    // pcpu 占据的大小，不含 guard page

static PCPU_BSS vmrange_t g_pcpu_vars;
static PCPU_BSS vmrange_t g_pcpu_nmi;

// percpu 初始化需要分成两步
//  1. 使用 early-alloc 准备相关数组
//    （分配物理页描述符数组）
//  2. 给 PCPU 分配真正的空间

// 使用 early-alloc 分配的数据应该连续，但是 PCPU 要对齐


// 传入内核结束位置（bss 结尾，不含 guard page）
// 划分 pcpu 占据的范围，返回 pcpu 结束位置
INIT_TEXT size_t pcpu_init(size_t kernel_end) {
    size_t copy_size = (size_t)(&_pcpu_data_end - &_pcpu_addr);
    size_t vars_size = (size_t)(&_pcpu_bss_end  - &_pcpu_addr); // 变量区域大小

    kernel_end = (kernel_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    g_pcpu_size = (vars_size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    int ncpu = cpu_count();
    ASSERT(ncpu > 0);

    for (int i = 0; i < ncpu; ++i) {
        uint8_t *pcpu = (uint8_t *)(kernel_end + PAGE_SIZE);
        bcpy(pcpu, &_pcpu_addr, copy_size);

        // 马上就能使用这个pcpu区域中的数据，正好用来记录内存布局
        vmrange_t *rng_vars = pcpu_ptr(i, &g_pcpu_vars);
    }

    return g_pcpu_addr + (g_pcpu_size + PAGE_SIZE) * ncpu;
    // kernel_end += PAGE_SIZE * 2 - 1;
    // kernel_end &= ~(PAGE_SIZE - 1);



    return kernel_end;
}

