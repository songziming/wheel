#include <cpu/local_apic.h>

#include <wheel.h>
#include <arch_smp.h>
#include <cpu/rw.h>
#include <arch_cpu.h>
#include <arch_int.h>

#include <str.h>



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
    REG_ID          = 0x02, // local APIC id
    REG_VER         = 0x03, // local APIC version
    REG_TPR         = 0x08, // task priority
    REG_APR         = 0x09, // arbitration priority
    REG_PPR         = 0x0a, // processor priority
    REG_EOI         = 0x0b, // end of interrupt
    REG_RRD         = 0x0c, // remote read
    REG_LDR         = 0x0d, // logical destination
    REG_DFR         = 0x0e, // destination format
    REG_SVR         = 0x0f, // spurious interrupt
    REG_ISR         = 0x10, // 8 in-service regs, 0x10~0x17
    REG_TMR         = 0x18, // 8 trigger mode regs, 0x18~0x1f
    REG_IRR         = 0x20, // 8 interrupt request regs, 0x20~0x27
    REG_ESR         = 0x28, // error status
    REG_LVT_CMCI    = 0x2f, // LVT (CMCI)
    REG_ICR_LO      = 0x30, // int-command-reg upper half
    REG_ICR_HI      = 0x31, // int-command-reg lower half
    REG_LVT_TIMER   = 0x32, // LVT (timer)
    REG_LVT_THERMAL = 0x33, // LVT (thermal)
    REG_LVT_PMC     = 0x34, // LVT (performance counter)
    REG_LVT_LINT0   = 0x35, // LVT (LINT0)
    REG_LVT_LINT1   = 0x36, // LVT (LINT1)
    REG_LVT_ERROR   = 0x37, // LVT (error)
    REG_TIMER_ICR   = 0x38, // timer initial count
    REG_TIMER_CCR   = 0x39, // timer current count
    REG_TIMER_DIV   = 0x3e, // timer divide config
    REG_SELF_IPI    = 0x3f,
} loapic_reg_t;

// IA32_APIC_BASE msr
#define IA32_APIC_BASE      0x1b        // MSR index
#define LOAPIC_MSR_BASE     0xfffff000  // local APIC base addr mask
#define LOAPIC_MSR_EN       0x00000800  // local APIC global enable
#define LOAPIC_MSR_EXTD     0x00000400  // enable x2APIC mode
#define LOAPIC_MSR_BSP      0x00000100  // local APIC is bsp

// local APIC vector table bits
#define LOAPIC_VECTOR       0x000000ff  // vector number mask
#define LOAPIC_DM_MASK      0x00000700  // delivery mode mask
#define LOAPIC_DM_FIXED     0x00000000  // delivery mode: fixed
#define LOAPIC_DM_LOWEST    0x00000100  // delivery mode: lowest
#define LOAPIC_DM_SMI       0x00000200  // delivery mode: SMI
#define LOAPIC_DM_NMI       0x00000400  // delivery mode: NMI
#define LOAPIC_DM_INIT      0x00000500  // delivery mode: INIT
#define LOAPIC_DM_STARTUP   0x00000600  // delivery mode: startup
#define LOAPIC_DM_EXTINT    0x00000700  // delivery mode: ExtINT
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
// xAPIC、x2APIC 的寄存器读写函数
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

// 虚函数接口
static uint32_t (*g_read)     (loapic_reg_t reg)               = x_read;
static void     (*g_write)    (loapic_reg_t reg, uint32_t val) = x_write;
static uint64_t (*g_read_icr) ()                               = x_read_icr;
static void     (*g_write_icr)(uint64_t val)                   = x_write_icr;


//------------------------------------------------------------------------------
// 使用 8254 校准 APIC Timer
//------------------------------------------------------------------------------

// channel 2 可以通过软件设置输入，可以通过软件读取输出
// 写端口 0x61 bit[0] 控制输入，读端口 0x61 bit[5] 获取输出
#define PIT_CH2 0x42
#define PIT_CMD 0x43

// 等待 50ms，计算一秒钟对应多少周期
// 同时还计算了 tsc 速度（tsc 可能睿频，导致速度不准）
static INIT_TEXT uint32_t calibrate_using_pit_03() {
    uint32_t start_count = 0;
    uint32_t end_count   = 0;

    uint64_t start_tsc = 0;
    uint64_t end_tsc = 0;

    // 首先确保 channel 2 处于禁用状态，输入低电平
    out8(0x61, in8(0x61) & ~1);

    // 将 apic timer 计数器设为最大值，divider=1
    g_write(REG_TIMER_DIV, 0x0b);
    g_write(REG_TIMER_ICR, 0xffffffff);

    // 使用 channel 2 mode 3，reload value 设为 65534
    // mode 3 表示输出方波，每个下降沿计数器减二（所以 reload value 为偶数）
    out8(PIT_CMD, 0xb6); // 10_11_011_0
    out8(PIT_CH2, 0xfe);
    out8(PIT_CH2, 0xff);

    // channel 2 输入信号设为高电平，从 65534 开始计数
    // 读取 apic timer 计数器，作为开始值
    out8(0x61, in8(0x61) | 1);
    start_count = g_read(REG_TIMER_CCR);
    start_tsc = read_tsc();

    // 不断读取输出（使用 read-back 模式锁住 status，最高比特表示输出）
    // 一旦输出变为 0 则退出循环，表示已经过了 32767 个周期（不足 50ms）
    while (1) {
        out8(PIT_CMD, 0xe8); // 11_10_100_0
        if ((in8(PIT_CH2) & 0x80) != 0x80) {
            break;
        }
    }

    // 不断读取 channel 2 计数器，以及 apic timer 计数器，足够 50ms 则退出循环
    // 50ms 即 1/20 秒，mode 3 每周期计数器减二，则 50ms 计数器减少 119318
    while (1) {
        out8(PIT_CMD, 0x80); // latch channel 2 count
        end_count = g_read(REG_TIMER_CCR);
        end_tsc = read_tsc();
        uint8_t lo = in8(PIT_CH2);
        uint8_t hi = in8(PIT_CH2);
        int pit = ((int)hi << 8) | lo;
        if (pit <= 2 * 65534 - 119318) {
            break;
        }
    }

    int64_t g_tsc_freq = (end_tsc - start_tsc) * 20;
    klog("tsc freq = %ld\n", g_tsc_freq);

    // 禁用 PIT channel 2，返回 APIC Timer 频率
    out8(0x61, in8(0x61) & ~1);
    return (start_count - end_count) * 20;  // 1s = 20 * 50ms
}


//------------------------------------------------------------------------------
// 中断处理函数
//------------------------------------------------------------------------------

// 这类中断一般不发生，无需发送 EOI
static void handle_spurious(int vec, arch_regs_t *f) {
    (void)vec;
    (void)f;
    klog("this cannot happen!\n");
}

static void handle_timer(int vec, arch_regs_t *f) {
    (void)vec;
    (void)f;
    g_write(REG_EOI, 0);
}

#if 0
// corrected machine check error
static void handle_cmci(int vec, arch_regs_t *f) {
    (void)vec;
    (void)f;
}

// 核心温度超过危险值时触发该中断，温度再高就会关闭核心
static void handle_thermal_monitor(int vec, arch_regs_t *f) {
    (void)vec;
    (void)f;
}

static void handle_performance_counter(int vec, arch_regs_t *f) {
    (void)vec;
    (void)f;
}
#endif

// APIC 发生错误
static void handle_error(int vec, arch_regs_t *f) {
    (void)vec;
    (void)f;
    klog("fatal: Local APIC internal error!\n");
}




//------------------------------------------------------------------------------
// 初始化
//------------------------------------------------------------------------------

// 仅在 BSP 运行，包括：
//  - 选择合适的寄存器读写函数
//  - 设置中断处理函数
//  - 校准 local apic timer
INIT_TEXT void local_apic_init_bsp() {
    uint64_t msr_base = read_msr(IA32_APIC_BASE);

    if (0 == (LOAPIC_MSR_BSP & msr_base)) {
        klog("warning: this is not loapic for BSP\n");
    }

    // 如果 MSR 记录的值和 MADT 规定的映射地址不一致，则重新映射
    if (g_loapic_addr != (LOAPIC_MSR_BASE & msr_base)) {
        msr_base &= ~LOAPIC_MSR_BASE;
        msr_base |= g_loapic_addr & LOAPIC_MSR_BASE;
    }

    // 如果支持 x2APIC，则启用
    if (CPU_FEATURE_X2APIC & g_cpu_features) {
        g_read = x2_read;
        g_write = x2_write;
        g_read_icr = x2_read_icr;
        g_write_icr = x2_write_icr;
        msr_base |= LOAPIC_MSR_EXTD;
    } else {
        // TODO 将映射的内存，标记为不可缓存，通过页表属性位或 mtrr 实现
    }

    // 开启 loapic（尚未真的启用，还要设置 spurious reg）
    msr_base |= LOAPIC_MSR_BSP;
    msr_base |= LOAPIC_MSR_EN;
    write_msr(IA32_APIC_BASE, msr_base);

    // 注册中断处理函数
    set_int_handler(VEC_LOAPIC_TIMER, handle_timer);
    set_int_handler(VEC_LOAPIC_ERROR, handle_error);
    set_int_handler(VEC_LOAPIC_SPURIOUS, handle_spurious);

    // 设置 DFR、LDR、TPR
    if (0 == (CPU_FEATURE_X2APIC & g_cpu_features)) {
        g_write(REG_DFR, 0xffffffff);
    }
    g_write(REG_TPR, 16);   // 屏蔽中断号 0~31
    g_write(REG_TIMER_ICR, 0);
    g_write(REG_TIMER_DIV, 0);

    // 设置 LINT0、LINT1，参考 Intel MultiProcessor Spec 第 5.1 节
    // LINT0 连接到 8259A，但连接到 8259A 的设备也连接到 IO APIC，可以不设置
    // LINT1 连接到 NMI，我们只需要 BSP 能够处理 NMI
    g_write(REG_LVT_LINT0, LOAPIC_INT_MASK); // LINT0 屏蔽
    g_write(REG_LVT_LINT1, LOAPIC_LEVEL | LOAPIC_DM_NMI);

    // 填写 LVT，设置中断处理方式
    g_write(REG_LVT_TIMER, LOAPIC_PERIODIC | LOAPIC_DM_FIXED | VEC_LOAPIC_TIMER);
    g_write(REG_LVT_ERROR, VEC_LOAPIC_ERROR);

    // // 屏蔽其他的中断
    // g_write(REG_LVT_CMCI,    LOAPIC_INT_MASK);
    // g_write(REG_LVT_THERMAL, LOAPIC_INT_MASK);
    // g_write(REG_LVT_PMC,     LOAPIC_INT_MASK);
    // g_write(REG_LVT_ERROR,   LOAPIC_INT_MASK);

    // 设置 spurious interrupt，开启这个 Local APIC
    g_write(REG_SVR, LOAPIC_SVR_ENABLE | VEC_LOAPIC_SPURIOUS);

    // 将已有中断丢弃
    // 更保险的方法是检查 IRR，里面有多少个 1 就发送多少次 EOI
    g_write(REG_EOI, 0);

    uint32_t timer_freq = calibrate_using_pit_03();
    klog("local apic timer freq %u\n", timer_freq);

    // 启动 Timer，周期性发送中断
    g_write(REG_TIMER_DIV, 0x0b); // divide by 1
    g_write(REG_TIMER_ICR, timer_freq);
}
