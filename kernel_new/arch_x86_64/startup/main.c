#include <wheel.h>
#include <debug.h>
#include <mem_map.h>

#include "multiboot1.h"
#include "multiboot2.h"

#include <devices/serial.h>



static INIT_TEXT void mb1_parse_mem_map(uint32_t mmap, uint32_t len) {
    int range_num = 0;
    for (uint32_t off = 0; off < len;) {
        mb1_mmap_entry_t *ent = (mb1_mmap_entry_t *)(size_t)(mmap + off);
        off += ent->size + sizeof(ent->size);
        ++range_num;
    }

    mem_map_reserve(range_num);

    for (uint32_t off = 0; off < len;) {
        mb1_mmap_entry_t *ent = (mb1_mmap_entry_t *)(size_t)(mmap +off);
        off += ent->size + sizeof(ent->size);

        mem_type_t type = (MB1_MEMORY_AVAILABLE == ent->type)
            ? MEM_AVAILABLE : MEM_RESERVED;

        mem_map_add(ent->addr, ent->addr + ent->len, type);
    }
}


static INIT_TEXT void mb1_init(uint32_t ebx UNUSED) {
    mb1_info_t *info = (mb1_info_t *)(size_t)ebx;

    if (MB1_INFO_MEM_MAP & info->flags) {
        log("MB1 has memory map\n");
        mb1_parse_mem_map(info->mmap_addr, info->mmap_length);
    }

    if (MB1_INFO_ELF_SHDR & info->flags) {
        log("MB1 has symbol table\n");
    }

    if (MB1_INFO_FRAMEBUFFER_INFO & info->flags) {
        log("MB1 has framebuffer info\n");
    }
}




static INIT_TEXT void mb2_parse_mmap(void *tag) {
    mb2_tag_mmap_t *mmap = (mb2_tag_mmap_t *)tag;

    uint32_t mmap_len = mmap->tag.size - sizeof(mb2_tag_mmap_t);
    int range_num = (int)(mmap_len / mmap->entry_size);
    mem_map_reserve(range_num);

    for (int i = 0; i < range_num; ++i) {
        mb2_mmap_entry_t *ent = &mmap->entries[i];
        mem_type_t type;
        switch (ent->type) {
        case MB2_MEMORY_AVAILABLE:        type = MEM_AVAILABLE;   break;
        case MB2_MEMORY_ACPI_RECLAIMABLE: type = MEM_RECLAIMABLE; break;
        default:                          type = MEM_RESERVED;    break;
        }

        mem_map_add(ent->addr, ent->addr + ent->len, type);
    }
}

static INIT_TEXT void mb2_init(uint32_t ebx UNUSED) {
    size_t info = (size_t)ebx;
    uint32_t total_size = *(uint32_t *)info;

    uint32_t offset = 8;
    while (offset < total_size) {
        mb2_tag_t *tag = (mb2_tag_t *)(info + offset);
        offset += (tag->size + 7) & ~7;

        switch (tag->type) {
        case MB2_TAG_TYPE_MMAP:
            mb2_parse_mmap(tag);
            break;
        default:
            log("unknown MB2 tag %d\n", tag->type);
            break;
        }
    }
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
