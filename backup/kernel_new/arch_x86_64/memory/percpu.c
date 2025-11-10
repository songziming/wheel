#include "percpu.h"
#include <arch_intf.h>
#include <arch_impl.h>
#include <cpu/rw.h>
#include <cpu/features.h>
#include <memory/vmspace.h>
#include <memory/page.h>
#include <library/debug.h>
#include <library/string.h>


// per-CPU area 不仅是变量，还包括中断栈、异常栈
// 在内核地址空间看来，percpu 是一整段连续范围，但 perpcu 内部还有精细结构
// 本模块就负责维护 percpu 内部精细结构

// percpu 包括 N 个处理器的数据、和 N-1 个空洞
// +----------+------+----------+------+----------+-----
// | percpu 0 | hole | percpu 1 | hole | percpu 2 | ...
// +----------+------+----------+------+----------+-----
// 添加 hole 是为了页面着色，让每个 percpu 映射到相同的 L1 cache line

// 每个处理器对应的 PCPU 区域内部结构如下：
// +-------+-----------+-------+--------+-------+--------+-----
// | guard | PCPU VARS | guard | STACK1 | guard | STACK2 | ...
// +-------+-----------+-------+--------+-------+--------+-----
// 其中 PCPU vars 是静态声明的变量，只有这部分变量可以用 percpu_ptr、thiscpu_ptr 访问


// layout.ld
extern char _percpu_addr;
extern char _percpu_data_end;
extern char _percpu_bss_end;

static CONST size_t g_percpu_base = 0; // 首个 pcpu 区域的偏移量，跳过 guard
static CONST size_t g_percpu_skip = 0; // 相邻两个 pcpu 的间距
// PERCPU_BSS int g_this_index; // 需要被内联汇编使用，不能使用 static

// percpu sections
static PERCPU_BSS vmrange_t g_percpu_vars; // data + bss
static PERCPU_BSS vmrange_t g_percpu_nmi;  // NMI IST
static PERCPU_BSS vmrange_t g_percpu_df;   // #DF IST
static PERCPU_BSS vmrange_t g_percpu_pf;   // #PF IST
static PERCPU_BSS vmrange_t g_percpu_mc;   // #MC IST
static PERCPU_BSS vmrange_t g_percpu_int;  // int stack

// mem_init.c
INIT_TEXT void add_kernel_range(vmrange_t *rng, size_t addr, size_t end, mmu_attr_t attrs, const char *desc);


// 返回对齐后的结束地址，包括 guard page
static INIT_TEXT size_t add_percpu_range(int cpu, vmrange_t *rng, size_t addr, size_t size, const char *desc) {
    size_t end = addr + size;
    add_kernel_range(percpu_ptr(cpu, rng), addr, end, MMU_WRITE, desc);

    end += PAGE_SIZE;
    end += PAGE_SIZE - 1;
    end &= ~(PAGE_SIZE - 1);
    return end;
}

INIT_TEXT size_t percpu_nmi_stack_top(int cpu) {
    vmrange_t *rng = percpu_ptr(cpu, &g_percpu_nmi);
    return rng->end;
}

INIT_TEXT size_t percpu_df_stack_top(int cpu) {
    vmrange_t *rng = percpu_ptr(cpu, &g_percpu_df);
    return rng->end;
}

INIT_TEXT size_t percpu_pf_stack_top(int cpu) {
    vmrange_t *rng = percpu_ptr(cpu, &g_percpu_pf);
    return rng->end;
}

INIT_TEXT size_t percpu_mc_stack_top(int cpu) {
    vmrange_t *rng = percpu_ptr(cpu, &g_percpu_mc);
    return rng->end;
}

INIT_TEXT size_t percpu_int_stack_top(int cpu) {
    vmrange_t *rng = percpu_ptr(cpu, &g_percpu_int);
    return rng->end;
}

// 划分 per-cpu 存储空间，从指定地址开始，开头留出一个 guard page
// 传入的是虚拟地址，返回分配了所有的 percpu 之后的结束地址
// 开头没有 guard page，这样输入和输出正好是 percpu 范围的起止
INIT_TEXT size_t percpu_init(size_t va) {
    ASSERT(0 == g_percpu_skip);
    ASSERT(0 == g_percpu_base);
    ASSERT(va > KERNEL_TEXT_ADDR);

    size_t copy_size = (size_t)(&_percpu_data_end - &_percpu_addr);
    size_t zero_size = (size_t)(&_percpu_bss_end  - &_percpu_data_end);
    size_t vars_size = copy_size + zero_size;

    size_t g_percpu_size  = vars_size + PAGE_SIZE - 1;
    g_percpu_size &= ~(PAGE_SIZE - 1);           // percpu 变量空间，页对齐
    g_percpu_size += PAGE_SIZE + INT_STACK_SIZE; // NMI 异常栈
    g_percpu_size += PAGE_SIZE + INT_STACK_SIZE; // #PF 异常栈
    g_percpu_size += PAGE_SIZE + INT_STACK_SIZE; // #DF 异常栈
    g_percpu_size += PAGE_SIZE + INT_STACK_SIZE; // #MC 异常栈
    g_percpu_size += PAGE_SIZE + INT_STACK_SIZE; // 中断栈

    g_percpu_skip  = g_percpu_size + PAGE_SIZE; // 相邻两个 percpu 之间要留有 guard page

    // 按 L1 大小对齐
    size_t l1size = g_l1d_info.line_size * g_l1d_info.sets;
    if (0 != l1size) {
        ASSERT(0 == (l1size & (l1size - 1)));
        g_percpu_skip += l1size - 1;
        g_percpu_skip &= ~(l1size - 1);
    }

    int ncpu = cpu_count();
    ASSERT(ncpu > 0);

    // per-cpu 起始地址按页对齐，并算出结束位置
    va += PAGE_SIZE - 1;
    va &= ~(PAGE_SIZE - 1);
    g_percpu_base = va - (size_t)&_percpu_addr;
    size_t end = va + g_percpu_skip * (ncpu - 1) + g_percpu_size;

    for (int i = 0; i < ncpu; ++i) {
        size_t next = va + g_percpu_skip;

        memcpy((void*)va, &_percpu_addr, copy_size); // 复制 per-cpu data
        memset((void*)va + copy_size, 0, zero_size); // per-cpu bss 清零

        va = add_percpu_range(i, &g_percpu_vars, va, vars_size,      "percpu vars");
        va = add_percpu_range(i, &g_percpu_nmi,  va, INT_STACK_SIZE, "NMI IST");
        va = add_percpu_range(i, &g_percpu_df,   va, INT_STACK_SIZE, "#DF IST");
        va = add_percpu_range(i, &g_percpu_pf,   va, INT_STACK_SIZE, "#PF IST");
        va = add_percpu_range(i, &g_percpu_mc,   va, INT_STACK_SIZE, "#MC IST");
        va = add_percpu_range(i, &g_percpu_int,  va, INT_STACK_SIZE, "INT stack");

        ASSERT(va <= next);
        va = next;
    }

    return end;
}

// 设置 this-cpu 指针，并且设置 CPU 编号
INIT_TEXT void thiscpu_init(int idx) {
    ASSERT(0 != g_percpu_base);
    ASSERT(0 != g_percpu_skip);
    ASSERT(idx >= 0);
    ASSERT(idx < cpu_count());

    write_gsbase(g_percpu_base + g_percpu_skip * idx);

    // g_this_index = idx;
    // __asm__("movl %0, %%gs:(g_this_index)" :: "r"(idx));
}


//------------------------------------------------------------------------------
// 访问 percpu 变量
//------------------------------------------------------------------------------

static inline int is_percpu_var(void *ptr) {
    return ((char*)ptr >= &_percpu_addr)
        && ((char*)ptr < &_percpu_bss_end);
}

inline void *percpu_ptr(int idx, void *ptr) {
    ASSERT(0 != g_percpu_base);
    ASSERT(0 != g_percpu_skip);
    ASSERT(is_percpu_var(ptr));
    ASSERT(idx < cpu_count());

    return (uint8_t*)ptr + g_percpu_base + g_percpu_skip * idx;
}

// 依赖 gsbase
inline void *thiscpu_ptr(void *ptr) {
    ASSERT(0 != g_percpu_base);
    ASSERT(0 != g_percpu_skip);
    ASSERT(is_percpu_var(ptr));

    return (uint8_t*)ptr + read_gsbase();
}

// 依赖 gsbase
inline int cpu_index() {
    ASSERT(0 != g_percpu_base);
    ASSERT(0 != g_percpu_skip);

    size_t base = (size_t)read_gsbase() - g_percpu_base;
    ASSERT(0 == (base % g_percpu_skip));
    return base / g_percpu_skip;

    // int idx;
    // __asm__("movl %%gs:(g_this_index), %0" : "=r"(idx));
    // return idx;
}
