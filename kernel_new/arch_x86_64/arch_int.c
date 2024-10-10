#include "arch_int.h"
#include <arch_intf.h>
#include "arch_impl.h"
#include <cpu/rw.h>
#include <cpu/gdt_idt_tss.h>
#include <memory/percpu.h>
#include <library/debug.h>
#include <library/dwarf.h>


PERCPU_DATA int   g_int_depth = 0;
PERCPU_DATA void *g_int_stack = NULL;

void *g_handlers[256];


//------------------------------------------------------------------------------
// 公开函数
//------------------------------------------------------------------------------

inline int cpu_int_lock() {
    uint64_t key;
    __asm__("pushfq; cli; popq %0" : "=r"(key));
    return (key & 0x200) ? 1 : 0;
}

inline void cpu_int_unlock(int key) {
    if (key) {
        __asm__("sti");
    }
}

inline int cpu_int_depth() {
    return THISCPU_GET(g_int_depth);
}

void set_int_handler(int vec, void (*handler)(int, regs_t *)) {
    g_handlers[vec] = handler;
}

//------------------------------------------------------------------------------
// default handler
//------------------------------------------------------------------------------

static void on_exception(int vec, regs_t *f) {
    log("[CPU%d] exception %d\n", cpu_index(), vec);
    log("  rip=%lx rsp=%lx\n", f->rip, f->rsp);

    log_stacktrace();
    cpu_halt();
}

static void on_interrupt(int vec, regs_t *f) {
    log("[CPU%d] interrupt %d\n", cpu_index(), vec);
    log("  rip=%lx rsp=%lx\n", f->rip, f->rsp);
    // cpu_halt();
}

static void on_generic_protect(int vec UNUSED, regs_t *f) {
    ASSERT(13 == vec);

    log("general protection\n");
    log("cpu%d rip=%lx rsp=%lx\n", cpu_index(), f->rip, f->rsp);
    log("errcode %zx\n", f->errcode);

    char *file = NULL;
    int line = addr_to_line(f->rip, &file);
    log("called from %s:%d\n", file, line);
}

static void on_page_fault(int vec UNUSED, regs_t *f) {
    ASSERT(14 == vec);

    uint64_t va = read_cr2();
    const char *p  = (f->errcode & 1) ? "non-exist " : "";
    const char *wr = (f->errcode & 2) ? "write to" : "read from";
    const char *us = (f->errcode & 4) ? "user mode" : "kernel";
    if (f->errcode & 16) {
        wr = "execute";
    }

    log("page fault %s %sva 0x%zx under %s\n", wr, p, va, us);

    if (f->errcode & 8) {
        // 页表项中的保留位必须是 0
        log("reserved bit set in paging-structure\n");
    }

    // TODO 获取当前 vmspace，定位所在 vmrange 的名称
    // 如果支持动态映射，应该在这里找到 range，执行里面的 handler，更新映射

    char *file = NULL;
    int line = addr_to_line(f->rip, &file);
    log("called from %s:%d\n", file, line);
}


//------------------------------------------------------------------------------
// 初始化
//------------------------------------------------------------------------------

INIT_TEXT void int_init() {
    // 填充所有 TSS 的 IST 字段
    for (int i = 0; i < cpu_count(); ++i) {
        tss_set_ist(i, 1, percpu_nmi_stack_top(i));
        tss_set_ist(i, 2, percpu_df_stack_top(i));
        tss_set_ist(i, 3, percpu_pf_stack_top(i));
        tss_set_ist(i, 4, percpu_mc_stack_top(i));

        int *pdepth = percpu_ptr(i, &g_int_depth);
        void **pstack = percpu_ptr(i, &g_int_stack);
        *pdepth = 0;
        *pstack = (void *)percpu_int_stack_top(i);
    }

    // 在 IDT 里面填入 IST 编号
    idt_set_ist(2,  1); // NMI
    idt_set_ist(8,  2); // #DF
    idt_set_ist(14, 3); // #PF
    idt_set_ist(18, 4); // #MC

    // 设置中断处理函数
    for (int i = 0; i < 32; ++i) {
        g_handlers[i] = on_exception;
    }
    for (int i = 32; i < 256; ++i) {
        g_handlers[i] = on_interrupt;
    }

    g_handlers[13] = on_generic_protect;
    g_handlers[14] = on_page_fault;
}
