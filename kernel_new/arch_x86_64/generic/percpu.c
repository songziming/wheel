#include <common.h>
#include <arch_intf.h>
#include "rw.h"
#include <library/debug.h>


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

#ifdef DEBUG
static char g_gsbase_set = 0; // 标记 gsbase 是否已经初始化
#endif

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

// 设置 this-cpu 指针，并且设置 CPU 编号
INIT_TEXT void gsbase_init(int idx) {
    ASSERT(0 != g_pcpu_offset);
    ASSERT(0 != g_pcpu_skip);
    ASSERT(idx < cpu_count());
#ifdef DEBUG
    g_gsbase_set = 1;
#endif
    write_gsbase(g_pcpu_offset + g_pcpu_skip * idx);
}

// 依赖 gsbase
inline void *this_ptr(void *ptr) {
    ASSERT(0 != g_pcpu_offset);
    ASSERT(0 != g_pcpu_skip);
    ASSERT(is_pcpu_var(ptr));
#ifdef DEBUG
    ASSERT(g_gsbase_set);
#endif
    return (uint8_t *)ptr + read_gsbase();
}

// 依赖 gsbase
inline int cpu_index() {
    ASSERT(0 != g_pcpu_offset);
    ASSERT(0 != g_pcpu_skip);
#ifdef DEBUG
    ASSERT(g_gsbase_set);
#endif
    size_t base = (size_t)read_gsbase() - g_pcpu_offset;
    ASSERT(0 == (base % g_pcpu_skip));

    return base / g_pcpu_skip;
}
