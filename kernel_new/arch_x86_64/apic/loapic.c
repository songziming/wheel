#include "loapic.h"
#include <arch_intf.h>
#include <arch_impl.h>
#include <arch_int.h>
#include <generic/rw.h>
#include <generic/cpufeatures.h>
#include <memory/early_alloc.h>
#include <library/debug.h>
#include <proc/tick.h>


// APIC 的作用不仅是中断处理，还可以描述 CPU 拓扑（调度）、定时、多核
// 但是主要负责中断，因此放在中断模块下
// linux kernel 将 apic 作为一个独立子模块

// apic 的初始化却必须放在 smp 模块


// 寄存器读写接口
static CONST uint32_t (*g_read)     (uint8_t reg)               = NULL;
static CONST void     (*g_write)    (uint8_t reg, uint32_t val) = NULL;
static CONST void     (*g_write_icr)(uint32_t id, uint32_t lo)  = NULL;

static CONST size_t    g_loapic_addr;   // 虚拟地址
static CONST int       g_loapic_num = 0;
static CONST loapic_t *g_loapics = NULL;

static CONST uint32_t  g_max_apicid = 0;

static CONST uint64_t g_timer_freq = 0; // 一秒对应多少周期
static CONST uint64_t g_tsc_freq = 0;   // time stamp counter 频率


//------------------------------------------------------------------------------
// 寄存器定义
//------------------------------------------------------------------------------

// 如果是 xAPIC，需要放大 16 倍，加上 mmio 基地址，就是内存中的映射地址
// 如果是 x2APIC，则直接加上 0x800，就是 MSR 地址

// 特殊情况：
// REG_ICR 是64位寄存器，xAPIC 对应两个32-bit寄存器，x2APIC 只需要一个 msr
// REG_DFR 不能在 x2APIC 模式下使用
// REG_SELF_IPI 只能在 x2APIC 模式下使用

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
};

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
#define LOAPIC_LOGICAL      0x00000800  // logical destination
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
// 寄存器读写，分为 xAPIC、x2APIC 两种
//------------------------------------------------------------------------------

static uint32_t x_read(uint8_t reg) {
    ASSERT(REG_SELF_IPI != reg);
    size_t map = DIRECT_MAP_ADDR + g_loapic_addr + ((size_t)reg << 4);
    return *(volatile uint32_t *)map;
}

static void x_write(uint8_t reg, uint32_t val) {
    ASSERT(REG_SELF_IPI != reg);
    size_t map = DIRECT_MAP_ADDR + g_loapic_addr + ((size_t)reg << 4);
    *(volatile uint32_t *)map = val;
}

// xAPIC 模式的目标 ID 只有 8-bit
static void x_write_icr(uint32_t dst, uint32_t lo) {
    dst <<= 24;
    x_write(REG_ICR_HI, dst);
    x_write(REG_ICR_LO, lo);
}

// x2APIC 使用 MSR，而且会被 CPU 乱序执行
// 为了安全，写寄存器前加上 memory fence，确保内存读写有序
// 目的是读写 Local APIC 寄存器的顺序必须和代码中的顺序一致

// TODO 每个 wrmsr 之前都加 mfence 也许太严格了
//      只有 spurious 等几个关键寄存器的读写需要有序

static uint32_t x2_read(uint8_t reg) {
    ASSERT(REG_DFR != reg);
    return (uint32_t)(read_msr(0x800 + reg) & 0xffffffff);
}

static void x2_write(uint8_t reg, uint32_t val) {
    ASSERT(REG_DFR != reg);
    cpu_rwfence();
    write_msr(0x800 + reg, val);
}

static void x2_write_icr(uint32_t id, uint32_t lo) {
    uint64_t val = (uint64_t)id << 32 | lo;
    cpu_rwfence();
    write_msr(0x800 + REG_ICR_LO, val);
}


//------------------------------------------------------------------------------
// 中断响应函数
//------------------------------------------------------------------------------

static void on_spurious(int vec UNUSED, regs_t *f UNUSED) {
}

static void on_timer(int vec UNUSED, regs_t *f UNUSED) {
    g_write(REG_EOI, 0);
    tick_advance();
}

static void on_error(int vec UNUSED, regs_t *f UNUSED) {
    log("APIC error\n");
}


//------------------------------------------------------------------------------
// 初始化
//------------------------------------------------------------------------------

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

    if (g_max_apicid < tbl->id) {
        g_max_apicid = tbl->id;
    }
}

INIT_TEXT void loapic_parse_x2(int i, madt_lox2apic_t *tbl) {
    ASSERT(g_loapic_num > 0);
    ASSERT(i >= 0);
    ASSERT(i < g_loapic_num);

    g_loapics[i].apic_id      = tbl->id;
    g_loapics[i].processor_id = tbl->processor_id;
    g_loapics[i].flags        = tbl->loapic_flags;

    if (g_max_apicid < tbl->id) {
        g_max_apicid = tbl->id;
    }
}

// 检查 Local APIC ID，判断 IO APIC 仅使用 8-bit LDR 是否表示所有的 CPU
INIT_TEXT int need_int_remap() {
    if (0 == (CPU_FEATURE_X2APIC & g_cpu_features)) {
        return 0; // xAPIC 没有任何问题
    }

    if (g_max_apicid >= 255) {
        return 1;
    }

    for (int i = 0; i < g_loapic_num; ++i) {
        uint16_t cluster = g_loapics[i].apic_id >> 4;
        uint16_t logical = 1 << (g_loapics[i].apic_id & 15);
        if ((cluster >= 15) || (logical >= 16)) {
            return 1;
        }
    }

    return 0;
}

INIT_TEXT void loapic_init() {
    int idx = cpu_index();
    loapic_t *lo = &g_loapics[idx];

    // 如果是第一个 CPU，还要挑选合适的读写函数，注册中断处理函数
    if (0 == idx) {
        if (CPU_FEATURE_X2APIC & g_cpu_features) {
            g_read = x2_read;
            g_write = x2_write;
            g_write_icr = x2_write_icr;
        } else {
            g_read = x_read;
            g_write = x_write;
            g_write_icr = x_write_icr;
        }

        set_int_handler(VEC_LOAPIC_TIMER, on_timer);
        set_int_handler(VEC_LOAPIC_ERROR, on_error);
        set_int_handler(VEC_LOAPIC_SPURIOUS, on_spurious);
    }

    // 开启 local APIC，进入 xAPIC 模式
    uint64_t msr_base = read_msr(IA32_APIC_BASE);
    if ((msr_base & LOAPIC_MSR_BASE) != g_loapic_addr) {
        log("warning: Local APIC base different!\n");
        msr_base &= LOAPIC_MSR_BASE;
        msr_base |= g_loapic_addr & LOAPIC_MSR_BASE;
    }
    if (0 == idx) {
        msr_base |= LOAPIC_MSR_BSP;
    }
    msr_base |= LOAPIC_MSR_EN;
    write_msr(IA32_APIC_BASE, msr_base);

    // 如果支持，则启用 x2APIC
    // bochs bug，两次写入 base 寄存器，LDR 才能生效
    if (CPU_FEATURE_X2APIC & g_cpu_features) {
        msr_base |= LOAPIC_MSR_EXTD;
        write_msr(IA32_APIC_BASE, msr_base);
        write_msr(IA32_APIC_BASE, msr_base);
    }

    // 设置 DFR、LDR，根据 CPU 个数分类讨论
    if (CPU_FEATURE_X2APIC & g_cpu_features) {
        cpu_rwfence();
        uint32_t ldr = g_read(REG_LDR);
        lo->cluster_id = ldr >> 16;
        lo->logical_id = ldr & 0xffff;

        ASSERT(lo->cluster_id == (lo->apic_id >> 4));
        ASSERT(lo->logical_id == (1 << (lo->apic_id & 15)));
    } else if (g_loapic_num <= 8) {
        // 正好每个 CPU 对应一个比特
        lo->cluster_id = 0;
        lo->logical_id = 1 << cpu_index();
        g_write(REG_DFR, 0xffffffff); // flat model
        g_write(REG_LDR, lo->logical_id << 24);
    } else if (g_loapic_num <= 60) {
        // 必须分组，每个组最多 4 个 CPU
        lo->cluster_id = idx / 4;
        lo->logical_id = idx % 4;
        uint32_t ldr = (lo->cluster_id << 4) | lo->logical_id;
        g_write(REG_DFR, 0x0fffffff); // cluster model
        g_write(REG_LDR, ldr << 24);
    } else {
        // 还不够，只能让多个 CPU 使用相同的 Logical ID
        log("fatal: too much processors!\n");
        // emu_exit(1);
    }

    // 屏蔽中断号 0~31
    g_write(REG_TPR, 16);

    // 设置 LINT0、LINT1，参考 Intel MultiProcessor Spec 第 5.1 节
    // LINT0 连接到 8259A，但连接到 8259A 的设备也连接到 IO APIC，可以不设置
    // LINT1 连接到 NMI，我们只需要 BSP 能够处理 NMI

    if (0 == idx) {
        g_write(REG_LVT_LINT1, LOAPIC_LEVEL | LOAPIC_DM_NMI);
    }
    g_write(REG_LVT_ERROR, VEC_LOAPIC_ERROR);

    // 设置 spurious interrupt，开启这个 Local APIC
    g_write(REG_SVR, LOAPIC_SVR_ENABLE | VEC_LOAPIC_SPURIOUS);

    // 将已有中断丢弃
    g_write(REG_EOI, 0);
}


//------------------------------------------------------------------------------
// 公开函数
//------------------------------------------------------------------------------

// 实现 arch 接口函数
inline int cpu_count() {
    ASSERT(0 != g_loapic_num);
    return g_loapic_num;
}

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

    uint32_t lo = (vec & 0xff) | LOAPIC_DM_STARTUP | LOAPIC_EDGE | LOAPIC_ASSERT;
    g_write_icr(g_loapics[cpu].apic_id, lo);
}

void loapic_send_ipi(int cpu, int vec) {
    ASSERT(cpu < cpu_count());
    ASSERT((vec >= 0) && (vec < 256));

    uint32_t lo = (vec & 0xff) | LOAPIC_DM_FIXED | LOAPIC_EDGE | LOAPIC_DEASSERT;
    if (cpu < 0) {
        g_write_icr(0xffffffffU, lo); // 广播
    } else {
        g_write_icr(g_loapics[cpu].apic_id, lo);
    }
}

void loapic_timer_set_oneshot(int n) {
    g_write(REG_LVT_TIMER, LOAPIC_DM_FIXED | VEC_LOAPIC_TIMER | LOAPIC_ONESHOT);
    g_write(REG_TIMER_DIV, 0x0b); // divide by 1
    g_write(REG_TIMER_ICR, n);
}

void loapic_timer_set_periodic(int freq) {
    uint64_t delay = g_timer_freq + (freq >> 1);
    delay /= freq;

    g_write(REG_LVT_TIMER, LOAPIC_DM_FIXED | VEC_LOAPIC_TIMER | LOAPIC_PERIODIC);
    g_write(REG_TIMER_DIV, 0x0b); // divide by 1
    g_write(REG_TIMER_ICR, delay);
}

void loapic_timer_busywait(int us) {
    ASSERT(0 != g_timer_freq);

    uint32_t start  = g_read(REG_TIMER_CCR);
    uint32_t period = g_read(REG_TIMER_ICR);
    uint64_t delay  = (g_timer_freq * us + 500000) / 1000000;

    // 如果等待时间大于一个完整周期
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


//------------------------------------------------------------------------------
// 使用 8254 mode 3 校准 local apic timer
//------------------------------------------------------------------------------

// 8254 标准主频为 105/88 MHz，使用 8254 计时 50ms
// 统计这段时间前后 apic timer 计数器的取值，计算 timer 频率
// 同时还计算了 tsc 速度（tsc 可能睿频，导致速度不准）

// TODO 只跑一次可能不准确，可以使用软件锁相环（sw-PLL）循环多次，统计平均值

// channel 2 可以通过软件设置输入，可以通过软件读取输出
// 写端口 0x61 bit[0] 控制输入，读端口 0x61 bit[5] 获取输出
#define PIT_CH2 0x42
#define PIT_CMD 0x43

INIT_TEXT void calibrate_timer() {
    uint64_t start_ctr = 0;
    uint64_t end_ctr   = 0;

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
    start_ctr = g_read(REG_TIMER_CCR);
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
        end_ctr = g_read(REG_TIMER_CCR);
        end_tsc = read_tsc();
        uint8_t lo = in8(PIT_CH2);
        uint8_t hi = in8(PIT_CH2);
        int pit = ((int)hi << 8) | lo;
        if (pit <= 2 * 65534 - 119318) {
            break;
        }
    }

    // 禁用 PIT channel 2
    out8(0x61, in8(0x61) & ~1);

    // TSC 频率可以保存下来，也许有用
    g_timer_freq = (start_ctr - end_ctr) * 20;
    g_tsc_freq = (end_tsc - start_tsc) * 20;
}
