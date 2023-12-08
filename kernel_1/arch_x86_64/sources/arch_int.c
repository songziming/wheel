// 异常和中断处理

#include <arch_int.h>
#include <arch_interface.h>
#include <arch_smp.h>
#include <arch_mem.h>
#include <arch_debug.h>
#include <liba/rw.h>
#include <dev/loapic.h>

#include <debug.h>
#include <page.h>
#include <libk.h>
#include <spin.h>


static PCPU_BSS vmrange_t g_rng_int_stack;
// static PCPU_BSS pfn_t g_int_stack_pn;

PCPU_BSS size_t g_int_rsp;   // 中断栈栈顶，发生中断时切换到这里
PCPU_BSS int    g_int_depth; // 表示当前中断深度

// 中断响应函数表
int_handler_t g_handlers[256];

static spin_t int_print_lock = SPIN_INIT;



int get_int_depth() {
    int depth;
    __asm__("movl %%gs:(g_int_depth), %0" : "=a"(depth));
    return depth;
}

int cpu_int_lock() {
    uint64_t flags;
    __asm__("pushfq; cli; popq %0" : "=r"(flags));
    return (int)(flags & 0x200);
}

void cpu_int_unlock(int key) {
    if (key & 0x200) {
        __asm__("sti");
    }
}



// 中断统一入口
void dispatch_int(int vec, int_context_t *ctx) {
    if (NULL != g_handlers[vec]) {
        g_handlers[vec](ctx);
        return;
    }

    spin_take(&int_print_lock);

    dbg_print("interrupt vec=%d, CPU #%d\n", vec, get_cpu_index());
    dbg_print("  => ss:rsp = %04lx:%016lx\n", ctx->ss & 0xffff, ctx->rsp);
    dbg_print("  => cs:rip = %04lx:%016lx\n", ctx->cs & 0xffff, ctx->rip);
    dbg_print("  => error  = %lx\n", ctx->err);

    spin_give(&int_print_lock);

    void *frames[32];
    frames[0] = (void *)ctx->rip;
    int depth = unwind_from(&frames[1], 31, ctx->rbp);
    dbg_show_trace(frames, depth);

    while (1) {
        cpu_halt();
    }
}

// NMI 表示严重的硬件错误，直接停机即可
// Linux 需要容错，必须响应 NMI，并通过 IST 为 NMI 指定了专用栈，我们不需要这个复杂
void handle_nmi(int_context_t *ctx) {
    (void)ctx;

    // 读取 system control port A/B，分析 NMI 的原因
    uint8_t a = in_byte(0x92);
    uint8_t b = in_byte(0x61);

    if (a & (1 << 4)) {
        dbg_print("NMI: watchdog timer\n");
    }
    if (b & (1 << 6)) {
        dbg_print("NMI: bus error\n");
    }
    if (b & (1 << 7)) {
        dbg_print("NMI: memory error\n");
    }

    cpu_halt();
}

INIT_TEXT void int_init() {
    int64_t ncpu = get_cpu_count();
    ASSERT(ncpu > 0);

    // 设置中断/异常处理函数
    memset(g_handlers, 0, sizeof(g_handlers));
    g_handlers[2]  = handle_nmi;
    g_handlers[14] = handle_pagefault;

    // 计算每个中断栈的页数、大小
    pfn_t stack_pages = 1 << INT_STACK_RANK;
    size_t stack_size = PAGE_SIZE << INT_STACK_RANK;

    // 给每个 CPU 的中断栈分配物理内存、建立映射
    for (int i = 0; i < ncpu; ++i) {
        vmrange_t *rng = pcpu_ptr(i, &g_rng_int_stack);
        rng->desc = "int stack";
        size_t va = vmspace_reserve(&g_kernel_vm, stack_size, rng,
                DYNAMIC_MAP_ADDR, DYNAMIC_MAP_END, RF_GUARD|RF_DOWN);

        // TODO 各 CPU 的中断栈需要映射到相同的 L1 缓存行，使用染色版本的页面分配
        size_t pa = (size_t)page_block_alloc(INT_STACK_RANK, PT_STACK) << PAGE_SHIFT;
        mmu_map(g_kernel_map, va, pa, stack_pages, PAGE_WRITE|PAGE_GLOBAL);

        *(size_t *)pcpu_ptr(i, &g_int_rsp) = va + stack_size;
        *(int *)pcpu_ptr(i, &g_int_depth) = 0;
    }
}
