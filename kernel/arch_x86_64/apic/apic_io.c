#include "apic.h"
#include <arch_api.h>
#include <debug.h>


// IO APIC 内存映射寄存器
#define IO_REG_SEL      0x00
#define IO_REG_WIN      0x10
#define IO_REG_EOI      0x40

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

static uint32_t ioapic_read(size_t base, uint32_t reg) {
    *(volatile uint32_t*)(base + DIRECT_MAP_ADDR + IO_REG_SEL) = reg;
    return *(volatile uint32_t*)(base + DIRECT_MAP_ADDR + IO_REG_WIN);
}

static void ioapic_write(size_t base, uint32_t reg, uint32_t data) {
    *(volatile uint32_t*)(base + DIRECT_MAP_ADDR + IO_REG_SEL) = reg;
    *(volatile uint32_t*)(base + DIRECT_MAP_ADDR + IO_REG_WIN) = data;
}

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

void ioapic_send_eoi(int vec) {
    ASSERT(vec >= VEC_GSI_BASE);

    ioapic_t *io = ioapic_for_gsi(vec - VEC_GSI_BASE);
    if (io && (io->ver >= 0x20)) {
        *(volatile uint32_t*)(io->address + DIRECT_MAP_ADDR + IO_REG_EOI) = vec;
    }
}

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
