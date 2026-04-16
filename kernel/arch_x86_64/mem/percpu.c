#include "mem.h"
#include "apic/apic.h"
#include <arch_api.h>
#include <vmspace.h>
#include <kstring.h>
#include <debug.h>


// percpu-area 不仅包括变量，还有中断栈、异常栈

// percpu 包括 N 个处理器的数据区、和 N-1 个空洞
// +----------+-----+----------+-----+----------+-----
// | percpu 0 | gap | percpu 1 | gap | percpu 2 | ...
// +----------+-----+----------+-----+----------+-----
// 添加 gap 是为了页面着色，让每个 percpu 映射到相同的 L1 cache line
// 让各个 CPU 的缓存使用情况尽量一致

// 每个处理器对应的一段 percpu 还能进一步细分：
// +-----+-------------+-----+--------+-----+--------+-----
// | gap | percpu VARS | gap | STACK1 | gap | STACK2 | ...
// +-----+-------------+-----+--------+-----+--------+-----
// 其中 percpu vars 是全局变量，只有这部分变量可以用 percpu_ptr、thiscpu_ptr 访问


// layout.ld
extern char _percpu_addr;
extern char _percpu_data_end;
extern char _percpu_bss_end;


static CONST size_t g_percpu_base = 0; // 首个 percpu 区域的偏移量，跳过 gap
static CONST size_t g_percpu_step = 0; // 相邻两个 percpu 的起始地址的距离
PERCPU_BSS int g_thiscpu_idx;   // 记录当前 CPU 的编号


// percpu ranges
static PERCPU_BSS vmrange_t g_percpu_vars; // data + bss
static PERCPU_BSS vmrange_t g_percpu_nmi;  // NMI IST
static PERCPU_BSS vmrange_t g_percpu_df;   // #DF IST
static PERCPU_BSS vmrange_t g_percpu_pf;   // #PF IST
static PERCPU_BSS vmrange_t g_percpu_mc;   // #MC IST
static PERCPU_BSS vmrange_t g_percpu_int;  // int stack



static inline int is_percpu_var(void *ptr) {
    return ((char*)ptr >= &_percpu_addr)
        && ((char*)ptr < &_percpu_bss_end);
}

// 返回结束地址，包括 guard page，对齐到页
static INIT_TEXT size_t percpu_add(int cpu, vmrange_t *rng, size_t addr, size_t size, const char *desc) {
    size_t end = addr + size;
    kspace_add(PERCPU(cpu, rng), addr, end, desc, MMU_WRITE);
    end += PAGE_SIZE * 2 - 1;
    end &= ~(PAGE_SIZE - 1);
    return end;
}



// 输入参数是 percpu 开始地址，紧贴前一段区域，没有 gap
// 返回 percpu 结束地址，不含 gap
INIT_TEXT size_t percpu_init(size_t va) {
    ASSERT(0 == g_percpu_base);
    ASSERT(0 == g_percpu_step);
    ASSERT(va > KERNEL_TEXT_ADDR);

    size_t copy_size = (size_t)(&_percpu_data_end - &_percpu_addr);
    size_t zero_size = (size_t)(&_percpu_bss_end  - &_percpu_data_end);
    size_t vars_size = copy_size + zero_size;
    vars_size += PAGE_SIZE - 1;
    vars_size &= ~(PAGE_SIZE - 1);

    g_percpu_step  = PAGE_SIZE + vars_size;      // percpu 变量
    g_percpu_step += PAGE_SIZE + INT_STACK_SIZE; // 中断栈
    g_percpu_step += PAGE_SIZE + INT_STACK_SIZE; // NMI 异常栈
    g_percpu_step += PAGE_SIZE + INT_STACK_SIZE; // #PF 异常栈
    g_percpu_step += PAGE_SIZE + INT_STACK_SIZE; // #DF 异常栈
    g_percpu_step += PAGE_SIZE + INT_STACK_SIZE; // #MC 异常栈

    // TODO 获取 L1 data cache size，将 percpu 总大小对齐到 L1

    // 起始地址页对齐，插入一个 gap page
    va += PAGE_SIZE * 2 - 1;
    va &= ~(PAGE_SIZE - 1);
    g_percpu_base = va - (size_t)&_percpu_addr;

    // base/step 都已算好，后面可以使用 percpu_ptr 访问数据
    // 划分每个 CPU 的 percpu 区域，记录在 vmrange 里面
    int ncpu = cpu_count();
    for (int i = 0; i < ncpu; ++i) {
        kmemcpy((uint8_t*)va, &_percpu_addr, copy_size); // 复制 percpu data
        kmemset((uint8_t*)va + copy_size, 0, zero_size); // percpu bss 清零
        va = percpu_add(i, &g_percpu_vars, va, vars_size,      "smp data");
        va = percpu_add(i, &g_percpu_nmi,  va, INT_STACK_SIZE, "smp NMI");
        va = percpu_add(i, &g_percpu_pf,   va, INT_STACK_SIZE, "smp #PF");
        va = percpu_add(i, &g_percpu_df,   va, INT_STACK_SIZE, "smp #DF");
        va = percpu_add(i, &g_percpu_mc,   va, INT_STACK_SIZE, "smp #MC");
        va = percpu_add(i, &g_percpu_int,  va, INT_STACK_SIZE, "smp int stack");
    }

    return va;
}


// 设置 this-cpu 指针，并且设置 CPU 编号
INIT_TEXT void thiscpu_init(int idx) {
    ASSERT(0 != g_percpu_base);
    ASSERT(0 != g_percpu_step);
    ASSERT(idx >= 0);
    ASSERT(idx < cpu_count());

    write_gsbase(g_percpu_base + g_percpu_step * idx);
    // ASMV("movl %0,%%gs:(g_thiscpu_idx)" :: "r"(idx)， "m");
    THISCPU_SET(g_thiscpu_idx, idx);
    // ASMV("movl %1,%%gs:(%0)" : "=r"(g_thiscpu_idx) : "r"(idx));
}


INIT_TEXT size_t get_ist_nmi(int cpu) { return PERCPU(cpu, &g_percpu_nmi)->vend; }
INIT_TEXT size_t get_ist_df(int cpu) { return PERCPU(cpu, &g_percpu_df)->vend; }
INIT_TEXT size_t get_ist_pf(int cpu) { return PERCPU(cpu, &g_percpu_pf)->vend; }
INIT_TEXT size_t get_ist_mc(int cpu) { return PERCPU(cpu, &g_percpu_mc)->vend; }
INIT_TEXT size_t get_int_top(int cpu) { return PERCPU(cpu, &g_percpu_int)->vend; }


//------------------------------------------------------------------------------
// arch-api funcs
//------------------------------------------------------------------------------

inline int cpu_count() {
    return g_loapic_num;
}

inline int cpu_index() {
    int idx;
    ASMV("movl %%gs:(g_thiscpu_idx),%0" : "=r"(idx));
    ASSERT(read_gsbase() == g_percpu_base + g_percpu_step * idx);
    return idx;
}

inline void *percpu_ptr(int idx, void *ptr) {
    ASSERT(0 != g_percpu_base);
    ASSERT(0 != g_percpu_step);
    ASSERT(is_percpu_var(ptr));
    ASSERT(idx < cpu_count());

    return (uint8_t*)ptr + g_percpu_base + g_percpu_step * idx;
}

inline void *thiscpu_ptr(void *ptr) {
    ASSERT(0 != g_percpu_base);
    ASSERT(0 != g_percpu_step);
    ASSERT(is_percpu_var(ptr));
    ASSERT(read_gsbase() >= g_percpu_base);

    return (uint8_t*)ptr + read_gsbase();
}
