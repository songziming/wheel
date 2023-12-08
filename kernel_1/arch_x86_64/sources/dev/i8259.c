#include <dev/i8259.h>
#include <liba/rw.h>

// PIC master
#define PIC1_CMD        (0x20)
#define PIC1_DAT        (0x21)

// PIC slave
#define PIC2_CMD        (0xA0)
#define PIC2_DAT        (0xA1)

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

// 将 8259 中断号映射到 32~47，并禁用 8259 的中断（使用 APIC）
INIT_TEXT void i8259_disable() {
    out_byte(PIC1_CMD, ICW1_INIT + ICW1_ICW4);
    out_byte(PIC2_CMD, ICW1_INIT + ICW1_ICW4);
    out_byte(PIC1_DAT, 0x20);       // ICW2: map master PIC vector base
    out_byte(PIC2_DAT, 0x28);       // ICW2: map slave PIC vector base
    out_byte(PIC1_DAT, 4);          // ICW3: slave PIC at IRQ2 (0000 0100)
    out_byte(PIC2_DAT, 2);          // ICW3: slave PIC's identity (0000 0010)
    out_byte(PIC1_DAT, ICW4_8086);
    out_byte(PIC2_DAT, ICW4_8086);
    out_byte(PIC1_DAT, 0xff);       // mask all pins on master chip
    out_byte(PIC2_DAT, 0xff);       // mask all pins on slave chip
}
