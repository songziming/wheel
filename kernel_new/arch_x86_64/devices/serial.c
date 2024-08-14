// UART 串口输出

#include "serial.h"
#include <generic/rw.h>
#include <library/spin.h>


#define COM1_PORT 0x3f8
#define BOCHS_PORT 0xe9

static spin_t g_serial_lock;

INIT_TEXT void serial_init() {
    spin_init(&g_serial_lock);
    out8(COM1_PORT + 1, 0x00);      // disable all interrupts
    out8(COM1_PORT + 3, 0x80);      // enable DLAB (set baud rate divisor)
    out8(COM1_PORT + 0, 0x03);      // set divisor to 3 (lo byte) 38400 baud
    out8(COM1_PORT + 1, 0x00);      //                  (hi byte)
    out8(COM1_PORT + 3, 0x03);      // 8 bits, no parity, one stop bit
    out8(COM1_PORT + 2, 0xc7);      // enable FIFO, clear them, with 14-byte threshold
    out8(COM1_PORT + 4, 0x0b);      // IRQs enabled, RTS/DSR set
}

void serial_putc(char c) {
    while ((in8(COM1_PORT + 5) & 0x20) == 0) {}
    out8(COM1_PORT, c);
#ifdef DEBUG
    out8(BOCHS_PORT, c);
#endif
}

void serial_puts(const char *s, size_t n) {
    int key = irq_spin_take(&g_serial_lock);
    for (size_t i = 0; i < n; ++i) {
        serial_putc(s[i]);
    }
    irq_spin_give(&g_serial_lock, key);
}
