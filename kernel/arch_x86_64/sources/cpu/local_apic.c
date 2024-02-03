#include <cpu/local_apic.h>
#include <cpu/rw.h>
#include <cpu/info.h>

#include <arch_smp.h>
#include <arch_int.h>

#include <wheel.h>




// 操作 Local APIC 有两种方式：
//  - MMIO，所有寄存器都位于 16-byte 对齐的位置
//  - MSR，速度更快，编号连续，x2APIC 引入
// 我们同时支持两种方式，根据检测结果选择其中一套读写函数
typedef enum reg reg_t;
static CONST uint32_t (*g_read)     (reg_t reg)                = NULL;
static CONST void     (*g_write)    (reg_t reg, uint32_t val)  = NULL;
static CONST void     (*g_write_icr)(uint32_t id, uint32_t lo) = NULL;


// x2APIC LDR 最大取值，判断 8-bit destination 够不够用
// 进而用来判断是否需要开启 interrupt remapping
static CONST uint16_t g_max_cluster = 0;
static CONST uint16_t g_max_logical = 0;


//------------------------------------------------------------------------------
// 寄存器定义
//------------------------------------------------------------------------------

// 如果是 xAPIC，需要放大 16 倍，加上 mmio 基地址，就是内存中的映射地址
// 如果是 x2APIC，则直接加上 0x800，就是 MSR 地址

// 特殊情况：
// REG_ICR 是64位寄存器，xAPIC 对应两个32-bit寄存器，x2APIC 只需要一个 msr
// REG_DFR 不能在 x2APIC 模式下使用
// REG_SELF_IPI 只能在 x2APIC 模式下使用

enum reg {
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

static uint32_t x_read(reg_t reg) {
    ASSERT(REG_SELF_IPI != reg);
    size_t map = DIRECT_MAP_ADDR + g_loapic_addr + ((size_t)reg << 4);
    return *(volatile uint32_t *)map;
}

static void x_write(reg_t reg, uint32_t val) {
    ASSERT(REG_SELF_IPI != reg);
    size_t map = DIRECT_MAP_ADDR + g_loapic_addr + ((size_t)reg << 4);
    *(volatile uint32_t *)map = val;
}

// xAPIC 模式的目标 ID 只有 8-bit
static void x_write_icr(uint32_t dst, uint32_t lo) {
    ASSERT(dst < 0x100);
    dst <<= 24;
    x_write(REG_ICR_HI, dst);
    x_write(REG_ICR_LO, lo);
}

// x2APIC 使用 MSR，而且会被 CPU 乱序执行
// 为了安全，写寄存器前加上 memory fence，确保内存读写有序
// 目的是读写 Local APIC 寄存器的顺序必须和代码中的顺序一致

// TODO 每个 wrmsr 之前都加 mfence 也许太严格了
//      只有 spurious 等几个关键寄存器的读写需要有序

static uint32_t x2_read(reg_t reg) {
    ASSERT(REG_DFR != reg);
    return (uint32_t)(read_msr(0x800 + reg) & 0xffffffff);
}

static void x2_write(reg_t reg, uint32_t val) {
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
// 中断处理函数
//------------------------------------------------------------------------------

// 这类中断一般不发生，无需发送 EOI
static void handle_spurious(int vec, arch_regs_t *f) {
    (void)vec;
    (void)f;
    klog("this cannot happen!\n");
}

// core/tick.c
// void tick_advance();

static void handle_timer(int vec, arch_regs_t *f) {
    (void)vec;
    (void)f;
    local_apic_send_eoi();
    tick_advance();
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

INIT_TEXT void local_apic_init() {
    int ix = cpu_index();
    loapic_t *lo = &g_loapics[ix];

    // 开启 local APIC，进入 xAPIC 模式
    uint64_t msr_base = read_msr(IA32_APIC_BASE);
    if ((msr_base & LOAPIC_MSR_BASE) != g_loapic_addr) {
        klog("warning: Local APIC base different!\n");
        msr_base &= LOAPIC_MSR_BASE;
        msr_base |= g_loapic_addr & LOAPIC_MSR_BASE;
    }
    if (0 == ix) {
        msr_base |= LOAPIC_MSR_BSP;
    }
    msr_base |= LOAPIC_MSR_EN;
    write_msr(IA32_APIC_BASE, msr_base);

    // 如果支持 x2APIC，则启用（必须分成两步，先进入 xAPIC，再切到 x2APIC）
    if (CPU_FEATURE_X2APIC & g_cpu_features) {
        if (0 == ix) {
            g_read = x2_read;
            g_write = x2_write;
            g_write_icr = x2_write_icr;
        }

        // bochs bug，启用 x2APIC 后再次写入 base，LDR 才能生效
        msr_base |= LOAPIC_MSR_EXTD;
        write_msr(IA32_APIC_BASE, msr_base);
        write_msr(IA32_APIC_BASE, msr_base);
    } else {
        // TODO 将映射的内存，标记为不可缓存，通过页表属性位或 mtrr 实现
        if (0 == ix) {
            g_read = x_read;
            g_write = x_write;
            g_write_icr = x_write_icr;
        }
    }

    // 注册中断处理函数
    if (0 == ix) {
        set_int_handler(VEC_LOAPIC_TIMER, handle_timer);
        set_int_handler(VEC_LOAPIC_ERROR, handle_error);
        set_int_handler(VEC_LOAPIC_SPURIOUS, handle_spurious);
    }

    // 设置 DFR、LDR，根据 CPU 个数分类讨论
    if (CPU_FEATURE_X2APIC & g_cpu_features) {
        cpu_rwfence();
        uint32_t ldr = g_read(REG_LDR);
        lo->cluster_id = ldr >> 16;
        lo->logical_id = ldr & 0xffff;

        ASSERT(lo->cluster_id == (lo->apic_id >> 4));
        ASSERT(lo->logical_id == (1 << (lo->apic_id & 15)));

        if (g_max_cluster < lo->cluster_id) {
            g_max_cluster = lo->cluster_id;
        }
        if (g_max_logical < lo->logical_id) {
            g_max_logical = lo->logical_id;
        }
    } else if (g_loapic_num <= 8) {
        // 正好每个 CPU 对应一个比特
        lo->cluster_id = 0;
        lo->logical_id = 1 << cpu_index();
        g_write(REG_DFR, 0xffffffff); // flat model
        g_write(REG_LDR, lo->logical_id << 24);
    } else if (g_loapic_num <= 60) {
        // 必须分组，每个组最多 4 个 CPU
        lo->cluster_id = ix / 4;
        lo->logical_id = ix % 4;
        uint32_t ldr = (lo->cluster_id << 4) | lo->logical_id;
        g_write(REG_DFR, 0x0fffffff); // cluster model
        g_write(REG_LDR, ldr << 24);
    } else {
        // 还不够，只能让多个 CPU 使用相同的 Logical ID
        klog("fatal: too much processors!\n");
        emu_exit(1);
    }

    // 屏蔽中断号 0~31
    g_write(REG_TPR, 16);

    // 设置 LINT0、LINT1，参考 Intel MultiProcessor Spec 第 5.1 节
    // LINT0 连接到 8259A，但连接到 8259A 的设备也连接到 IO APIC，可以不设置
    // LINT1 连接到 NMI，我们只需要 BSP 能够处理 NMI

    if (0 == ix) {
        g_write(REG_LVT_LINT1, LOAPIC_LEVEL | LOAPIC_DM_NMI);
    }
    g_write(REG_LVT_ERROR, VEC_LOAPIC_ERROR);

    // 设置 spurious interrupt，开启这个 Local APIC
    g_write(REG_SVR, LOAPIC_SVR_ENABLE | VEC_LOAPIC_SPURIOUS);

    // 将已有中断丢弃
    g_write(REG_EOI, 0);
}


//------------------------------------------------------------------------------
// 使用 8254 校准 APIC Timer
//------------------------------------------------------------------------------

// channel 2 可以通过软件设置输入，可以通过软件读取输出
// 写端口 0x61 bit[0] 控制输入，读端口 0x61 bit[5] 获取输出
#define PIT_CH2 0x42
#define PIT_CMD 0x43

// time stamp counter 频率
static CONST uint64_t g_tsc_freq = 0;

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

    // 禁用 PIT channel 2
    out8(0x61, in8(0x61) & ~1);

    // TSC 频率可以保存下来，也许有用
    g_tsc_freq = (end_tsc - start_tsc) * 20;
    // klog("calibrated tsc freq %ld\n", g_tsc_freq);

    // 返回 APIC Timer 频率
    return (start_count - end_count) * 20;  // 1s = 20 * 50ms
}


//------------------------------------------------------------------------------
// 时钟
//------------------------------------------------------------------------------

// 一秒对应多少周期
static CONST uint32_t g_timer_freq = 0;


// 设置时钟中断频率，是否周期性中断
// freq==0 表示禁用时钟中断
// tickless OS 会多次调用这个函数
void local_apic_timer_set(int freq, local_apic_timer_mode_t mode) {
    ASSERT(freq >= 0);

    if (0 == freq) {
        g_write(REG_LVT_TIMER, LOAPIC_INT_MASK);
        return;
    }

    if (0 == g_timer_freq) {
        g_timer_freq = calibrate_using_pit_03();
        // klog("calibrated apic timer freq %u\n", g_timer_freq);
    }

    uint32_t lvt = LOAPIC_DM_FIXED | VEC_LOAPIC_TIMER;
    if (LOCAL_APIC_TIMER_PERIODIC == mode) {
        lvt |= LOAPIC_PERIODIC;
    } else {
        lvt |= LOAPIC_ONESHOT;
    }
    g_write(REG_LVT_TIMER, lvt);

    // 写入 ICR 会重启 timer
    g_write(REG_TIMER_DIV, 0x0b); // divide by 1
    g_write(REG_TIMER_ICR, g_timer_freq / freq);
}

// 忙等待
void local_apic_busywait(int us) {
    ASSERT(0 != g_timer_freq);

    uint32_t start  = g_read(REG_TIMER_CCR);
    uint32_t period = g_read(REG_TIMER_ICR);
    uint64_t delay  = ((uint64_t)g_timer_freq * us + 500000) / 1000000;

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
// 公开 API
//------------------------------------------------------------------------------

int local_apic_get_tmr(uint8_t vec) {
    int reg = vec / 32;
    int bit = vec % 32;
    uint32_t val = g_read(REG_TMR + reg);
    return (val >> bit) & 1;
}

void local_apic_send_eoi() {
    g_write(REG_EOI, 0);
}


//------------------------------------------------------------------------------
// 发送 IPI
//------------------------------------------------------------------------------

// 向目标处理器发送 INIT-IPI
INIT_TEXT void local_apic_send_init(int cpu) {
    ASSERT(cpu >= 0);
    ASSERT(cpu < cpu_count());

    uint32_t lo = LOAPIC_DM_INIT | LOAPIC_EDGE | LOAPIC_ASSERT;
    g_write_icr(g_loapics[cpu].apic_id, lo);
}

// 向目标处理器发送 startup-IPI
INIT_TEXT void local_apic_send_sipi(int cpu, int vec) {
    ASSERT(cpu >= 0);
    ASSERT(cpu < cpu_count());
    ASSERT((vec >= 0) && (vec < 256));
    ASSERT((vec < 0xa0) || (vec > 0xbf)); // 向量号 a0~bf 非法

    uint32_t lo = (vec & 0xff) | LOAPIC_DM_STARTUP | LOAPIC_EDGE | LOAPIC_ASSERT;
    g_write_icr(g_loapics[cpu].apic_id, lo);
}

void local_apic_send_ipi(int cpu, int vec) {
    ASSERT(cpu >= 0);
    ASSERT(cpu < cpu_count());
    ASSERT((vec >= 0) && (vec < 256));

    uint32_t lo = (vec & 0xff) | LOAPIC_DM_FIXED | LOAPIC_EDGE | LOAPIC_DEASSERT;
    g_write_icr(g_loapics[cpu].apic_id, lo);
}