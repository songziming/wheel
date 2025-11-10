#include <arch_mem.h>
#include <cpu/rw.h>
#include <cpu/info.h>

#include <wheel.h>


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
static CONST size_t g_pcpu_skip = 0; // 相邻两个 pcpu 的间距

PCPU_BSS vmrange_t g_range_pcpu_vars;
PCPU_BSS vmrange_t g_range_pcpu_nmi;
PCPU_BSS vmrange_t g_range_pcpu_df;
PCPU_BSS vmrange_t g_range_pcpu_pf;
PCPU_BSS vmrange_t g_range_pcpu_mc;
PCPU_BSS vmrange_t g_range_pcpu_int;


inline int is_pcpu_var(void *ptr) {
    return ((char *)ptr >= &_pcpu_addr)
        || ((char *)ptr < &_pcpu_bss_end);
}

inline void *pcpu_ptr(int idx, void *ptr) {
    ASSERT(0 != g_pcpu_offset);
    ASSERT(0 != g_pcpu_skip);
    ASSERT(is_pcpu_var(ptr));
    ASSERT(idx < cpu_count());

    return (uint8_t *)ptr + g_pcpu_offset + g_pcpu_skip * idx;
}

// 依赖 gsbase
inline void *this_ptr(void *ptr) {
    ASSERT(0 != g_pcpu_offset);
    ASSERT(0 != g_pcpu_skip);
    ASSERT(is_pcpu_var(ptr));

    return (uint8_t *)ptr + read_gsbase();
}

// 依赖 gsbase
inline int cpu_index() {
    ASSERT(0 != g_pcpu_offset);
    ASSERT(0 != g_pcpu_skip);

    size_t base = (size_t)read_gsbase() - g_pcpu_offset;
    ASSERT(0 == (base % g_pcpu_skip));

    return base / g_pcpu_skip;
}


#if 0
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
#endif

// 记录一段 PCPU 区域，标记物理页，返回对齐的结束地址
// 类似 arch_mem.c 里面的 add_kernel_range
static INIT_TEXT size_t add_pcpu_range(vmrange_t *rng, size_t addr, size_t size, const char *desc) {
    ASSERT(NULL != rng);

    // klog("mapping %lx~%lx as %s\n", addr, addr + size, desc);

    kernel_context_mark(rng, addr, addr + size, addr - KERNEL_TEXT_ADDR, MMU_WRITE, desc);

    size_t from = addr & ~(PAGE_SIZE - 1);
    size_t to = (rng->end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    page_add(from - KERNEL_TEXT_ADDR, to - KERNEL_TEXT_ADDR, PT_KERNEL);

    return to;
}


// 传入内核结束位置（bss 结尾，不含 guard page）
// 划分 pcpu 占据的范围，返回 pcpu 结束位置
INIT_TEXT void pcpu_allocate(size_t kend) {
    ASSERT(0 == g_pcpu_offset);
    ASSERT(0 == g_pcpu_skip);

    size_t copy_size = (size_t)(&_pcpu_data_end - &_pcpu_addr);
    size_t vars_size = (size_t)(&_pcpu_bss_end  - &_pcpu_addr);
    size_t vars_size_aligned = (vars_size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    // 获取 L1 大小（一路）
    size_t l1size = g_l1d_info.line_size * g_l1d_info.sets;
    if (0 == l1size) {
        l1size = PAGE_SIZE;
    }
    ASSERT(0 == (l1size & (l1size - 1)));

    // 计算相邻 PCPU 距离，包含了 guard page
    g_pcpu_skip  = vars_size_aligned + PAGE_SIZE; // 变量区域
    g_pcpu_skip += INT_STACK_SIZE    + PAGE_SIZE; // NMI 异常栈
    g_pcpu_skip += INT_STACK_SIZE    + PAGE_SIZE; // #PF 异常栈
    g_pcpu_skip += INT_STACK_SIZE    + PAGE_SIZE; // #DF 异常栈
    g_pcpu_skip += INT_STACK_SIZE    + PAGE_SIZE; // #MC 异常栈
    g_pcpu_skip += INT_STACK_SIZE    + PAGE_SIZE; // 中断栈
    g_pcpu_skip  = (g_pcpu_skip + l1size - 1) & ~(l1size - 1); // 按 L1 对齐

    int ncpu = cpu_count();
    ASSERT(ncpu > 0);

    // 起始地址按页对齐，并留出一个 guard page
    kend += 2 * PAGE_SIZE - 1;
    kend &= ~(PAGE_SIZE - 1);
    g_pcpu_offset = kend - (size_t)&_pcpu_addr;

    // 划分每个 PCPU 的布局，并记录在 vmrange 中
    for (int i = 0; i < ncpu; ++i) {
        size_t ptr = kend;
        kend += g_pcpu_skip;
        // klog("range for cpu %d is %lx~%lx\n", i, ptr, kend);

        memcpy((char *)ptr, &_pcpu_addr, copy_size); // 复制 PCPU_DATA
        memset((char *)ptr + copy_size, 0, vars_size - copy_size); // PCPU_BSS 清零

        vmrange_t *rng_vars = pcpu_ptr(i, &g_range_pcpu_vars);
        vmrange_t *rng_nmi  = pcpu_ptr(i, &g_range_pcpu_nmi);
        vmrange_t *rng_df   = pcpu_ptr(i, &g_range_pcpu_df);
        vmrange_t *rng_pf   = pcpu_ptr(i, &g_range_pcpu_pf);
        vmrange_t *rng_mc   = pcpu_ptr(i, &g_range_pcpu_mc);
        vmrange_t *rng_int  = pcpu_ptr(i, &g_range_pcpu_int);

        ptr = add_pcpu_range(rng_vars, ptr, vars_size_aligned, strmake("pcpu %d vars", i));
        ptr = add_pcpu_range(rng_nmi, ptr + PAGE_SIZE, INT_STACK_SIZE, strmake("pcpu %d NMI stack", i));
        ptr = add_pcpu_range(rng_df,  ptr + PAGE_SIZE, INT_STACK_SIZE, strmake("pcpu %d #DF stack", i));
        ptr = add_pcpu_range(rng_pf,  ptr + PAGE_SIZE, INT_STACK_SIZE, strmake("pcpu %d #PF stack", i));
        ptr = add_pcpu_range(rng_mc,  ptr + PAGE_SIZE, INT_STACK_SIZE, strmake("pcpu %d #MC stack", i));
        ptr = add_pcpu_range(rng_int, ptr + PAGE_SIZE, INT_STACK_SIZE, strmake("pcpu %d int stack", i));

        ASSERT(ptr + PAGE_SIZE <= kend);
    }
}

// 设置 this-cpu 指针，并且设置 CPU 编号
INIT_TEXT void gsbase_init(int idx) {
    ASSERT(0 != g_pcpu_offset);
    ASSERT(0 != g_pcpu_skip);
    ASSERT(idx < cpu_count());

    write_gsbase(g_pcpu_offset + g_pcpu_skip * idx);
}
