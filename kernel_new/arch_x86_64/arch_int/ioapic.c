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



static INIT_BSS int       g_ioapic_max;
static CONST    int       g_ioapic_num;
static CONST    ioapic_t *g_ioapics    = NULL;



// 寄存器读写，base 是物理地址

static uint32_t io_apic_read(size_t base, uint32_t reg) {
    *(volatile uint32_t *)(base + DIRECT_MAP_ADDR + IO_REG_SEL) = reg;
    return *(volatile uint32_t *)(base + DIRECT_MAP_ADDR + IO_REG_WIN);
}

static void io_apic_write(size_t base, uint32_t reg, uint32_t data) {
    *(volatile uint32_t *)(base + DIRECT_MAP_ADDR + IO_REG_SEL) = reg;
    *(volatile uint32_t *)(base + DIRECT_MAP_ADDR + IO_REG_WIN) = data;
}




void io_apic_send_eoi(ioapic_t *io, uint8_t vec) {
    if (io->ver >= 0x20) {
        *(volatile uint32_t *)(io->address + DIRECT_MAP_ADDR + IO_REG_EOI) = vec;
    }
}


// 寻找 GSI 所对应的 IO APIC
ioapic_t *io_apic_for_gsi(uint32_t gsi) {
    for (int i = 0; i < g_ioapic_num; ++i) {
        ioapic_t *io = &g_ioapics[i];
        if ((io->gsi_base <= gsi) && (gsi < io->gsi_base + io->red_num)) {
            return io;
        }
        // gsi -= io->red_num;
    }
    return NULL;
}

void io_apic_set_red(uint32_t gsi, uint32_t hi, uint32_t lo) {
    ioapic_t *io = io_apic_for_gsi(gsi);
    if (NULL == io) {
        return;
    }

    gsi -= io->gsi_base;
    io_apic_write(io->address, IOAPIC_RED_H(gsi), hi);
    io_apic_write(io->address, IOAPIC_RED_L(gsi), lo);
}

void io_apic_mask_gsi(uint32_t gsi) {
    ioapic_t *io = io_apic_for_gsi(gsi);
    if (NULL == io) {
        return;
    }

    gsi -= io->gsi_base;
    uint32_t red_lo = io_apic_read(io->address, IOAPIC_RED_L(gsi));
    red_lo |= IOAPIC_INT_MASK;
    io_apic_write(io->address, IOAPIC_RED_L(gsi), red_lo);
}

void io_apic_unmask_gsi(uint32_t gsi) {
    ioapic_t *io = io_apic_for_gsi(gsi);
    if (NULL == io) {
        return;
    }

    gsi -= io->gsi_base;
    uint32_t red_lo = io_apic_read(io->address, IOAPIC_RED_L(gsi));
    red_lo &= ~IOAPIC_INT_MASK;
    io_apic_write(io->address, IOAPIC_RED_L(gsi), red_lo);
}


//------------------------------------------------------------------------------
// 初始化函数，由 arch_smp 调用
//------------------------------------------------------------------------------

INIT_TEXT void ioapics_alloc(int n) {
    ASSERT(NULL == g_ioapics);

    g_ioapic_max = n;
    g_ioapic_num = 0;
    g_ioapics = early_alloc_ro(n * sizeof(ioapic_t));
}

INIT_TEXT void ioapic_add_madt() {
    // TODO 传入 madt::io_apic 条目，解析内容
}
