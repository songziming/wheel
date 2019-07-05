#include <wheel.h>

// legacy PIC interrupts are mapped to 32 (0x20)
// IO APIC interrupts are mapped to 64 (0x40)

// 8259 PIC port
#define PIC1            0x20        // IO base address for PIC master
#define PIC2            0xA0        // IO base address for PIC slave
#define PIC1_CMD        PIC1
#define PIC1_DAT        (PIC1 + 1)
#define PIC2_CMD        PIC2
#define PIC2_DAT        (PIC2 + 1)

// 8259 PIC control words
#define ICW1_ICW4       0x01        // ICW4 (not) needed
#define ICW1_SINGLE     0x02        // Single (cascade) mode
#define ICW1_INTERVAL4  0x04        // Call address interval 4 (8)
#define ICW1_LEVEL      0x08        // Level triggered (edge) mode
#define ICW1_INIT       0x10        // Initialization - required!
#define ICW4_8086       0x01        // 8086/88 (MCS-80/85) mode
#define ICW4_AUTO       0x02        // Auto (normal) EOI
#define ICW4_BUF_SLAVE  0x08        // Buffered mode/slave
#define ICW4_BUF_MASTER 0x0C        // Buffered mode/master
#define ICW4_SFNM       0x10        // Special fully nested (not)

// IO APIC register address map
#define IOAPIC_INDEX    0x0000
#define IOAPIC_DATA     0x0010
#define IOAPIC_IRQ_PIN  0x0020          // IRQ pin assertion

// the following registers must be accessed using INDEX and DATA
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
#define IOAPIC_FIXED    0x00000000
#define IOAPIC_LOWEST   0x00000100
#define IOAPIC_SMI      0x00000200
#define IOAPIC_NMI      0x00000400
#define IOAPIC_INIT     0x00000500
#define IOAPIC_EXTINT   0x00000700
#define IOAPIC_VEC_MASK 0x000000ff

typedef struct ioapic {
    u8   id;
    u64  addr;
    u8 * base;
    int  gsi_start;         // global irq base
    int  rednum;            // redirection entry count
} ioapic_t;
static ioapic_t ioapic_devs[4];
static int      ioapic_count = 0;

static u32 irq_to_gsi[16] = { 0 };     // top bit set means redirected
static u16 gsi_to_flg[16] = { 0 };

//------------------------------------------------------------------------------
// helper functions to access mapped io apic registers

static inline u32 ioapic_read(u8 * base, u32 key) {
    write32(base + IOAPIC_INDEX, key);
    return read32(base + IOAPIC_DATA);
}

static inline void ioapic_write(u8 * base, u32 key, u32 data) {
    write32(base + IOAPIC_INDEX, key);
    write32(base + IOAPIC_DATA, data);
}

//------------------------------------------------------------------------------
// public function

// convert irq to global system interrupt
// notice that irq and gsi are not vector numbers
int ioapic_irq_to_gsi(int irq) {
    if ((irq < 16) && (irq_to_gsi[irq] & 0x80)) {
        return irq_to_gsi[irq] & 0x7f;
    }
    return irq;
}

// TODO: make io apic interrupts span multiple priviledges
int ioapic_gsi_to_vec(int gsi) {
    return 0x40 + gsi;
}

void ioapic_gsi_mask(int gsi) {
    for (int i = 0; i < ioapic_count; ++i) {
        if (gsi < ioapic_devs[i].rednum) {
            u32 lo = ioapic_read(ioapic_devs[i].base, IOAPIC_RED_L(gsi));
            lo |= IOAPIC_INT_MASK;
            ioapic_write(ioapic_devs[i].base, IOAPIC_RED_L(gsi), lo);
            return;
        }
        gsi -= ioapic_devs[i].rednum;
    }
}

void ioapic_gsi_unmask(int gsi) {
    for (int i = 0; i < ioapic_count; ++i) {
        if (gsi < ioapic_devs[i].rednum) {
            u32 lo = ioapic_read(ioapic_devs[i].base, IOAPIC_RED_L(gsi));
            lo &= ~IOAPIC_INT_MASK;
            ioapic_write(ioapic_devs[i].base, IOAPIC_RED_L(gsi), lo);
            return;
        }
        gsi -= ioapic_devs[i].rednum;
    }
}

//------------------------------------------------------------------------------
// initialization functions

__INIT void ioapic_dev_add(madt_ioapic_t * tbl) {
    if (ioapic_count < 4) {
        ioapic_devs[ioapic_count].id        = tbl->id;
        ioapic_devs[ioapic_count].addr      = tbl->address;
        ioapic_devs[ioapic_count].gsi_start = tbl->global_irq_base;
        ++ioapic_count;
    }
}

__INIT void ioapic_int_override(madt_int_override_t * tbl) {
    if (tbl->source_irq < 16) {
        irq_to_gsi[tbl->source_irq] = tbl->global_irq | 0x80;
        gsi_to_flg[tbl->global_irq] = tbl->inti_flags;
    }
}

// initialize all io apics installed
__INIT void ioapic_all_init() {
    // first disable 8259 PIC properly
    out8(PIC1_CMD, ICW1_INIT + ICW1_ICW4);
    out8(PIC2_CMD, ICW1_INIT + ICW1_ICW4);
    out8(PIC1_DAT, 0x20);       // ICW2: map master PIC vector base
    out8(PIC2_DAT, 0x28);       // ICW2: map slave PIC vector base
    out8(PIC1_DAT, 4);          // ICW3: slave PIC at IRQ2 (0000 0100)
    out8(PIC2_DAT, 2);          // ICW3: slave PIC's identity (0000 0010)
    out8(PIC1_DAT, ICW4_8086);
    out8(PIC2_DAT, ICW4_8086);
    out8(PIC1_DAT, 0xff);       // mask all pins on master chip
    out8(PIC2_DAT, 0xff);       // mask all pins on slave chip

    for (int i = 0; i < ioapic_count; ++i) {
        u8 * base = (u8 *) phys_to_virt(ioapic_devs[i].addr);
        u32  ver  = ioapic_read(base, IOAPIC_VER);
        ioapic_devs[i].base   = base;
        ioapic_devs[i].rednum = (ver >> 16) & 0xff;

        int gsi = 0;
        if (i == 0) {
            // for IRQs < 16, default are edge-triggered, active high
            for (; gsi < 16; ++gsi) {
                u32 flg = gsi_to_flg[gsi];
                u32 lo  = IOAPIC_FIXED | IOAPIC_INT_MASK | IOAPIC_PHYSICAL
                        | ((gsi + 0x40) & 0xff);
                if ((flg & POLARITY_MASK) == POLARITY_LOW) {
                    lo |= IOAPIC_LOW;
                } else {
                    lo |= IOAPIC_HIGH;
                }
                if ((flg & TRIGMODE_MASK) == TRIGMODE_LEVEL) {
                    lo |= IOAPIC_LEVEL;
                } else {
                    lo |= IOAPIC_EDGE;
                }
                ioapic_write(ioapic_devs[i].base, IOAPIC_RED_H(gsi), 0);
                ioapic_write(ioapic_devs[i].base, IOAPIC_RED_L(gsi), lo);
            }
        }

        // for IRQs >= 16, default are level-triggered, active low
        for (; gsi < ioapic_devs[i].rednum; ++gsi) {
            u32 lo = IOAPIC_FIXED | IOAPIC_INT_MASK | IOAPIC_PHYSICAL
                   | IOAPIC_LOW | IOAPIC_LEVEL | ((0x40 + gsi) & 0xff);
            ioapic_write(ioapic_devs[i].base, IOAPIC_RED_H(gsi), 0);
            ioapic_write(ioapic_devs[i].base, IOAPIC_RED_L(gsi), lo);
        }
    }
}
