#include <wheel.h>

#include "multiboot1.h"
#include "multiboot2.h"

#include <dev/serial.h>

#include <kstring.h>


void sys_init(uint32_t eax, uint32_t ebx) {
    char *video = (char*)0xb8000;
    const char *msg = "unknown\n";
    switch (eax) {
    case MB1_BOOTLOADER_MAGIC:
        msg = "multiboot version 1\n";
        break;
    case MB2_BOOTLOADER_MAGIC:
        msg = "multiboot version 2\n";
        break;
    }

    serial_init();
    serial_puts(msg, kstrlen(msg));

    for (int i = 0; msg[i]; ++i) {
        video[2*i] = msg[i];
        video[2*i+1] = 0x1f;
    }
    while (1) {}
}
