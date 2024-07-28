#include <wheel.h>
#include <debug.h>

#include "multiboot1.h"
#include "multiboot2.h"

#include <devices/serial.h>



static void mb1_parse_pmmap(uint32_t mmap, uint32_t len) {
    //

    for (uint32_t off = 0, i = 0; off < len; ++i) {
        mb1_mmap_entry_t *ent = (mb1_mmap_entry_t *)(size_t)(mmap +off);
        off += ent->size + sizeof(ent->size);

        if (MB1_MEMORY_AVAILABLE == ent->type) {
            //
        }
    }
}

static void mb1_init(uint32_t ebx UNUSED) {
    mb1_info_t *info = (mb1_info_t *)(size_t)ebx;

    if (MB1_INFO_MEM_MAP & info->flags) {
        log("MB1 has memory map\n");
    }

    if (MB1_INFO_ELF_SHDR & info->flags) {
        log("MB1 has symbol table\n");
    }

    if (MB1_INFO_FRAMEBUFFER_INFO & info->flags) {
        log("MB1 has framebuffer info\n");
    }
}

static void mb2_init(uint32_t ebx UNUSED) {
    log("booted using MB2\n");
}

// static void log(const char *s) {
//     serial_puts(s, strlen(s));
// }

INIT_TEXT NORETURN void sys_init(uint32_t eax, uint32_t ebx) {
    serial_init();
    set_log_func(serial_puts);
    log("welcome to wheel os\n");

    // 解析 multiboot 信息
    switch (eax) {
    case MB1_BOOTLOADER_MAGIC: mb1_init(ebx); break;
    case MB2_BOOTLOADER_MAGIC: mb2_init(ebx); break;
    default:
        log("fatal: unknown multibooot magic 0x%08x\n", eax);
        goto end;
    }

end:
    while (1) {}
}
