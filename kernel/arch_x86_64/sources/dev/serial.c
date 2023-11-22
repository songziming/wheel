// UART 串口输出

#include <dev/serial.h>
#include <arch_api_p.h>

#define COM1_PORT 0x3f8
#define BOCHS_PORT 0xe9

INIT_TEXT void serial_init() {
    out_byte(COM1_PORT + 1, 0x00);      // disable all interrupts
    out_byte(COM1_PORT + 3, 0x80);      // enable DLAB (set baud rate divisor)
    out_byte(COM1_PORT + 0, 0x03);      // set divisor to 3 (lo byte) 38400 baud
    out_byte(COM1_PORT + 1, 0x00);      //                  (hi byte)
    out_byte(COM1_PORT + 3, 0x03);      // 8 bits, no parity, one stop bit
    out_byte(COM1_PORT + 2, 0xc7);      // enable FIFO, clear them, with 14-byte threshold
    out_byte(COM1_PORT + 4, 0x0b);      // IRQs enabled, RTS/DSR set
}

void serial_putc(char c) {
    while ((in_byte(COM1_PORT + 5) & 0x20) == 0) {}
    out_byte(COM1_PORT, c);
#ifdef DEBUG
    out_byte(BOCHS_PORT, c);
#endif
}

void serial_puts(const char *s, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        serial_putc(s[i]);
    }
}
