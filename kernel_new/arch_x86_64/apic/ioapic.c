#include "ioapic.h"
#include <arch_impl.h>
#include <memory/early_alloc.h>
#include <library/debug.h>


// 理论上，IO APIC 不属于 CPU，而是集成在芯片组中
// 大部分资源只给出了 82093AA 芯片手册，但那款 IO APIC 过于老旧，版本号仅为 0x11
// 应该参考 Intel IO Controller Hub 的文档，ICH 也就是南桥，最新版为 ICH 10

// Intel® I/O Controller Hub 10 (Intel® ICH10) Family Datasheet
// 主要参考其中的 13.5

// 0x20 及以后版本的 IO APIC 才有 EOI 寄存器

// x2APIC 的 APIC ID 是 32-bit 整数，IO APIC 的 redirect entry 装不下
// 因此需要设置 Local APIC 的 LDR（logical destination register）
// 凡是 LDR 取值匹配的 Local APIC 都会接收到中断，即中断可以发给多个 CPU
// 但是 LDR 到底是多少 bit？如果是 8-bit，则取值可能冲突；如果 32-bit，怎么与 RED 比较？


static CONST int       g_ioapic_num = 0;
static CONST ioapic_t *g_ioapics = NULL;

static CONST uint8_t   g_irq_max = 0;
static CONST uint32_t  g_gsi_max = 0;

typedef struct trigger_mode {
    uint8_t edge : 1;
    uint8_t high : 1;
} trigger_mode_t;

static CONST uint32_t *g_irq_to_gsi = NULL;
static CONST uint8_t  *g_gsi_to_irq = NULL;
static CONST trigger_mode_t *g_gsi_modes = NULL;


//------------------------------------------------------------------------------
// 寄存器定义
//------------------------------------------------------------------------------

// IO APIC 内存映射寄存器
#define IO_REG_SEL      0x00
#define IO_REG_WIN      0x10
#define IO_REG_EOI      0x40

// #define IOAPIC_IRQ_PIN  0x0020          // IRQ pin assertion

// 间接寄存器，通过 sel、win 访问
#define IOAPIC_ID       0x00
#define IOAPIC_VER      0x01
#define IOAPIC_ARB      0x02
#define IOAPIC_BOOT     0x03            // boot configuration
#define IOAPIC_RED_L(i) (0x10 + 2*(i))
#define IOAPIC_RED_H(i) (0x11 + 2*(i))

// redirection table entry upper 32bit
#define IOAPIC_DST      0xff000000

// redirection table entry lower 32 bit
#define IOAPIC_INT_MASK 0x00010000
#define IOAPIC_LEVEL    0x00008000
#define IOAPIC_EDGE     0x00000000
#define IOAPIC_REMOTE   0x00004000
#define IOAPIC_LOW      0x00002000
#define IOAPIC_HIGH     0x00000000
#define IOAPIC_LOGICAL  0x00000800
#define IOAPIC_PHYSICAL 0x00000000

// delivery mode
#define IOAPIC_DM_FIXED    0x00000000
#define IOAPIC_DM_LOWEST   0x00000100
#define IOAPIC_DM_SMI      0x00000200
#define IOAPIC_DM_NMI      0x00000400
#define IOAPIC_DM_INIT     0x00000500
#define IOAPIC_DM_EXTINT   0x00000700

#define IOAPIC_VEC_MASK 0x000000ff


//------------------------------------------------------------------------------
// 寄存器读写，base 是物理地址
//------------------------------------------------------------------------------

static uint32_t ioapic_read(size_t base, uint32_t reg) {
    *(volatile uint32_t *)(base + DIRECT_MAP_ADDR + IO_REG_SEL) = reg;
    return *(volatile uint32_t *)(base + DIRECT_MAP_ADDR + IO_REG_WIN);
}

static void ioapic_write(size_t base, uint32_t reg, uint32_t data) {
    *(volatile uint32_t *)(base + DIRECT_MAP_ADDR + IO_REG_SEL) = reg;
    *(volatile uint32_t *)(base + DIRECT_MAP_ADDR + IO_REG_WIN) = data;
}

// void ioapic_send_eoi(ioapic_t *io, uint8_t vec) {
//     if (io->ver >= 0x20) {
//         *(volatile uint32_t *)(io->address + DIRECT_MAP_ADDR + IO_REG_EOI) = vec;
//     }
// }

//------------------------------------------------------------------------------
// 公开函数
//------------------------------------------------------------------------------

// 寻找 GSI 所对应的 IO APIC
static ioapic_t *ioapic_for_gsi(uint32_t gsi) {
    for (int i = 0; i < g_ioapic_num; ++i) {
        ioapic_t *io = &g_ioapics[i];
        if ((io->gsi_base <= gsi) && (gsi < io->gsi_base + io->red_num)) {
            return io;
        }
    }
    return NULL;
}

// void ioapic_set_red(uint32_t gsi, uint32_t hi, uint32_t lo) {
//     ioapic_t *io = ioapic_for_gsi(gsi);
//     if (NULL == io) {
//         return;
//     }

//     gsi -= io->gsi_base;
//     ioapic_write(io->address, IOAPIC_RED_H(gsi), hi);
//     ioapic_write(io->address, IOAPIC_RED_L(gsi), lo);
// }

void ioapic_mask_gsi(uint32_t gsi) {
    ioapic_t *io = ioapic_for_gsi(gsi);
    if (NULL == io) {
        return;
    }

    gsi -= io->gsi_base;
    uint32_t red_lo = ioapic_read(io->address, IOAPIC_RED_L(gsi));
    red_lo |= IOAPIC_INT_MASK;
    ioapic_write(io->address, IOAPIC_RED_L(gsi), red_lo);
}

void ioapic_unmask_gsi(uint32_t gsi) {
    ioapic_t *io = ioapic_for_gsi(gsi);
    if (NULL == io) {
        return;
    }

    gsi -= io->gsi_base;
    uint32_t red_lo = ioapic_read(io->address, IOAPIC_RED_L(gsi));
    red_lo &= ~IOAPIC_INT_MASK;
    ioapic_write(io->address, IOAPIC_RED_L(gsi), red_lo);
}

// EOI 用来清除 Remote_IRR，使用重定位条目中的向量号进行匹配
void ioapic_send_eoi(int vec) {
    ASSERT(vec >= VEC_GSI_BASE);

    ioapic_t *io = ioapic_for_gsi(vec - VEC_GSI_BASE);
    if (io && (io->ver >= 0x20)) {
        *(volatile uint32_t *)(io->address + DIRECT_MAP_ADDR + IO_REG_EOI) = vec;
    }
}


//------------------------------------------------------------------------------
// 初始化
//------------------------------------------------------------------------------

INIT_TEXT void ioapic_alloc(int n, uint8_t irq_max, uint32_t gsi_max) {
    g_ioapic_num = n;
    g_irq_max = irq_max;
    g_gsi_max = gsi_max;

    g_ioapics = early_alloc_ro(n * sizeof(ioapic_t));
    g_irq_to_gsi = early_alloc_ro((irq_max + 1) * sizeof(uint32_t));
    g_gsi_to_irq = early_alloc_ro((gsi_max + 1) * sizeof(uint8_t));
    g_gsi_modes  = early_alloc_ro((gsi_max + 1) * sizeof(trigger_mode_t));

    // 默认情况下，8259 IRQ 0~15 与 GSI 0~15 对应
    for (uint8_t i = 0; i < irq_max; ++i) {
        g_irq_to_gsi[i] = i;
    }

    // 默认是上升沿触发
    for (uint32_t i = 0; i < gsi_max; ++i) {
        g_gsi_to_irq[i] = i;
        g_gsi_modes[i].edge = 1;
        g_gsi_modes[i].high = 1;
    }
}

INIT_TEXT void ioapic_parse(int i, madt_ioapic_t *tbl) {
    ASSERT(g_ioapic_num > 0);
    ASSERT(i >= 0);
    ASSERT(i < g_ioapic_num);

    g_ioapics[i].apic_id  = tbl->id;
    g_ioapics[i].gsi_base = tbl->gsi_base;
    g_ioapics[i].address  = tbl->address;
}

INIT_TEXT void override_int(madt_int_override_t *tbl) {
    ASSERT(g_irq_max > 0);
    ASSERT(g_gsi_max > 0);

    g_irq_to_gsi[tbl->source] = tbl->gsi;
    g_gsi_to_irq[tbl->gsi] = tbl->source;

    if (TRIGMODE_LEVEL == (TRIGMODE_MASK & tbl->inti_flags)) {
        g_gsi_modes[tbl->gsi].edge = 0;
    }
    if (POLARITY_LOW == (POLARITY_MASK & tbl->inti_flags)) {
        g_gsi_modes[tbl->gsi].high = 0;
    }
}

int irq_to_gsi(int irq) {
    if (irq < g_irq_max) {
        return g_irq_to_gsi[irq];
    }
    return irq;
}

static INIT_TEXT void ioapic_init(ioapic_t *io) {
    uint32_t id = ioapic_read(io->address, IOAPIC_ID);
    uint32_t ver = ioapic_read(io->address, IOAPIC_VER);

    if (((id >> 24) & 0x0f) != io->apic_id) {
        log("warning: IO APIC id %u different from MADT (%u)\n", id, io->apic_id);
    }

    io->ver = ver & 0xff;
    io->red_num = ((ver >> 16) & 0xff) + 1;
    if (ver & (1 << 15)) {
        log("no Pin Assertion Register\n");
    }
}

static INIT_TEXT int gsi_is_edge(int gsi) {
    return g_gsi_modes[gsi].edge;
}

static INIT_TEXT int gsi_is_high(int gsi) {
    return g_gsi_modes[gsi].high;
}

// 初始化所有的 IO APIC，默认禁用所有硬件中断，按需开启
INIT_TEXT void ioapic_init_all() {
    for (int i = 0; i < g_ioapic_num; ++i) {
        ioapic_t *io = &g_ioapics[i];
        ioapic_init(io);

        // 前面 16 个中断来自 ISA，以 logical 模式广播发送给所有 CPU
        int ent = 0;
        for (; (io->gsi_base + ent < 16) && (ent < io->red_num); ++ent) {
            uint32_t lo = IOAPIC_DM_LOWEST | IOAPIC_LOGICAL;
            lo |= gsi_is_edge(ent) ? IOAPIC_EDGE : IOAPIC_LEVEL;
            lo |= gsi_is_high(ent) ? IOAPIC_HIGH : IOAPIC_LOW;
            lo |= (ent + VEC_GSI_BASE) & IOAPIC_VEC_MASK;
            lo |= IOAPIC_INT_MASK;
            // ioapic_write(io->address, IOAPIC_RED_H(ent), 0x01000000); // 固定发送给 CPU0
            // ioapic_write(io->address, IOAPIC_RED_H(ent), 0xfe000000); // 发送给 CPU0 之外的任意 CPU
            ioapic_write(io->address, IOAPIC_RED_H(ent), 0xff000000); // 发送给所有 CPU
            ioapic_write(io->address, IOAPIC_RED_L(ent), lo);
        }

        // IRQ 之后的硬件中断，level-triggered，active low
        for (; ent < io->red_num; ++ent) {
            uint32_t lo = IOAPIC_DM_LOWEST | IOAPIC_LOGICAL | IOAPIC_LEVEL | IOAPIC_LOW;
            lo |= (io->gsi_base + ent + VEC_GSI_BASE) & IOAPIC_VEC_MASK;
            lo |= IOAPIC_INT_MASK;
            ioapic_write(io->address, IOAPIC_RED_H(ent), 0xff000000);
            ioapic_write(io->address, IOAPIC_RED_L(ent), lo);
        }
    }
}
