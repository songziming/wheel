#include "percpu.h"
#include <arch_intf.h>
#include <arch_impl.h>
#include "rw.h"
#include "cpufeatures.h"
#include <memory/vm_space.h>
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
// 其中 PCPU vars 是静态声明的变量，只有这部分变量可以用 pcpu_ptr、this_ptr 访问


// layout.ld
extern char _pcpu_addr;
extern char _pcpu_data_end;
extern char _pcpu_bss_end;

static CONST size_t g_pcpu_addr = 0;
static CONST size_t g_pcpu_vars_size = 0; // 页对齐
static CONST size_t g_pcpu_size = 0;
static CONST size_t g_pcpu_skip = 0; // 相邻两个 pcpu 的间距
static CONST size_t g_pcpu_offset = 0; // 首个 pcpu 区域的偏移量，跳过 guard

// // 用来描述一个 percpu 的内部结构
// static vmspace_t g_percpu_space;
// static PCPU_BSS vmrange_t g_percpu_vars; // data + bss
// static PCPU_BSS vmrange_t g_percpu_nmi;  // NMI IST
// static PCPU_BSS vmrange_t g_percpu_df;   // #DF IST
// static PCPU_BSS vmrange_t g_percpu_pf;   // #PF IST
// static PCPU_BSS vmrange_t g_percpu_mc;   // #MC IST
// static PCPU_BSS vmrange_t g_percpu_int;  // int stack

// #ifdef DEBUG
// static char g_gsbase_set = 0; // 标记 gsbase 是否已经初始化
// #endif

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


// // TODO 记录 per-cpu map，类似于 vmmap，所有 percpu 共享同一个 map
// static vmspace_t g_percpu_map;
// static vmrange_t g_percpu_data; // percpu-data、percpu-rodata


// percpu 初始化流程：
//  percpu_reserve 可以调用多次，标记需要
//  percpu_init 将真正的 per-cpu 数据复制多份
//  thiscpu_init（对每个 cpu 都要执行）


// 在 per-cpu 区域中预留一段空间，返回在 per-cpu 中的偏移，和前一个 range 留有 guard page
INIT_TEXT size_t percpu_reserve(size_t size, size_t align) {
    ASSERT(0 == g_pcpu_offset);

    if (0 != g_pcpu_skip) {
        g_pcpu_skip += 2 * PAGE_SIZE - 1;
        g_pcpu_skip &= ~(PAGE_SIZE - 1);
    }

    if (0 != align) {
        ASSERT(0 == (align & (align - 1)));
        g_pcpu_skip += align - 1;
        g_pcpu_skip &= ~(align - 1);
    }

    size_t ret = g_pcpu_skip;
    g_pcpu_skip += size;
    return ret;
}


// percpu range 已经划分完成，整体间距按 L1 对齐，保证各个 cpu 的着色相同
// 此函数之后不能再调用 percpu_reserve
INIT_TEXT size_t percpu_align_to_l1() {
    ASSERT(0 == g_pcpu_offset);
    ASSERT(0 != g_pcpu_skip);

    // 按 L1 大小对齐
    size_t l1size = g_l1d_info.line_size * g_l1d_info.sets;
    if (0 != l1size) {
        ASSERT(0 == (l1size & (l1size - 1)));
        g_pcpu_skip += l1size - 1;
        g_pcpu_skip &= ~(l1size - 1);
    }

    return g_pcpu_skip;
}



// 一步到位，划分完整的 percpu 区域
INIT_TEXT void percpu_setup(size_t va) {
    ASSERT(va > KERNEL_TEXT_ADDR);
    ASSERT(cpu_count() > 0);
}


// 输入 percpu 的起始虚拟地址
// 返回所有 percpu 占据的空间，包括了 N-1 个中间补齐
INIT_TEXT size_t percpu_allocate(size_t va) {
    ASSERT(va > KERNEL_TEXT_ADDR);
    ASSERT(cpu_count() > 0);

    g_pcpu_offset = va - (size_t)&_pcpu_addr;

    size_t percpu_size = g_pcpu_skip;

    // 按 L1 大小对齐
    size_t l1size = g_l1d_info.line_size * g_l1d_info.sets;
    if (0 != l1size) {
        ASSERT(0 == (l1size & (l1size - 1)));
        g_pcpu_skip += l1size - 1;
        g_pcpu_skip &= ~(l1size - 1);
    }

    return g_pcpu_skip * (cpu_count() - 1) + percpu_size;
}


// 划分 per-cpu 存储空间，从指定地址开始，开头留出一个 guard page
// 传入的是虚拟地址，返回分配了所有的 percpu 之后的结束地址
// 开头没有 guard page，这样输入和输出正好是 percpu 范围的起止
INIT_TEXT size_t percpu_init(size_t va) {
    ASSERT(0 == g_pcpu_size);
    ASSERT(0 == g_pcpu_skip);
    ASSERT(0 == g_pcpu_offset);
    ASSERT(va > KERNEL_TEXT_ADDR);

    size_t copy_size = (size_t)(&_pcpu_data_end - &_pcpu_addr);
    size_t zero_size = (size_t)(&_pcpu_bss_end  - &_pcpu_data_end);

    g_pcpu_vars_size = copy_size + zero_size;
    g_pcpu_vars_size += PAGE_SIZE - 1;
    g_pcpu_vars_size &= ~(PAGE_SIZE - 1);

    g_pcpu_size  = g_pcpu_vars_size;
    g_pcpu_size += PAGE_SIZE + INT_STACK_SIZE; // NMI 异常栈
    g_pcpu_size += PAGE_SIZE + INT_STACK_SIZE; // #PF 异常栈
    g_pcpu_size += PAGE_SIZE + INT_STACK_SIZE; // #DF 异常栈
    g_pcpu_size += PAGE_SIZE + INT_STACK_SIZE; // #MC 异常栈
    g_pcpu_size += PAGE_SIZE + INT_STACK_SIZE; // 中断栈

    g_pcpu_skip  = g_pcpu_size + PAGE_SIZE; // 相邻两个 percpu 之间要留有 guard page

    // 按 L1 大小对齐
    size_t l1size = g_l1d_info.line_size * g_l1d_info.sets;
    if (0 != l1size) {
        ASSERT(0 == (l1size & (l1size - 1)));
        g_pcpu_skip += l1size - 1;
        g_pcpu_skip &= ~(l1size - 1);
    }

    int ncpu = cpu_count();
    ASSERT(ncpu > 0);

    // per-cpu 起始地址按页对齐，并算出结束位置
    va += PAGE_SIZE - 1;
    va &= ~(PAGE_SIZE - 1);
    g_pcpu_offset = va - (size_t)&_pcpu_addr;
    size_t end = va + g_pcpu_skip * (ncpu - 1) + g_pcpu_size;

    for (int i = 0; i < ncpu; ++i) {
        char *ptr = (char *)va;
        va += g_pcpu_skip;

        memcpy(ptr, &_pcpu_addr, copy_size); // 复制 per-cpu data
        memset(ptr + copy_size, 0, zero_size); // per-cpu bss 清零
    }

    return end;
}

INIT_TEXT size_t get_nmi_stack(int cpu) {
    ASSERT(cpu >= 0);
    ASSERT(cpu < cpu_count());
    return g_pcpu_addr + g_pcpu_skip * cpu
        + g_pcpu_vars_size + PAGE_SIZE;
}

INIT_TEXT size_t get_pf_stack(int cpu) {
    ASSERT(cpu >= 0);
    ASSERT(cpu < cpu_count());
    return g_pcpu_addr + g_pcpu_skip * cpu
        + g_pcpu_vars_size + PAGE_SIZE
        + INT_STACK_SIZE + PAGE_SIZE;
}

INIT_TEXT size_t get_df_stack(int cpu) {
    ASSERT(cpu >= 0);
    ASSERT(cpu < cpu_count());
    return g_pcpu_addr + g_pcpu_skip * cpu
        + g_pcpu_vars_size + PAGE_SIZE
        + (INT_STACK_SIZE + PAGE_SIZE) * 2;
}

INIT_TEXT size_t get_mc_stack(int cpu) {
    ASSERT(cpu >= 0);
    ASSERT(cpu < cpu_count());
    return g_pcpu_addr + g_pcpu_skip * cpu
        + g_pcpu_vars_size + PAGE_SIZE
        + (INT_STACK_SIZE + PAGE_SIZE) * 3;
}

INIT_TEXT size_t get_int_stack(int cpu) {
    ASSERT(cpu >= 0);
    ASSERT(cpu < cpu_count());
    return g_pcpu_addr + g_pcpu_skip * cpu
        + g_pcpu_vars_size + PAGE_SIZE
        + (INT_STACK_SIZE + PAGE_SIZE) * 4;
}


// 回收 perpcu 里面用来对齐的部分
INIT_TEXT void percpu_reclaim_guard() {
    size_t addr = g_pcpu_addr;
    size_t next = g_pcpu_addr;

    int ncpu = cpu_count();
    for (int i = 0; i < ncpu; ++i) {
        pages_free(addr, next); // 和前一个 percpu 的间隔
        next = addr += g_pcpu_skip;

        addr += g_pcpu_vars_size; // percpu var
        pages_free(addr, addr + PAGE_SIZE);
        addr += INT_STACK_SIZE; // NMI
        pages_free(addr, addr + PAGE_SIZE);
        addr += INT_STACK_SIZE; // #PF
        pages_free(addr, addr + PAGE_SIZE);
        addr += INT_STACK_SIZE; // #DF
        pages_free(addr, addr + PAGE_SIZE);
        addr += INT_STACK_SIZE; // #MC
        pages_free(addr, addr + PAGE_SIZE);
        addr += INT_STACK_SIZE; // Int stack
    }
}





// 设置 this-cpu 指针，并且设置 CPU 编号
INIT_TEXT void gsbase_init(int idx) {
    ASSERT(0 != g_pcpu_offset);
    ASSERT(0 != g_pcpu_skip);
    ASSERT(idx < cpu_count());
// #ifdef DEBUG
//     g_gsbase_set = 1;
// #endif
    write_gsbase(g_pcpu_offset + g_pcpu_skip * idx);
}

// 依赖 gsbase
inline void *this_ptr(void *ptr) {
    ASSERT(0 != g_pcpu_offset);
    ASSERT(0 != g_pcpu_skip);
    ASSERT(is_pcpu_var(ptr));
// #ifdef DEBUG
//     ASSERT(g_gsbase_set);
// #endif
    return (uint8_t *)ptr + read_gsbase();
}

// 依赖 gsbase
inline int cpu_index() {
    ASSERT(0 != g_pcpu_offset);
    ASSERT(0 != g_pcpu_skip);
// #ifdef DEBUG
//     ASSERT(g_gsbase_set);
// #endif
    size_t base = (size_t)read_gsbase() - g_pcpu_offset;
    ASSERT(0 == (base % g_pcpu_skip));

    return base / g_pcpu_skip;
}
