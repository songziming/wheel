#include <wheel.h>

#include <multiboot1.h>
#include <multiboot2.h>

#include <dev_serial.h>


static INIT_TEXT void mb1_init(uint32_t ebx) {
    mb1_info_t *info = (mb1_info_t *)(size_t)ebx;

    klog("booted by %s\n", (const char *)(size_t)info->boot_loader_name);
}

static INIT_TEXT void mb2_init(uint32_t ebx) {
    size_t info = (size_t)ebx;
    uint32_t total_size = *(uint32_t *)info;

    klog("multiboot 2 size %u\n", total_size);
}


// BSP 初始化函数
INIT_TEXT void sys_init(uint32_t eax, uint32_t ebx) {
    serial_init();
    set_log_func(serial_puts);

    switch (eax) {
    case MB1_BOOTLOADER_MAGIC: mb1_init(ebx); break;
    case MB2_BOOTLOADER_MAGIC: mb2_init(ebx); break;
    default:
        klog("error: unknown multibooot magic %x\n", eax);
        goto end;
    }

end:
    vmshutdown(0);
    while (1) {}
}
