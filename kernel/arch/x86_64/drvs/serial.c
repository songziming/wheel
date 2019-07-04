#include <wheel.h>

#define COM1_PORT 0x3f8

static void serial_put_char(char c) {
    while ((in8(COM1_PORT + 5) & 0x20) == 0) {}
    out8(COM1_PORT, c);
}

usize serial_write(const char * s, usize len) {
    for (usize i = 0; i < len; ++i) {
        serial_put_char(s[i]);
    }
    return len;
}

__INIT void serial_dev_init() {
    out8(COM1_PORT + 1, 0x00);      // disable all interrupts
    out8(COM1_PORT + 3, 0x80);      // enable DLAB (set baud rate divisor)
    out8(COM1_PORT + 0, 0x03);      // set divisor to 3 (lo byte) 38400 baud
    out8(COM1_PORT + 1, 0x00);      //                  (hi byte)
    out8(COM1_PORT + 3, 0x03);      // 8 bits, no parity, one stop bit
    out8(COM1_PORT + 2, 0xc7);      // enable FIFO, clear them, with 14-byte threshold
    out8(COM1_PORT + 4, 0x0b);      // IRQs enabled, RTS/DSR set
}
