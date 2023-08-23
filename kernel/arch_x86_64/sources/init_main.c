// 初始化程序


#include <base.h>
#include <debug.h>

#include <arch_api.h>
#include <multiboot1.h>
#include <multiboot2.h>

#include <arch_mem.h>

#include <dev/out_serial.h>


static INIT_TEXT void mb1_init(uint32_t ebx) {
    mb1_info_t *info = (mb1_info_t *)(size_t)ebx;

    // 统计本机物理内存布局
    // 首先统计可用内存范围的数量，提前申请好空间，然后再填充数据
    if (MB1_INFO_MEM_MAP & info->flags) {
        int valid_num = 0;
        for (uint32_t off = 0; off < info->mmap_length;) {
            mb1_mmap_entry_t *ent = (mb1_mmap_entry_t *)(size_t)(info->mmap_addr + off);
            off += ent->size + sizeof(ent->size);
            if (MB1_MEMORY_AVAILABLE == ent->type) {
                ++valid_num;
            }
        }
        ram_range_reserve(valid_num);
        int valid_idx = 0;
        for (uint32_t off = 0; off < info->mmap_length;) {
            mb1_mmap_entry_t *ent = (mb1_mmap_entry_t *)(size_t)(info->mmap_addr + off);
            off += ent->size + sizeof(ent->size);
            if (MB1_MEMORY_AVAILABLE == ent->type) {
                ram_range_set(valid_idx++, ent->addr, ent->len);
            }
        }

#if DEBUG
        dbg_print("%d ram ranges\n", valid_num);
#endif
    }
}

static INIT_TEXT void mb2_init(uint32_t ebx) {
    size_t info = (size_t)ebx;
    uint32_t total_size = *(uint32_t *)info;
}

INIT_TEXT NORETURN void sys_init(uint32_t eax, uint32_t ebx) {
    serial_init();
    g_dbg_print_func = serial_puts;

    early_alloc_init();

    switch (eax) {
    case MB1_BOOTLOADER_MAGIC: mb1_init(ebx); break;
    case MB2_BOOTLOADER_MAGIC: mb2_init(ebx); break;
    default:
        dbg_print("error: unknown multibooot magic %x\n", eax);
        cpu_halt();
    }

    while (1) {}
}

INIT_TEXT NORETURN void sys_init_ap(uint32_t ebx) {
    (void)ebx;
}
