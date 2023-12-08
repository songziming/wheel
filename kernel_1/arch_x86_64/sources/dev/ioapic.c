// 硬件可能有多个 IO APIC

#include <base.h>
#include <arch_smp.h>



//------------------------------------------------------------------------------
// 读写 IO APIC 寄存器
//------------------------------------------------------------------------------

#define IO_SEL  0x00
#define IO_WIN  0x10

static uint32_t io_read(uint64_t base, uint32_t reg) {
    *(volatile uint32_t *)(base + IO_SEL) = reg;
    return *(volatile uint32_t *)(base + IO_WIN);
}

static void io_write(uint64_t base, uint32_t reg, uint32_t val) {
    *(volatile uint32_t *)(base + IO_SEL) = reg;
    *(volatile uint32_t *)(base + IO_WIN) = val;
}



// 寄存器编号
#define IOAPIC_ID       0x00
#define IOAPIC_VER      0x01
#define IOAPIC_ARB      0x02
#define IOAPIC_BOOT     0x03            // boot configuration
#define IOAPIC_RED_L(i) (0x10 + 2*(i))
#define IOAPIC_RED_H(i) (0x11 + 2*(i))

// redirection table entry lower 32 bit
#define IOAPIC_INT_MASK 0x00010000
#define IOAPIC_LEVEL    0x00008000
#define IOAPIC_EDGE     0x00000000
#define IOAPIC_REMOTE   0x00004000
#define IOAPIC_LOW      0x00002000
#define IOAPIC_HIGH     0x00000000
#define IOAPIC_LOGICAL  0x00000800
#define IOAPIC_PHYSICAL 0x00000000
#define IOAPIC_FIXED    0x00000000
#define IOAPIC_LOWEST   0x00000100
#define IOAPIC_SMI      0x00000200
#define IOAPIC_NMI      0x00000400
#define IOAPIC_INIT     0x00000500
#define IOAPIC_EXTINT   0x00000700
#define IOAPIC_VEC_MASK 0x000000ff

// redirection table entry upper 32bit
#define IOAPIC_DST      0xff000000



void ioapic_dev_init(uint64_t base) {
    uint32_t ver = io_read(base, IOAPIC_VER);
    int rednum = (ver >> 16) & 0xff; // 重定位条目数量

    for (int i = 0; i < rednum; ++i) {
        io_write(base, IOAPIC_RED_H(i), 0);
    }
}

void ioapic_init_all() {
    for (int i = 0; i < g_ioapic_count; ++i) {
        uint64_t base = DIRECT_MAP_BASE + g_ioapics[i].addr;
        ioapic_dev_init(base);
    }
}
