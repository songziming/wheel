#include "int_init.h"
#include <arch_intf.h>

#include <generic/gdt_idt_tss.h>
#include <memory/percpu.h>
#include <library/debug.h>


void *g_handlers[256];

PCPU_DATA int   g_int_depth = 0;
PCPU_DATA void *g_int_stack = NULL;


//------------------------------------------------------------------------------
// default handler
//------------------------------------------------------------------------------

static void handle_exception(int vec) {
    log("[CPU%d] exception 0x%x\n", cpu_index(), vec);
    cpu_halt();
}

static void handle_interrupt(int vec) {
    log("[CPU%d] interrupt 0x%x\n", cpu_index(), vec);
    // cpu_halt();
}




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
        g_handlers[i] = handle_exception;
    }
    for (int i = 32; i < 256; ++i) {
        g_handlers[i] = handle_interrupt;
    }
}
