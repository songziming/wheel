#include "loapic.h"
#include <arch_intf.h>
#include <arch_impl.h>
#include <memory/early_alloc.h>
#include <library/debug.h>



// APIC 的作用不仅是中断处理，还可以描述 CPU 拓扑（调度）、定时、多核
// 但是主要负责中断，因此放在中断模块下
// linux kernel 将 apic 作为一个独立子模块

// apic 的初始化却必须放在 smp 模块




static CONST size_t    g_loapic_addr;   // 虚拟地址
static CONST int       g_loapic_num = 0;
static CONST loapic_t *g_loapics = NULL;




INIT_TEXT void loapic_alloc(size_t base, int n) {
    ASSERT(base < DIRECT_MAP_ADDR);

    g_loapic_addr = base;
    g_loapic_num = n;
    g_loapics = early_alloc_ro(n * sizeof(loapic_t));
}

INIT_TEXT void loapic_parse(int i, madt_loapic_t *tbl) {
    ASSERT(g_loapic_num > 0);
    ASSERT(i >= 0);
    ASSERT(i < g_loapic_num);

    g_loapics[i].apic_id      = tbl->id;
    g_loapics[i].processor_id = tbl->processor_id;
    g_loapics[i].flags        = tbl->loapic_flags;
}

INIT_TEXT void loapic_parse_x2(int i, madt_lox2apic_t *tbl) {
    ASSERT(g_loapic_num > 0);
    ASSERT(i >= 0);
    ASSERT(i < g_loapic_num);

    g_loapics[i].apic_id      = tbl->id;
    g_loapics[i].processor_id = tbl->processor_id;
    g_loapics[i].flags        = tbl->loapic_flags;
}



inline int cpu_count() {
    ASSERT(0 != g_loapic_num);
    return g_loapic_num;
}

