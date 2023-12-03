// 控制 CPU 相关表结构

#include <arch_cpu.h>
#include <wheel.h>
#include <str.h>







typedef struct idt_ent {
    uint16_t offset_low;
    uint16_t selector;
    uint16_t attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
} PACKED idt_ent_t;

typedef struct tss {
    uint32_t reserved1;
    uint32_t rsp0_lower;
    uint32_t rsp0_upper;
    uint32_t rsp1_lower;
    uint32_t rsp1_upper;
    uint32_t rsp2_lower;
    uint32_t rsp2_upper;
    uint32_t reserved2;
    uint32_t reserved3;
    uint32_t ist1_lower;
    uint32_t ist1_upper;
    uint32_t ist2_lower;
    uint32_t ist2_upper;
    uint32_t ist3_lower;
    uint32_t ist3_upper;
    uint32_t ist4_lower;
    uint32_t ist4_upper;
    uint32_t ist5_lower;
    uint32_t ist5_upper;
    uint32_t ist6_lower;
    uint32_t ist6_upper;
    uint32_t ist7_lower;
    uint32_t ist7_upper;
    uint32_t reserved4;
    uint32_t reserved5;
    uint16_t reserved6;
    uint16_t io_map_base;
} PACKED tss_t;

typedef struct tbl_ptr {
    uint16_t limit;
    uint64_t base;
} PACKED tbl_ptr_t;



static CONST    uint64_t *g_gdt = NULL;
static CONST    idt_ent_t g_idt[256];
static PCPU_BSS tss_t     g_tss;


// arch_entries.S
extern uint64_t isr_entries[256];

// ./support.S
void load_gdtr(tbl_ptr_t *ptr);
void load_idtr(tbl_ptr_t *ptr);
void load_tr  (uint16_t sel);



// 为 tss 段留出空间，但是不初始化内容
INIT_TEXT void gdt_init() {
    ASSERT(NULL == g_gdt);

    int ncpu = cpu_count();
    ASSERT(0 != ncpu);

    g_gdt = (uint64_t *)early_alloc_ro((6 + ncpu * 2) * sizeof(uint64_t));
    g_gdt[0] = 0UL;                   // dummy
    g_gdt[1] = 0x00a0980000000000UL;  // 内核代码段
    g_gdt[2] = 0x00c0920000000000UL;  // 内核数据段
    g_gdt[3] = 0UL;                   // 保留
    g_gdt[4] = 0x00c0f20000000000UL;  // 用户代码段
    g_gdt[5] = 0x00a0f80000000000UL;  // 用户数据段
}

INIT_TEXT void gdt_load() {
    tbl_ptr_t gdtr;
    gdtr.base = (uint64_t)g_gdt;
    gdtr.limit = (6 + cpu_count() * 2) * sizeof(uint64_t) - 1;
    load_gdtr(&gdtr);
}


INIT_TEXT void idt_init() {
    for (int i = 0; i < 256; ++i) {
        g_idt[i].attr        = 0x8e00;  // dpl = 0
        g_idt[i].selector    = 0x08;    // 内核代码段
        g_idt[i].offset_low  =  isr_entries[i]        & 0xffff;
        g_idt[i].offset_mid  = (isr_entries[i] >> 16) & 0xffff;
        g_idt[i].offset_high = (isr_entries[i] >> 32) & 0xffffffff;
        g_idt[i].reserved    = 0;
    }
}

INIT_TEXT void idt_load() {
    tbl_ptr_t idtr;
    idtr.base = (uint64_t)g_idt;
    idtr.limit = sizeof(g_idt) - 1;
    load_idtr(&idtr);
}


INIT_TEXT void tss_init_load() {
    ASSERT(NULL != g_gdt);

    tss_t *tss = this_ptr(&g_tss);
    bset(tss, 0, sizeof(tss_t));

    uint64_t addr = (uint64_t)tss;
    uint64_t size = sizeof(tss_t);

    uint64_t lower = 0UL;
    uint64_t upper = 0UL;
    lower |=  size        & 0x000000000000ffffUL;   // limit [15:0]
    lower |= (addr << 16) & 0x000000ffffff0000UL;   // base  [23:0]
    lower |= (size << 32) & 0x000f000000000000UL;   // limit [19:16]
    lower |= (addr << 32) & 0xff00000000000000UL;   // base  [31:24]
    lower |=                0x0000e90000000000UL;   // present 64bit ring3
    upper  = (addr >> 32) & 0x00000000ffffffffUL;   // base  [63:32]

    int idx = cpu_index();
    g_gdt[2 * idx + 6] = lower;
    g_gdt[2 * idx + 7] = upper;
    load_tr(((2 * idx + 6) << 3) | 3);
}

