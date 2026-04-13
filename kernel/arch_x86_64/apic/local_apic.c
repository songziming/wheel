#include "apic.h"
#include <arch_api.h>
#include <arch_int.h>
#include <cpu/features.h>
#include <debug.h>



CONST int    g_loapic_num;
CONST size_t g_loapic_addr;
CONST loapic_t *g_loapics;


//------------------------------------------------------------------------------
// local apic 寄存器编号
//------------------------------------------------------------------------------

enum loapic_reg {
    REG_ID          = 0x02, // local APIC id
    REG_VER         = 0x03, // local APIC version
    REG_TPR         = 0x08, // task priority
    REG_APR         = 0x09, // arbitration priority
    REG_PPR         = 0x0a, // processor priority
    REG_EOI         = 0x0b, // end of interrupt
    REG_RRD         = 0x0c, // remote read
    REG_LDR         = 0x0d, // logical destination
    REG_DFR         = 0x0e, // destination format
    REG_SVR         = 0x0f, // spurious interrupt vector
    REG_ISR         = 0x10, // 8 in-service regs, 0x10~0x17
    REG_TMR         = 0x18, // 8 trigger mode regs, 0x18~0x1f
    REG_IRR         = 0x20, // 8 interrupt request regs, 0x20~0x27
    REG_ESR         = 0x28, // error status
    REG_LVT_CMCI    = 0x2f, // LVT corrected machine check interrupt (CMCI)
    REG_ICR_LO      = 0x30, // interrupt command reg upper half
    REG_ICR_HI      = 0x31, // interrupt command reg lower half
    REG_LVT_TIMER   = 0x32, // LVT (timer)
    REG_LVT_THERMAL = 0x33, // LVT (thermal)
    REG_LVT_PMC     = 0x34, // LVT (performance monitoring counters)
    REG_LVT_LINT0   = 0x35, // LVT (LINT0)
    REG_LVT_LINT1   = 0x36, // LVT (LINT1)
    REG_LVT_ERROR   = 0x37, // LVT (error)
    REG_TIMER_ICR   = 0x38, // timer initial count
    REG_TIMER_CCR   = 0x39, // timer current count
    REG_TIMER_DIV   = 0x3e, // timer divide configuration

    REG_SELF_IPI    = 0x3f, // x2APIC only
};

// IA32_APIC_BASE msr
#define IA32_APIC_BASE      0x1b        // MSR index
#define LOAPIC_MSR_BASE     0xfffff000  // local APIC base addr mask
#define LOAPIC_MSR_EN       0x00000800  // local APIC global enable
#define LOAPIC_MSR_EXTD     0x00000400  // enable x2APIC mode
#define LOAPIC_MSR_BSP      0x00000100  // local APIC is bsp

// ICR bits
#define ICR_VECTOR_MASK     0x000000ff  // vector number mask
#define LOAPIC_DM_FIXED     0x00000000  // delivery mode: fixed
#define LOAPIC_DM_LOWEST    0x00000100  // delivery mode: lowest
#define LOAPIC_DM_SMI       0x00000200  // delivery mode: SMI
#define LOAPIC_DM_NMI       0x00000400  // delivery mode: NMI
#define LOAPIC_DM_INIT      0x00000500  // delivery mode: INIT
#define LOAPIC_DM_STARTUP   0x00000600  // delivery mode: startup
#define LOAPIC_DM_EXTINT    0x00000700  // delivery mode: ExtINT
#define LOAPIC_LOGICAL      0x00000800  // destination mode: logical
#define LOAPIC_IDLE         0x00000000  // delivery status: idle
#define LOAPIC_PENDING      0x00001000  // delivery status: pend
#define LOAPIC_HIGH         0x00000000  // polarity: High
#define LOAPIC_LOW          0x00002000  // polarity: Low
#define LOAPIC_REMOTE       0x00004000  // remote IRR
#define LOAPIC_DEASSERT     0x00000000  // level: de-assert
#define LOAPIC_ASSERT       0x00004000  // level: assert
#define LOAPIC_EDGE         0x00000000  // trigger mode: Edge
#define LOAPIC_LEVEL        0x00008000  // trigger mode: Level
#define LOAPIC_INT_MASK     0x00010000  // interrupt disabled mask

// local APIC spurious-interrupt reg bits
#define LOAPIC_SVR_ENABLE   0x00000100  // APIC enabled

// local APIC timer reg only bits
#define LOAPIC_ONESHOT      0x00000000  // timer mode: one-shot
#define LOAPIC_PERIODIC     0x00020000  // timer mode: periodic
#define LOAPIC_DEADLINE     0x00040000  // timer mode: tsc-deadline


//------------------------------------------------------------------------------
// 寄存器读写函数
//------------------------------------------------------------------------------

// xAPIC 使用内存映射读写，每个寄存器地址按 16 字节对齐
// 读写映射寄存器时，要注意内存屏障。编译器和 CPU 都可能重排序
// volatile 可以防止编译器优化
// xAPIC 寄存器映射在 un-cached 区域，读写这些寄存器不会乱序

static uint32_t x_read(uint32_t reg) {
    ASSERT(REG_SELF_IPI != reg);
    size_t map = DIRECT_MAP_ADDR + g_loapic_addr + (reg << 4);
    return *(volatile uint32_t*)map;
}

static void x_write(uint32_t reg, uint32_t val) {
    ASSERT(REG_SELF_IPI != reg);
    size_t map = DIRECT_MAP_ADDR + g_loapic_addr + ((size_t)reg << 4);
    *(volatile uint32_t*)map = val;
}

// xAPIC 模式的目标 ID 只有 8-bit
static void x_write_icr(uint32_t id, uint32_t lo) {
    id <<= 24;
    x_write(REG_ICR_HI, id);
    x_write(REG_ICR_LO, lo);
}

// x2APIC 使用 MSR 读写寄存器，比内存映射更快
// 需要防止编译器对代码重排序，防止CPU对指令重排序

static uint32_t x2_read(uint32_t reg) {
    ASSERT(REG_DFR != reg);
    return (uint32_t)(read_msr(0x800 + reg) & 0xffffffff);
}

static void x2_write(uint32_t reg, uint32_t val) {
    ASSERT(REG_DFR != reg);
    cpu_rwfence();
    write_msr(0x800 + reg, val);
}

static void x2_write_icr(uint32_t id, uint32_t lo) {
    uint64_t val = (uint64_t)id << 32 | lo;
    cpu_rwfence();
    write_msr(0x800 + REG_ICR_LO, val);
}

static CONST uint32_t (*g_read)     (uint32_t)          = x_read;
static CONST void     (*g_write)    (uint32_t,uint32_t) = x_write;
static CONST void     (*g_write_icr)(uint32_t,uint32_t) = x_write_icr;

static INIT_TEXT void loapic_enable_x2() {
    g_read      = x2_read;
    g_write     = x2_write;
    g_write_icr = x2_write_icr;
}


//------------------------------------------------------------------------------
// 中断处理函数
//------------------------------------------------------------------------------

static void on_resched(int vec UNUSED, regs_t *f UNUSED) {
    g_write(REG_EOI, 0);
    // 无需任何动作，中断返回过程自然会切换任务
}

static void on_stopall(int vec UNUSED, regs_t *f UNUSED) {
    g_write(REG_EOI, 0);
    panic("cpu-%d stopped\n", cpu_index());
}

static void on_timer(int vec UNUSED, regs_t *f UNUSED) {
    g_write(REG_EOI, 0);
    // TODO 调用 tick_advance
}

static void on_error(int vec UNUSED, regs_t *f UNUSED) {
    panic("loapic error\n");
}

static void on_thermal(int vec UNUSED, regs_t *f UNUSED) {
    g_write(REG_EOI, 0);
}

static void on_spurious(int vec UNUSED, regs_t *f UNUSED) {}


//------------------------------------------------------------------------------
// 初始化
//------------------------------------------------------------------------------

INIT_TEXT void loapic_parse(loapic_t *dst, const madt_loapic_t *tbl) {
    dst->apic_id      = tbl->id;
    dst->processor_id = tbl->processor_id;
    dst->flags        = tbl->loapic_flags;
}

INIT_TEXT void loapic_parse_x2(loapic_t *dst, const madt_lox2apic_t *tbl) {
    dst->apic_id      = tbl->id;
    dst->processor_id = tbl->processor_id;
    dst->flags        = tbl->loapic_flags;
}

INIT_TEXT void loapic_init(int idx) {
    loapic_t *lo = &g_loapics[idx];
    if (0 == idx) {
        if (g_cpu_features & CPU_FEATURE_X2APIC) {
            // 支持 x2APIC，使用 MSR 读写 local APIC 寄存器
            loapic_enable_x2();
        }

        irq_handlers[VEC_IPI_RESCHED] = on_resched;
        irq_handlers[VEC_IPI_STOPALL] = on_stopall;
        irq_handlers[VEC_LOAPIC_TIMER] = on_timer;
        irq_handlers[VEC_LOAPIC_ERROR] = on_error;
        irq_handlers[VEC_LOAPIC_THERMAL] = on_thermal;
        irq_handlers[VEC_LOAPIC_SPURIOUS] = on_spurious;
    }

    // 开启 local APIC，进入 xAPIC 模式
    uint64_t msr_base = read_msr(IA32_APIC_BASE);
    if ((msr_base & LOAPIC_MSR_BASE) != g_loapic_addr) {
        logk("warning: Local APIC base different!\n");
        msr_base &= ~LOAPIC_MSR_BASE;
        msr_base |= g_loapic_addr & LOAPIC_MSR_BASE;
    }
    if (0 == idx) {
        msr_base |= LOAPIC_MSR_BSP;
    }
    msr_base |= LOAPIC_MSR_EN;
    write_msr(IA32_APIC_BASE, msr_base);

    // 如果 CPU 支持，则启用 x2APIC（必须 enable 之后再启用 x2APIC，不能一步完成）
    // bochs bug，base 寄存器写两次，LDR 才能生效
    // https://github.com/bochs-emu/Bochs/pull/250
    if (g_cpu_features & CPU_FEATURE_X2APIC) {
        msr_base |= LOAPIC_MSR_EXTD;
        write_msr(IA32_APIC_BASE, msr_base);
        write_msr(IA32_APIC_BASE, msr_base);
    }

    // 屏蔽中断向量号 0~31
    g_write(REG_TPR, 16);

    // 设置 LINT0、LINT1，参考 Intel MultiProcessor Spec 第 5.1 节
    // LINT0 连接到 8259A，但连接到 8259A 的设备也连接到 IO APIC，可以不设置
    // LINT1 连接到 NMI，我们只需要 BSP 能够处理 NMI
    if (0 == idx) {
        g_write(REG_LVT_LINT1, LOAPIC_LEVEL | LOAPIC_DM_NMI);
    }

    // 设置 LVT 向量号
    g_write(REG_LVT_ERROR, VEC_LOAPIC_ERROR);
    g_write(REG_LVT_THERMAL, VEC_LOAPIC_THERMAL);

    // 设置 spurious interrupt，开启这个 Local APIC
    g_write(REG_SVR, LOAPIC_SVR_ENABLE | VEC_LOAPIC_SPURIOUS);

    // 丢弃已有中断
    g_write(REG_EOI, 0);
}

void loapic_show() {
    logk("local APICs:\n");
    for (int i = 0; i < g_loapic_num; ++i) {
        logk("  - apic-id: %u, processor-id: %u\n",
            g_loapics[i].apic_id, g_loapics[i].processor_id);
    }
}




//------------------------------------------------------------------------------
// public functions
//------------------------------------------------------------------------------

// 向目标处理器发送 INIT-IPI
INIT_TEXT void loapic_send_init(int cpu) {
    ASSERT(cpu >= 0);
    ASSERT(cpu < cpu_count());

    uint32_t lo = LOAPIC_DM_INIT | LOAPIC_EDGE | LOAPIC_ASSERT;
    g_write_icr(g_loapics[cpu].apic_id, lo);
}

// 向目标处理器发送 startup-IPI
INIT_TEXT void loapic_send_sipi(int cpu, int vec) {
    ASSERT(cpu >= 0);
    ASSERT(cpu < cpu_count());
    ASSERT((vec >= 0) && (vec < 256));
    ASSERT((vec < 0xa0) || (vec > 0xbf)); // 向量号 a0~bf 非法

    uint32_t lo = (vec & ICR_VECTOR_MASK) | LOAPIC_DM_STARTUP | LOAPIC_EDGE | LOAPIC_ASSERT;
    g_write_icr(g_loapics[cpu].apic_id, lo);
}

// 发送 IPI
void loapic_send_ipi(int cpu, int vec) {
    ASSERT(cpu < cpu_count());
    ASSERT((vec >= 0) && (vec < 256));

    uint32_t lo = (vec & ICR_VECTOR_MASK) | LOAPIC_DM_FIXED | LOAPIC_EDGE | LOAPIC_DEASSERT;
    if (cpu < 0) {
        g_write_icr(0xffffffffU, lo); // 广播
    } else {
        g_write_icr(g_loapics[cpu].apic_id, lo);
    }
}
