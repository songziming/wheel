#include <arch_mem.h>
#include <cpu/rw.h>
#include <arch_cpu.h>

#include <wheel.h>
#include <str.h>

#include <vmspace.h>
#include <page.h>


// 多核环境下，每个处理器的私有数据区
// 除了提供 per-cpu 变量支持，还包括异常栈、中断栈
// 因为这些栈也是每个处理器私有的


// 每个处理器对应的 PCPU 区域内部结构如下：
// +-------+-----------+-------+--------+-------+--------+-----
// | guard | PCPU vars | guard | stack1 | guard | stack2 | ...
// +-------+-----------+-------+--------+-------+--------+-----
// 其中 PCPU vars 是静态声明的变量，只有这部分变量可以用 pcpu_ptr、this_ptr 访问




// layout.ld
extern char _pcpu_addr;
extern char _pcpu_data_end;
extern char _pcpu_bss_end;

static CONST size_t g_pcpu_offset = 0; // 首个 pcpu 区域的偏移量，跳过 guard
static CONST size_t g_pcpu_size = 0; // pcpu 占据的大小

CONST vmrange_t *g_range_pcpu_vars = NULL;
CONST vmrange_t *g_range_pcpu_nmi  = NULL;
CONST vmrange_t *g_range_pcpu_df   = NULL;
CONST vmrange_t *g_range_pcpu_pf   = NULL;
CONST vmrange_t *g_range_pcpu_mc   = NULL;
CONST vmrange_t *g_range_pcpu_int  = NULL;


inline void *pcpu_ptr(int idx, void *ptr) {
    ASSERT(0 != g_pcpu_offset);
    ASSERT(0 != g_pcpu_size);
    ASSERT((char *)ptr >= &_pcpu_addr);
    ASSERT((char *)ptr < &_pcpu_bss_end);
    ASSERT(idx < cpu_count());

    return (uint8_t *)ptr + g_pcpu_offset + g_pcpu_size * idx;
}

// 依赖 gsbase
inline void *this_ptr(void *ptr) {
    ASSERT(0 != g_pcpu_offset);
    ASSERT(0 != g_pcpu_size);
    ASSERT((char *)ptr >= &_pcpu_addr);
    ASSERT((char *)ptr < &_pcpu_bss_end);

    return (uint8_t *)ptr + read_gsbase();
}

// 依赖 gsbase
inline int cpu_index() {
    ASSERT(0 != g_pcpu_offset);
    ASSERT(0 != g_pcpu_size);

    size_t base = (size_t)read_gsbase() - g_pcpu_offset;
    ASSERT(0 == (base % g_pcpu_size));

    return base / g_pcpu_size;
}


INIT_TEXT void pcpu_prepare() {
    ASSERT(NULL == g_range_pcpu_vars);
    ASSERT(NULL == g_range_pcpu_nmi);
    ASSERT(NULL == g_range_pcpu_df);
    ASSERT(NULL == g_range_pcpu_pf);
    ASSERT(NULL == g_range_pcpu_mc);
    ASSERT(NULL == g_range_pcpu_int);

    int ncpu = cpu_count();
    ASSERT(ncpu > 0);

    g_range_pcpu_vars = early_alloc_ro(ncpu * sizeof(vmrange_t));
    g_range_pcpu_nmi  = early_alloc_ro(ncpu * sizeof(vmrange_t));
    g_range_pcpu_df   = early_alloc_ro(ncpu * sizeof(vmrange_t));
    g_range_pcpu_pf   = early_alloc_ro(ncpu * sizeof(vmrange_t));
    g_range_pcpu_mc   = early_alloc_ro(ncpu * sizeof(vmrange_t));
    g_range_pcpu_int  = early_alloc_ro(ncpu * sizeof(vmrange_t));
}

// 记录一段 PCPU 区域，标记物理页，返回对齐的结束地址
// 类似 arch_mem.c 里面的 add_kernel_range
static INIT_TEXT size_t add_pcpu_range(vmspace_t *vm, vmrange_t *rng, size_t addr, size_t size, const char *desc) {
    vmrange_init(rng, addr, addr + size, desc);
    vmspace_insert(vm, rng);

    size_t from = addr & ~(PAGE_SIZE - 1);
    size_t to = (rng->end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    page_add(from - KERNEL_TEXT_ADDR, to - KERNEL_TEXT_ADDR, PT_KERNEL);

    return to;
}


// 传入内核结束位置（bss 结尾，不含 guard page）
// 划分 pcpu 占据的范围，返回 pcpu 结束位置
INIT_TEXT size_t pcpu_allocate(size_t kernel_end, vmspace_t *vm) {
    ASSERT(0 == g_pcpu_offset);
    ASSERT(0 == g_pcpu_size);

    ASSERT(NULL != g_range_pcpu_vars);
    ASSERT(NULL != g_range_pcpu_nmi);
    ASSERT(NULL != g_range_pcpu_df);
    ASSERT(NULL != g_range_pcpu_pf);
    ASSERT(NULL != g_range_pcpu_mc);
    ASSERT(NULL != g_range_pcpu_int);

    size_t copy_size = (size_t)(&_pcpu_data_end - &_pcpu_addr);
    size_t vars_size = (size_t)(&_pcpu_bss_end  - &_pcpu_addr);
    size_t vars_size_aligned = (vars_size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    // 获取 L1 大小（一路）
    size_t l1size = g_l1d_info.line_size * g_l1d_info.sets;
    if (0 == l1size) {
        l1size = PAGE_SIZE;
    }
    ASSERT(0 == (l1size & (l1size - 1)));

    // 计算 PCPU 占据的大小，包含了 guard page
    g_pcpu_size  = PAGE_SIZE + vars_size_aligned; // 变量区域
    g_pcpu_size += PAGE_SIZE + INT_STACK_SIZE; // NMI 异常栈
    g_pcpu_size += PAGE_SIZE + INT_STACK_SIZE; // #PF 异常栈
    g_pcpu_size += PAGE_SIZE + INT_STACK_SIZE; // #DF 异常栈
    g_pcpu_size += PAGE_SIZE + INT_STACK_SIZE; // #MC 异常栈
    g_pcpu_size += PAGE_SIZE + INT_STACK_SIZE; // 中断栈
    g_pcpu_size  = (g_pcpu_size + l1size - 1) & ~(l1size - 1); // 按 L1 对齐

    int ncpu = cpu_count();
    ASSERT(ncpu > 0);

    // 划分每个 PCPU 的布局，并记录在 vmrange 中
    kernel_end = (kernel_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    for (int i = 0; i < ncpu; ++i) {
        kernel_end = add_pcpu_range(vm, &g_range_pcpu_vars[i], kernel_end + PAGE_SIZE, vars_size, "pcpu vars");
        bcpy((void *)g_range_pcpu_vars[i].addr, &_pcpu_addr, copy_size);

        kernel_end = add_pcpu_range(vm, &g_range_pcpu_nmi[i], kernel_end + PAGE_SIZE, INT_STACK_SIZE, "pcpu NMI stack");
        kernel_end = add_pcpu_range(vm, &g_range_pcpu_df[i],  kernel_end + PAGE_SIZE, INT_STACK_SIZE, "pcpu #DF stack");
        kernel_end = add_pcpu_range(vm, &g_range_pcpu_pf[i],  kernel_end + PAGE_SIZE, INT_STACK_SIZE, "pcpu #PF stack");
        kernel_end = add_pcpu_range(vm, &g_range_pcpu_mc[i],  kernel_end + PAGE_SIZE, INT_STACK_SIZE, "pcpu #MC stack");
        kernel_end = add_pcpu_range(vm, &g_range_pcpu_int[i], kernel_end + PAGE_SIZE, INT_STACK_SIZE, "pcpu int stack");

        if (0 == i) {
            g_pcpu_size = kernel_end - g_range_pcpu_vars[i].addr + PAGE_SIZE;
            g_pcpu_offset = g_range_pcpu_vars[i].addr - (size_t)&_pcpu_addr;
        } else {
            ASSERT(g_pcpu_size == kernel_end - g_range_pcpu_vars[i].addr + PAGE_SIZE);
        }
    }

    return kernel_end;
}

// 设置 this-cpu 指针，并且设置 CPU 编号
INIT_TEXT void gsbase_init(int idx) {
    ASSERT(0 != g_pcpu_offset);
    ASSERT(0 != g_pcpu_size);
    ASSERT(idx < cpu_count());

    write_gsbase(g_pcpu_offset + g_pcpu_size * idx);
}
