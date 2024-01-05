#include <cpu/gdt_idt_tss.h>
#include <wheel.h>



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



static CONST     uint64_t *g_gdt = NULL;
static CONST     idt_ent_t g_idt[256];
static PCPU_DATA tss_t     g_tss = {0};


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


// 64-bit IDT 只允许中断门和陷阱门，没有调用门
// 中断门和陷阱门有 ist，调用门没有
// 调用门可以放在 GDT、LDT 内部

INIT_TEXT void idt_init() {
    kmemset(g_idt, 0, sizeof(g_idt));

    for (int i = 0; i < 256; ++i) {
        g_idt[i].attr        = 0x8e; // dpl=0，type=E，中断门
        g_idt[i].selector    = 0x08; // 内核代码段
        g_idt[i].offset_low  =  isr_entries[i]        & 0xffff;
        g_idt[i].offset_mid  = (isr_entries[i] >> 16) & 0xffff;
        g_idt[i].offset_high = (isr_entries[i] >> 32) & 0xffffffff;
    }
}

INIT_TEXT void idt_load() {
    tbl_ptr_t idtr;
    idtr.base = (uint64_t)g_idt;
    idtr.limit = sizeof(g_idt) - 1;
    load_idtr(&idtr);
}

INIT_TEXT void idt_set_ist(int vec, int idx) {
    ASSERT(vec >= 0);
    ASSERT(vec < 256);
    ASSERT(idx > 0);
    ASSERT(idx < 8);

    g_idt[vec].ist = idx & 7;
}


INIT_TEXT void tss_init_load() {
    ASSERT(NULL != g_gdt);

    uint64_t addr = (uint64_t)this_ptr(&g_tss);
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

INIT_TEXT void tss_set_rsp(int cpu, int idx, uint64_t addr) {
    ASSERT(idx >= 0);
    ASSERT(idx < 3);

    tss_t *tss = pcpu_ptr(cpu, &g_tss);
    tss->rsp[idx].lower = addr & 0xffffffff;
    tss->rsp[idx].upper = (addr >> 32) & 0xffffffff;
}

INIT_TEXT void tss_set_ist(int cpu, int idx, uint64_t addr) {
    ASSERT(idx > 0);
    ASSERT(idx < 8);

    tss_t *tss = pcpu_ptr(cpu, &g_tss);
    tss->ist[idx].lower = addr & 0xffffffff;
    tss->ist[idx].upper = (addr >> 32) & 0xffffffff;
}
