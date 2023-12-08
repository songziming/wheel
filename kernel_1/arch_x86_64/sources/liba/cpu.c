#include <liba/cpu.h>
#include <liba/rw.h>
#include <liba/cpuid.h>
#include <arch_smp.h>
#include <dev/loapic.h>

#include <arch_interface.h>
#include <debug.h>
#include <libk.h>



typedef struct tbl_ptr {
    uint16_t limit;
    uint64_t base;
} PACKED tbl_ptr_t;

// 定义在 cpu.S
INIT_TEXT void load_gdtr(tbl_ptr_t *ptr);
INIT_TEXT void load_idtr(tbl_ptr_t *ptr);
INIT_TEXT void load_tr(uint16_t sel);





//------------------------------------------------------------------------------
// 功能开关
//------------------------------------------------------------------------------

INIT_TEXT void cpu_feat_init() {
    uint64_t efer = read_msr(MSR_EFER);
    if (CPU_FEATURE_NX & g_cpu_features) {
        efer |= 1UL << 11;  // NXE
    }
    write_msr(MSR_EFER, efer);

    uint64_t misc = read_msr(MSR_MISC);
    misc |= 1;  // fast string enable
    write_msr(MSR_MISC, misc);

    uint64_t cr0 = read_cr0();
    cr0 |=  (1UL << 16); // WP 分页写保护
    write_cr0(cr0);

    uint64_t cr4 = read_cr4();
    cr4 |= 1UL << 2; // time stamp counter
    cr4 |= 1UL << 7; // PGE 全局页（不会从 TLB 中清除）
    if (CPU_FEATURE_PCID & g_cpu_features) {
        cr4 |= 1UL << 17; // PCIDE 上下文标识符
    }
    write_cr4(cr4);
}


//------------------------------------------------------------------------------
// 全局描述符表
//------------------------------------------------------------------------------

// 每个 CPU 都有自己的拷贝
static PCPU_BSS uint64_t g_gdt[8];

INIT_TEXT void gdt_init_load() {
    uint64_t *this_gdt = (uint64_t *)this_ptr(g_gdt);
    memset(this_gdt, 0, sizeof(g_gdt));

    // 代码段：L=1，D=0，P=1，C=0
    // 数据段：P=1，W=1
    // 数据段的 W 位必须置一，尽管 AMD 文档说这个比特被忽略
    this_gdt[1] = 0x0020980000000000UL;  // kernel code
    this_gdt[2] = 0x0000920000000000UL;  // kernel data
    this_gdt[3] = 0x0020f80000000000UL;  // user code
    this_gdt[4] = 0x0000f20000000000UL;  // user data

    tbl_ptr_t gdtr = {
        .limit = sizeof(g_gdt) - 1,
        .base = (uint64_t)this_gdt,
    };
    load_gdtr(&gdtr);
}


//------------------------------------------------------------------------------
// 任务状态段
//------------------------------------------------------------------------------

// 64-bit TSS
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

static PCPU_BSS tss_t g_tss;

INIT_TEXT void tss_init_load() {
    tss_t *this_tss = this_ptr(&g_tss);
    memset(this_tss, 0, sizeof(tss_t));

    uint64_t tss_size = sizeof(tss_t);
    uint64_t tss_addr = (uint64_t)this_tss;

    uint64_t lower = 0UL;
    uint64_t upper = 0UL;
    lower |=  tss_size        & 0x000000000000ffffUL;   // limit [15:0]
    lower |= (tss_addr << 16) & 0x000000ffffff0000UL;   // base  [23:0]
    lower |= (tss_size << 32) & 0x000f000000000000UL;   // limit [19:16]
    lower |= (tss_addr << 32) & 0xff00000000000000UL;   // base  [31:24]
    lower |=                    0x0000e90000000000UL;   // P=1, DPL=3, Type=9 (64-bit TSS)
    upper  = (tss_addr >> 32) & 0x00000000ffffffffUL;   // base  [63:32]

    uint64_t *this_gdt = this_ptr(g_gdt);
    this_gdt[6] = lower;
    this_gdt[7] = upper;
    load_tr(6 << 3);
}

// 设置当前 CPU 的中断栈地址，跨特权级中断时生效
void tss_set_rsp0(uint64_t rsp0) {
    uint32_t lower =  rsp0        & 0xffffffff;
    uint32_t upper = (rsp0 >> 32) & 0xffffffff;
    __asm__("movl %1, %%gs:%0" : "=m"(g_tss.rsp0_lower) : "r"(lower));
    __asm__("movl %1, %%gs:%0" : "=m"(g_tss.rsp0_upper) : "r"(upper));
}

//------------------------------------------------------------------------------
// 中断描述符表
//------------------------------------------------------------------------------

static struct idte {
    uint16_t offset_low;
    uint16_t selector;
    uint16_t attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
} PACKED g_idt[256] = {0};

// 初始化 IDT，所有 CPU 共享
INIT_TEXT void idt_set_entry(int idx, uint64_t target, uint16_t attr) {
    ASSERT((0 <= idx) && (idx < 256));

    g_idt[idx].selector = 8;
    g_idt[idx].offset_high = (target >> 32) & 0xffffffff;
    g_idt[idx].offset_mid  = (target >> 16) & 0xffff;
    g_idt[idx].offset_low  =  target        & 0xffff;
    g_idt[idx].attr        = attr;
}

// 定义在 arch_entries.S
extern INIT_DATA uint64_t isr_entries[256];
void isr_dummy();

INIT_TEXT void idt_init() {
    memset(g_idt, 0, sizeof(g_idt));

    // 设置 IDT 条目，全是中断门，触发后自动禁用中断
    // P=1, DPL=0, Type=E (64-bit interrupt gate)
    for (int i = 0; i < 256; ++i) {
        idt_set_entry(i, isr_entries[i], 0x8e00);
    }

    // 一些特殊的中断需要单独设置
    idt_set_entry(VEC_LOAPIC_SPURIOUS, (uint64_t)isr_dummy, 0x8e00);
}

INIT_TEXT void idt_load() {
    tbl_ptr_t idtr = {
        .base = (uint64_t)g_idt,
        .limit = sizeof(g_idt) - 1,
    };
    load_idtr(&idtr);
}
