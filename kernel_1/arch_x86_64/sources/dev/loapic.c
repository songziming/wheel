// Local (x2)APIC 驱动

#include <dev/loapic.h>
#include <debug.h>
#include <tick.h>

#include <arch_smp.h>
#include <arch_int.h>
#include <liba/cpuid.h>
#include <liba/rw.h>


//------------------------------------------------------------------------------
// Local APIC 寄存器定义
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
    size_t map = DIRECT_MAP_BASE + g_loapic_addr + ((size_t)reg << 4);
    return *(volatile uint32_t *)map;
}

static void x_write(loapic_reg_t reg, uint32_t val) {
    ASSERT(REG_SELF_IPI != reg);
    size_t map = DIRECT_MAP_BASE + g_loapic_addr + ((size_t)reg << 4);
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


// uint64_t g_tsc_freq = 0;

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
    out_byte(0x61, in_byte(0x61) & ~1);

    // 将 apic timer 计数器设为最大值，divider=1
    g_write(REG_TIMER_DIV, 0x0b);
    g_write(REG_TIMER_ICR, 0xffffffff);

    // 使用 channel 2 mode 3，reload value 设为 65534
    // mode 3 表示输出方波，每个下降沿计数器减二（所以 reload value 为偶数）
    out_byte(PIT_CMD, 0xb6); // 10_11_011_0
    out_byte(PIT_CH2, 0xfe);
    out_byte(PIT_CH2, 0xff);

    // channel 2 输入信号设为高电平，从 65534 开始计数
    // 读取 apic timer 计数器，作为开始值
    out_byte(0x61, in_byte(0x61) | 1);
    start_count = g_read(REG_TIMER_CCR);
    start_tsc = read_tsc();

    // 不断读取输出（使用 read-back 模式锁住 status，最高比特表示输出）
    // 一旦输出变为 0 则退出循环，表示已经过了 32767 个周期（不足 50ms）
    while (1) {
        out_byte(PIT_CMD, 0xe8); // 11_10_100_0
        if ((in_byte(PIT_CH2) & 0x80) != 0x80) {
            break;
        }
    }

    // 不断读取 channel 2 计数器，以及 apic timer 计数器，足够 50ms 则退出循环
    // 50ms 即 1/20 秒，mode 3 每周期计数器减二，则 50ms 计数器减少 119318
    while (1) {
        out_byte(PIT_CMD, 0x80); // latch channel 2 count
        end_count = g_read(REG_TIMER_CCR);
        end_tsc = read_tsc();
        uint8_t lo = in_byte(PIT_CH2);
        uint8_t hi = in_byte(PIT_CH2);
        int pit = ((int)hi << 8) | lo;
        if (pit <= 2 * 65534 - 119318) {
            break;
        }
    }

    if (0 == g_tsc_freq) {
        g_tsc_freq = (end_tsc - start_tsc) * 20;
    }

    // 禁用 PIT channel 2，返回 APIC Timer 频率
    out_byte(0x61, in_byte(0x61) & ~1);
    return (start_count - end_count) * 20;  // 1s = 20 * 50ms
}


//------------------------------------------------------------------------------
// 中断处理函数
//------------------------------------------------------------------------------

// Local APIC Timer 中断的处理函数
static void handle_timer() {
    g_write(REG_EOI, 0);
    tick_advance();
    // TODO 如果是 one-shot mode，说明采用可变时间片调度
    //      需要在这里重新写入 ICR，重新开始计时
}


//------------------------------------------------------------------------------
// 初始化设备
//------------------------------------------------------------------------------

static uint32_t g_timer_freq = 0;

// 每个 cpu 都要设置自己的 local apic
INIT_TEXT void loapic_init() {
    uint64_t msr_base = read_msr(IA32_APIC_BASE);

    // 如果和 MADT 信息不一致，使用 MADT 提供的值
    if (g_loapic_addr != (msr_base & LOAPIC_MSR_BASE)) {
        msr_base &= ~LOAPIC_MSR_BASE;
        msr_base |= g_loapic_addr & LOAPIC_MSR_BASE;
        write_msr(IA32_APIC_BASE, msr_base);
    }

    // 如果支持 x2APIC，切换到新模式
    if (CPU_FEATURE_X2APIC & g_cpu_features) {
        msr_base |= LOAPIC_MSR_EXTD;
        write_msr(IA32_APIC_BASE, msr_base);

        if (LOAPIC_MSR_BSP & msr_base) {
            g_read      = x2_read;
            g_write     = x2_write;
            g_read_icr  = x2_read_icr;
            g_write_icr = x2_write_icr;
        }
    }

    // 注册中断处理函数
    if (LOAPIC_MSR_BSP & msr_base) {
        // 其中 spurious interrupt 不需要注册处理函数
        // 其 IDT 条目对应的是 isr_dummy，参考 liba/cpu.c
        g_handlers[VEC_LOAPIC_TIMER] = handle_timer;
    }

#if 0
    // 读取 local apic version，检测相关功能是否可用
    uint32_t ver = g_read(REG_VER);
    if ((ver & 0xff) < 16) {
        dbg_print("82489DX discrete APIC (%d)\n", ver);
    } else {
        dbg_print("integrated Local APIC\n");
    }
    int lvt_count = ((ver >> 16) & 0xff) + 1; // 获取 LVT 的数量
    int support_eoi_sup = (ver & (1 << 24)) ? 1 : 0; // 是否支持 EOI 屏蔽
#endif

    // 接下来开始初始化 Local APIC
    // 首先将一些寄存器清零
    if (0 == (CPU_FEATURE_X2APIC & g_cpu_features)) {
        g_write(REG_DFR, 0xffffffff);
    }
    g_write(REG_TPR, 0);
    g_write(REG_TIMER_ICR, 0);
    g_write(REG_TIMER_DIV, 0);

    // 屏蔽所有中断
    g_write(REG_LVT_CMCI,    LOAPIC_INT_MASK);
    g_write(REG_LVT_TIMER,   LOAPIC_INT_MASK);
    g_write(REG_LVT_THERMAL, LOAPIC_INT_MASK);
    g_write(REG_LVT_PMC,     LOAPIC_INT_MASK);
    g_write(REG_LVT_LINT0,   LOAPIC_INT_MASK);
    g_write(REG_LVT_LINT1,   LOAPIC_INT_MASK);
    g_write(REG_LVT_ERROR,   LOAPIC_INT_MASK);

    // 启用 local apic
    g_write(REG_SVR, LOAPIC_SVR_ENABLE | VEC_LOAPIC_SPURIOUS);

    // 将已有中断丢弃
    g_write(REG_EOI, 0);

    if (0 == g_timer_freq) {
        g_timer_freq = calibrate_using_pit_03();
    }

    // 启动 Timer，周期性发送中断
    g_write(REG_LVT_TIMER, LOAPIC_PERIODIC | VEC_LOAPIC_TIMER);
    g_write(REG_TIMER_DIV, 0x0b); // divide by 1
    g_write(REG_TIMER_ICR, g_timer_freq / SYS_CLOCK_FREQ);
}


void loapic_set_freq(int freq) {
    g_write(REG_TIMER_ICR, g_timer_freq / freq);
}


#define INIT_FLAGS  (LOAPIC_INIT | LOAPIC_EDGE | LOAPIC_ASSERT)
#define SIPI_FLAGS  (LOAPIC_STARTUP | LOAPIC_EDGE | LOAPIC_ASSERT)

// 发送 INIT-IPI，输入目标的 apic-id（0xffffffff 表示广播）
INIT_TEXT void loapic_emit_init(uint32_t id) {
    if (0 == (CPU_FEATURE_X2APIC & g_cpu_features)) {
        ASSERT(id < 256);
        id <<= 24;
    }
    g_write_icr(((uint64_t)id << 32) | INIT_FLAGS);
}

INIT_TEXT void loapic_emit_startup(uint32_t id, int vec) {
    ASSERT(vec >= 0);
    ASSERT(vec < 256);
    if (0 == (CPU_FEATURE_X2APIC & g_cpu_features)) {
        ASSERT(id < 256);
        id <<= 24;
    }
    g_write_icr(((uint64_t)id << 32) | vec | SIPI_FLAGS);
}


// 忙等待，轮询 APIC Timer 计时器，不需要开启中断
// 相对于 CPU cycle，timer 计数器变化很慢，两次 poll 可能得到相同结果
void loapic_busywait(uint64_t us) {
    uint64_t delay = (uint64_t)g_timer_freq * us / 1000000;
    uint32_t period = g_read(REG_TIMER_ICR);
    uint32_t start = g_read(REG_TIMER_CCR);

    // 等待完整周期
    while (delay > period) {
        while (g_read(REG_TIMER_CCR) <= start) {
            cpu_pause();
        }
        while (g_read(REG_TIMER_CCR) >= start) {
            cpu_pause();
        }
        delay -= period;
    }

    uint64_t end = start - delay;
    if (delay > start) {
        while (g_read(REG_TIMER_CCR) <= start) {
            cpu_pause();
        }
        end = start + period - delay;
    }

    while (g_read(REG_TIMER_CCR) >= end) {
        cpu_pause();
    }
}


// 忙等待，轮询时间戳，不需要中断就能等待
// TSC 频率和 CPU 动态主频相关，可能会变化
void tsc_busywait(uint64_t us) {
    uint64_t delay = g_tsc_freq / 1000000 * us;
    uint64_t start = read_tsc();
    uint64_t end = start + delay;

    // 如果发生了溢出，先等待 tsc 重置
    if (end < start) {
        while (read_tsc() >= start) {
            cpu_pause();
        }
    }

    while (read_tsc() < end) {
        cpu_pause();
    }
}
