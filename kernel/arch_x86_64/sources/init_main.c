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

    // 记录本机物理内存布局
    // 首先统计内存范围的数量，提前申请好空间，然后填充数据
    if (MB1_INFO_MEM_MAP & info->flags) {
        int map_len = 0;
        for (uint32_t off = 0; off < info->mmap_length;) {
            mb1_mmap_entry_t *ent = (mb1_mmap_entry_t *)(size_t)(info->mmap_addr + off);
            off += ent->size + sizeof(ent->size);
            ++map_len;
        }
        ram_map_reserve(map_len);
        int map_idx = 0;
        for (uint32_t off = 0; off < info->mmap_length; ++map_idx) {
            mb1_mmap_entry_t *ent = (mb1_mmap_entry_t *)(size_t)(info->mmap_addr + off);
            off += ent->size + sizeof(ent->size);
            if (MB1_MEMORY_AVAILABLE == ent->type) {
                ram_map_set(map_idx, RAM_AVAILABLE, ent->addr, ent->len);
            } else {
                ram_map_set(map_idx, RAM_RESERVED, ent->addr, ent->len);
            }
        }
        ASSERT(map_idx == map_len);
    }
}

static INIT_TEXT void mb2_init(uint32_t ebx) {
    size_t info = (size_t)ebx;
    uint32_t total_size = *(uint32_t *)info;

    uint32_t offset = 8;
    while (offset < total_size) {
        mb2_tag_t *tag = (mb2_tag_t *)(info + offset);
        offset += (tag->size + 7) & ~7;

        switch (tag->type) {
        case MB2_TAG_TYPE_MMAP: {
            mb2_tag_mmap_t *mmap = (mb2_tag_mmap_t *)tag;
            size_t len = (mmap->tag.size - sizeof(mb2_tag_mmap_t)) / mmap->entry_size;
            ram_map_reserve(len);
            for (size_t i = 0; i < len; ++i) {
                mb2_mmap_entry_t *ent = &mmap->entries[i];
                if (MB2_MEMORY_AVAILABLE == ent->type) {
                    ram_map_set(i, RAM_AVAILABLE, ent->addr, ent->len);
                } else if (MB2_MEMORY_ACPI_RECLAIMABLE == ent->type) {
                    ram_map_set(i, RAM_RECLAIMABLE, ent->addr, ent->len);
                } else {
                    ram_map_set(i, RAM_RESERVED, ent->addr, ent->len);
                }
            }
            break;
        }
        default:
            break;
        }
    }
}

INIT_TEXT NORETURN void sys_init(uint32_t eax, uint32_t ebx) {
    serial_init();
    g_dbg_print_func = serial_puts;

    switch (eax) {
    case MB1_BOOTLOADER_MAGIC: mb1_init(ebx); break;
    case MB2_BOOTLOADER_MAGIC: mb2_init(ebx); break;
    default:
        dbg_print("error: unknown multibooot magic %x\n", eax);
        cpu_halt();
    }

#if DEBUG
    dbg_print("ram ranges:\n");
    ram_map_show();
#endif

    cpu_halt();
    while (1) {}
}

INIT_TEXT NORETURN void sys_init_ap(uint32_t ebx) {
    (void)ebx;

    cpu_halt();
    while (1) {}
}
