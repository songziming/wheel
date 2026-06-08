#include "gdt_idt_tss.h"
#include "arch_api.h"

#include <early_alloc.h>
#include <kstring.h>
#include <debug.h>


typedef struct idt_ent {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
} PACKED idt_ent_t;

typedef struct tss {
    uint32_t reserved1;
    struct {
        uint32_t lower;
        uint32_t upper;
    } rsp[3]; // 0,1,2
    struct {
        uint32_t lower;
        uint32_t upper;
    } ist[8]; // 0..7，其中 ist[0] 保留
    uint32_t reserved4;
    uint32_t reserved5;
    uint16_t reserved6;
    uint16_t io_map_base;
} PACKED tss_t;

typedef struct tbl_ptr {
    uint16_t limit;
    uint64_t base;
} PACKED tbl_ptr_t;



static CONST uint64_t *g_gdt = NULL;
static CONST idt_ent_t g_idt[256];
PERCPU_DATA  tss_t     g_tss = {0};

// gdt_idt_tss.S
void load_gdtr(tbl_ptr_t *ptr);
void load_idtr(tbl_ptr_t *ptr);
void load_tr  (uint16_t sel);



// 为 tss 段留出空间，但是不初始化内容
INIT_TEXT void gdt_init() {
    ASSERT(NULL == g_gdt);

    int ncpu = cpu_count();
    ASSERT(0 != ncpu);

    g_gdt = (uint64_t*)early_alloc_ro((6 + ncpu * 2) * sizeof(uint64_t));
    g_gdt[0] = 0UL;                   // dummy
    g_gdt[1] = 0x00a0980000000000UL;  // 内核代码段
    g_gdt[2] = 0x00c0920000000000UL;  // 内核数据段
    g_gdt[3] = 0UL;                   // 保留
    g_gdt[4] = 0x00c0f20000000000UL;  // 用户数据段
    g_gdt[5] = 0x00a0f80000000000UL;  // 用户代码段
}

INIT_TEXT void gdt_load() {
    tbl_ptr_t gdtr;
    gdtr.base = (uint64_t)g_gdt;
    gdtr.limit = (6 + cpu_count() * 2) * sizeof(uint64_t) - 1;
    load_gdtr(&gdtr);
}

INIT_TEXT void idt_init() {
    kmemset(g_idt, 0, sizeof(g_idt));
}

INIT_TEXT void idt_load() {
    tbl_ptr_t idtr;
    idtr.base = (uint64_t)g_idt;
    idtr.limit = sizeof(g_idt) - 1;
    load_idtr(&idtr);
}

INIT_TEXT void idt_set_isr(uint8_t vec, uint64_t isr, int dpl) {
    g_idt[vec].attr        = 0x8e | ((dpl & 3) << 5); // type=E 中断门
    g_idt[vec].selector    = 0x08; // 内核代码段
    g_idt[vec].offset_low  =  isr        & 0xffff;
    g_idt[vec].offset_mid  = (isr >> 16) & 0xffff;
    g_idt[vec].offset_high = (isr >> 32) & 0xffffffff;
}

// 一共 7 个 IST
INIT_TEXT void idt_set_ist(uint8_t vec, int ist) {
    ASSERT(ist > 0);
    ASSERT(ist < 8);
    g_idt[vec].ist = ist & 7;
}

INIT_TEXT void thistss_init_load(int cpu) {
    ASSERT(NULL != g_gdt);

    uint64_t addr = (uint64_t)thiscpu_ptr(&g_tss);
    uint64_t size = sizeof(tss_t);

    uint64_t lower = 0UL;
    uint64_t upper = 0UL;
    lower |=  size        & 0x000000000000ffffUL;   // limit [15:0]
    lower |= (addr << 16) & 0x000000ffffff0000UL;   // base  [23:0]
    lower |= (size << 32) & 0x000f000000000000UL;   // limit [19:16]
    lower |= (addr << 32) & 0xff00000000000000UL;   // base  [31:24]
    lower |=                0x0000e90000000000UL;   // present 64bit ring3
    upper  = (addr >> 32) & 0x00000000ffffffffUL;   // base  [63:32]

    // int cpu = cpu_index();
    g_gdt[2 * cpu + 6] = lower;
    g_gdt[2 * cpu + 7] = upper;
    load_tr(((2 * cpu + 6) << 3) | 3);
}

// 某些中断需要使用确定的栈，amd64 提供了 7 个 IST
INIT_TEXT void thistss_set_ist(int ist, uint64_t addr) {
    ASSERT(ist > 0);
    ASSERT(ist < 8);
    THISCPU_SET(g_tss.ist[ist].lower, (uint32_t)addr & 0xffffffff);
    THISCPU_SET(g_tss.ist[ist].upper, (uint32_t)(addr >> 32) & 0xffffffff);
}
