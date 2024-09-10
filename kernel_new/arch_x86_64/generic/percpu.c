#include "percpu.h"
#include <arch_intf.h>
#include <arch_impl.h>
#include "rw.h"
#include "cpufeatures.h"
#include <memory/vm_space.h>
#include <library/debug.h>
#include <library/string.h>


// per-CPU area 不仅是变量，还包括中断栈、异常栈


// 每个处理器对应的 PCPU 区域内部结构如下：
// +-------+-----------+-------+--------+-------+--------+-----
// | guard | PCPU vars | guard | stack1 | guard | stack2 | ...
// +-------+-----------+-------+--------+-------+--------+-----
// 其中 PCPU vars 是静态声明的变量，只有这部分变量可以用 pcpu_ptr、this_ptr 访问


// defined in layout.ld
extern char _pcpu_addr;
extern char _pcpu_data_end;
extern char _pcpu_bss_end;

static CONST size_t g_pcpu_offset = 0; // 首个 pcpu 区域的偏移量，跳过 guard
static CONST size_t g_pcpu_skip = 0; // 相邻两个 pcpu 的间距

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

// 划分 per-cpu 存储空间，从指定地址开始，开头留出一个 guard page
// 传入的是虚拟地址
INIT_TEXT void percpu_init(size_t addr) {
    ASSERT(0 == g_pcpu_offset);
    ASSERT(0 != g_pcpu_skip);
    ASSERT(addr > KERNEL_TEXT_ADDR);
    ASSERT(cpu_count() > 0);

    size_t copy_size = (size_t)(&_pcpu_data_end - &_pcpu_addr);
    size_t zero_size = (size_t)(&_pcpu_bss_end  - &_pcpu_data_end);

    // 按 L1 大小对齐
    size_t l1size = g_l1d_info.line_size * g_l1d_info.sets;
    if (0 != l1size) {
        ASSERT(0 == (l1size & (l1size - 1)));
        g_pcpu_skip += l1size - 1;
        g_pcpu_skip &= ~(l1size - 1);
    }

    // per-cpu 起始地址按页对齐，并在开头留出一个 guard page
    addr += 2 * PAGE_SIZE - 1;
    addr &= ~(PAGE_SIZE - 1);
    g_pcpu_offset = addr - (size_t)&_pcpu_addr;

    for (int i = 0; i < cpu_count(); ++i) {
        char *ptr = (char *)addr; // per-cpu 映射到 kernel text
        addr += g_pcpu_skip;

        // memcpy(ptr, &_pcpu_addr, copy_size); // 复制 per-cpu data
        // memset(ptr + copy_size, 0, zero_size); // per-cpu bss 清零
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
