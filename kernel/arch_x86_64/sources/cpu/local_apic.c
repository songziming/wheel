#include <wheel.h>
#include <arch_smp.h>

// 操作 Local APIC 有两种方式：
//  - MMIO，所有寄存器都位于 16-byte 对齐的位置
//  - MSR，速度更快，编号连续，x2APIC 引入
// 我们同时支持两种方式，根据检测结果选择其中一套读写函数

//------------------------------------------------------------------------------
// 寄存器定义
//------------------------------------------------------------------------------

// 如果是 xAPIC，需要放大 16 倍，加上 mmio 基地址，就是内存中的映射地址
// 如果是 x2APIC，则直接加上 0x800，就是 MSR 地址

// 特殊情况：
// REG_ICR 是64位寄存器，xAPIC 对应两个32-bit寄存器，x2APIC 只需要一个 msr
// REG_DFR 不能在 x2APIC 模式下使用
// REG_SELF_IPI 只能在 x2APIC 模式下使用


typedef enum loapic_reg {
    REG_ID          = 0x002,    // local APIC id
    REG_VER         = 0x003,    // local APIC version
    REG_TPR         = 0x008,    // task priority
    REG_APR         = 0x009,    // arbitration priority
    REG_PPR         = 0x00a,    // processor priority
    REG_EOI         = 0x00b,    // end of interrupt
    REG_RRD         = 0x00c,    // remote read
    REG_LDR         = 0x00d,    // logical destination
    REG_DFR         = 0x00e,    // destination format
    REG_SVR         = 0x00f,    // spurious interrupt
    REG_ISR         = 0x010,    // 8 in-service regs, 0x0100~0x0170
    REG_TMR         = 0x018,    // 8 trigger mode regs, 0x0180~0x01f0
    REG_IRR         = 0x020,    // 8 interrupt request regs, 0x0200~0x0270
    REG_ESR         = 0x028,    // error status
    REG_LVT_CMCI    = 0x02f,    // LVT (CMCI)
    REG_ICR_LO      = 0x030,    // int-command-reg upper half
    REG_ICR_HI      = 0x031,    // int-command-reg lower half
    REG_LVT_TIMER   = 0x032,    // LVT (timer)
    REG_LVT_THERMAL = 0x033,    // LVT (thermal)
    REG_LVT_PMC     = 0x034,    // LVT (performance counter)
    REG_LVT_LINT0   = 0x035,    // LVT (LINT0)
    REG_LVT_LINT1   = 0x036,    // LVT (LINT1)
    REG_LVT_ERROR   = 0x037,    // LVT (error)
    REG_TIMER_ICR   = 0x038,    // timer initial count
    REG_TIMER_CCR   = 0x039,    // timer current count
    REG_TIMER_DIV   = 0x03e,    // timer divide config
    REG_SELF_IPI    = 0x03f,
} loapic_reg_t;

// IA32_APIC_BASE msr
#define IA32_APIC_BASE      0x1b        // MSR index
#define LOAPIC_MSR_BASE     0xfffff000  // local APIC base addr mask
#define LOAPIC_MSR_EN       0x00000800  // local APIC global enable
#define LOAPIC_MSR_EXTD     0x00000400  // enable x2APIC mode
#define LOAPIC_MSR_BSP      0x00000100  // local APIC is bsp

// local APIC vector table bits
#define LOAPIC_VECTOR       0x000000ff  // vector number mask
#define LOAPIC_MODE         0x00000700  // delivery mode mask
#define LOAPIC_FIXED        0x00000000  // delivery mode: fixed
#define LOAPIC_LOWEST       0x00000100  // delivery mode: lowest
#define LOAPIC_SMI          0x00000200  // delivery mode: SMI
#define LOAPIC_NMI          0x00000400  // delivery mode: NMI
#define LOAPIC_INIT         0x00000500  // delivery mode: INIT
#define LOAPIC_STARTUP      0x00000600  // delivery mode: startup
#define LOAPIC_EXT          0x00000700  // delivery mode: ExtINT
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
// xAPIC 读写函数
//------------------------------------------------------------------------------

static uint32_t x_read(loapic_reg_t reg) {
    ASSERT(REG_SELF_IPI != reg);
    size_t map = DIRECT_MAP_ADDR + g_loapic_addr + ((size_t)reg << 4);
    return *(volatile uint32_t *)map;
}

static void x_write(loapic_reg_t reg, uint32_t val) {
    ASSERT(REG_SELF_IPI != reg);
    size_t map = DIRECT_MAP_ADDR + g_loapic_addr + ((size_t)reg << 4);
    *(volatile uint32_t *)map = val;
}

static uint64_t x_read_icr() {
    uint32_t lo = x_read(REG_ICR_LO);
    uint32_t hi = x_read(REG_ICR_HI);
    return ((uint64_t)hi << 32) | lo;
}

static void x_write_icr(uint64_t val) {
    x_write(REG_ICR_HI, (val >> 32) & 0xffffffff);
    x_write(REG_ICR_LO, val & 0xffffffff);
}


//------------------------------------------------------------------------------
// x2APIC 读写函数
//------------------------------------------------------------------------------

static uint32_t x2_read(loapic_reg_t reg) {
    ASSERT(REG_DFR != reg);
    return (uint32_t)(read_msr(0x800 + reg) & 0xffffffff);
}

static void x2_write(loapic_reg_t reg, uint32_t val) {
    ASSERT(REG_DFR != reg);
    write_msr(0x800 + reg, val);
}

static uint64_t x2_read_icr() {
    return read_msr(0x800 + REG_ICR_LO);
}

static void x2_write_icr(uint64_t val) {
    write_msr(0x800 + REG_ICR_LO, val);
}


//------------------------------------------------------------------------------
// 用函数指针来统一 xAPIC、x2APIC 两种访问模式
//------------------------------------------------------------------------------

static uint32_t (*g_read)     (loapic_reg_t reg)               = x_read;
static void     (*g_write)    (loapic_reg_t reg, uint32_t val) = x_write;
static uint64_t (*g_read_icr) ()                               = x_read_icr;
static void     (*g_write_icr)(uint64_t val)                   = x_write_icr;

INIT_TEXT void loapic_init() {
    g_read = x2_read;
    g_write = x2_write;
    g_read_icr = x2_read_icr;
    g_write_icr = x2_write_icr;
}
