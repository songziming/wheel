#include <wheel.h>
#include <string.h>

#include "multiboot1.h"
#include "multiboot2.h"

#include <devices/serial.h>


static void mb1_init(uint32_t ebx UNUSED) {}

static void mb2_init(uint32_t ebx UNUSED) {}

static void log(const char *s) {
    size_t n = strnlen(s, 0x1000);
    serial_puts(s, n);
}

INIT_TEXT NORETURN void sys_init(uint32_t eax, uint32_t ebx) {
    // ()(size_t)ebx

    serial_init();
    log("welcome to wheel os\n");

    // 解析 multiboot 信息
    switch (eax) {
    case MB1_BOOTLOADER_MAGIC: mb1_init(ebx); break;
    case MB2_BOOTLOADER_MAGIC: mb2_init(ebx); break;
    default:
        // klog("fatal: unknown multibooot magic %x\n", eax);
        goto end;
    }

end:
    while (1) {}
}
