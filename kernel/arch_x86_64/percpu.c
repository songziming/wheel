#include <wheel.h>
#include <arch_api.h>
#include <vmspace.h>


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


// percpu (sub)sections
static PERCPU_BSS vmrange_t g_percpu_vars; // data + bss
static PERCPU_BSS vmrange_t g_percpu_nmi;  // NMI IST
static PERCPU_BSS vmrange_t g_percpu_df;   // #DF IST
static PERCPU_BSS vmrange_t g_percpu_pf;   // #PF IST
static PERCPU_BSS vmrange_t g_percpu_mc;   // #MC IST
static PERCPU_BSS vmrange_t g_percpu_int;  // int stack



INIT_TEXT void percpu_init() {
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
}



void *percpu_ptr(int idx, void *ptr) {
    return (uint8_t*)ptr + g_percpu_base + g_percpu_step * idx;
}

