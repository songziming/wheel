// 这个头文件会被 arch 之外的代码使用，是对外公开的 API
// 将平台相关的功能封装起来，提供一致的接口

// TODO 如果我们支持多种 arch，可以将 API 的统一部份提取出来，放在 common
//      每个 arch 只需要提供定制部分，例如 TCB、、per cpu、内联汇编

#ifndef ARCH_INTERFACE_H
#define ARCH_INTERFACE_H

#include <base.h>


// 中断时任务内核栈的内容
// 包括 CPU 自动入栈的寄存器和 scratch registers
typedef struct int_context {
    uint64_t    rbp;
    uint64_t    r11;
    uint64_t    r10;
    uint64_t    r9;
    uint64_t    r8;
    uint64_t    rax;
    uint64_t    rcx;
    uint64_t    rdx;
    uint64_t    rsi;
    uint64_t    rdi;

    uint64_t    err;
    uint64_t    rip;
    uint64_t    cs;
    uint64_t    rflags;
    uint64_t    rsp;
    uint64_t    ss;
} int_context_t;

// TCB 中保存的上下文
typedef struct regs {
    int_context_t *stack_top;

    uint64_t rbx;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;

    // TODO FPU、SSE、AVX 也需要保存
} task_regs_t;

// 虚拟页属性
enum page_flags {
    PAGE_WRITE   = 1,    // 可写
    PAGE_EXEC    = 2,    // 可执行
    PAGE_USER    = 4,    // 用户态可访问
    PAGE_GLOBAL  = 8,    // 全局页
};

// 是否清除分页缓存
typedef enum page_flush {
    NO_FLUSH_TLB = 0,
    FLUSH_TLB = 1,
} flush_flag_t;

// 页表在 page_t 里记录的信息
typedef struct page_info_mmu {
    int ent_count;  // 有效子条目数量
    int ref_count;  // 被多少个上级页表引用
} page_info_mmu_t;


static inline void cpu_halt() { __asm__("hlt"); }
static inline void cpu_pause() { __asm__("pause"); }
static inline void cpu_rfence() { __asm__("lfence" ::: "memory"); }
static inline void cpu_wfence() { __asm__("sfence" ::: "memory"); }
static inline void cpu_rwfence() { __asm__("mfence" ::: "memory"); }


// liba/atomic.S
uint32_t atomic32_add(uint32_t *ptr, uint32_t val);

// liba/mmu.c
size_t mmu_create();
size_t mmu_translate(size_t tbl, size_t va, uint32_t *flags);
void mmu_map(size_t tbl, size_t va, size_t pa, size_t npages, uint32_t flags);
void mmu_unmap(size_t tbl, size_t va, size_t npages, flush_flag_t flush);

// arch_mem.c
INIT_TEXT void *early_const_alloc(size_t size);
INIT_TEXT void *early_alloc(size_t size);

// arch_debug.c
int unwind(void **addrs, int max);

// arch_smp.c
int64_t get_cpu_count();
int     get_cpu_index();

void *pcpu_ptr(int idx, void *ptr);
void *this_ptr(void *ptr);

// TODO 还需提供更丰富的 thiscpu 操作函数
#define THISCPU_GET(var) ({                                         \
    __typeof__(var) ret;                                            \
    switch (sizeof(var)) {                                          \
    case 1: __asm__("movb %%gs:(" #var "), %0" : "=r"(ret)); break; \
    case 2: __asm__("movw %%gs:(" #var "), %0" : "=r"(ret)); break; \
    case 4: __asm__("movl %%gs:(" #var "), %0" : "=r"(ret)); break; \
    case 8: __asm__("movq %%gs:(" #var "), %0" : "=r"(ret)); break; \
    }                                                               \
    ret;                                                            \
})
#define THISCPU_SET(var, val) ({                                    \
    switch (sizeof(var)) {                                          \
    case 1: __asm__("movb %0, %%gs:(" #var ")" :: "r"(val)); break; \
    case 2: __asm__("movw %0, %%gs:(" #var ")" :: "r"(val)); break; \
    case 4: __asm__("movl %0, %%gs:(" #var ")" :: "r"(val)); break; \
    case 8: __asm__("movq %0, %%gs:(" #var ")" :: "r"(val)); break; \
    }                                                               \
})

// arch_int.c
int get_int_depth();
int cpu_int_lock();
void cpu_int_unlock(int key);

// arch_entries.S
void switch_task(); // 立即切换到 tid_next

// arch_task.c
void task_regs_init(task_regs_t *regs, uint8_t stack[], size_t size, void *entry);

#endif // ARCH_INTERFACE_H
