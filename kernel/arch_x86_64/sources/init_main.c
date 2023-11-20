#include <wheel.h>

#include <multiboot1.h>
#include <multiboot2.h>

#include <arch_mem.h>

#include <dev_serial.h>


static INIT_TEXT void mb1_init(uint32_t ebx) {
    mb1_info_t *info = (mb1_info_t *)(size_t)ebx;

    if (MB1_INFO_MEM_MAP & info->flags) {
        g_rammap_len = 0;
        for (uint32_t off = 0; off < info->mmap_length;) {
            mb1_mmap_entry_t *ent = (mb1_mmap_entry_t *)(size_t)(info->mmap_addr + off);
            off += ent->size + sizeof(ent->size);
            ++g_rammap_len;
        }

        g_rammap = early_alloc_ro(g_rammap_len * sizeof(ram_range_t));

        for (uint32_t off = 0, i = 0; off < info->mmap_length; ++i) {
            mb1_mmap_entry_t *ent = (mb1_mmap_entry_t *)(size_t)(info->mmap_addr + off);
            off += ent->size + sizeof(ent->size);
            if (MB1_MEMORY_AVAILABLE == ent->type) {
                g_rammap[i].type = RAM_AVAILABLE;
            } else {
                g_rammap[i].type = RAM_RESERVED;
            }
            g_rammap[i].addr = ent->addr;
            g_rammap[i].end  = ent->addr + ent->len;
        }
    }

    if (MB1_INFO_ELF_SHDR & info->flags) {
        mb1_elf_sec_tbl_t *elf = &info->elf;
        symtab_init((void *)(size_t)elf->addr, elf->size, elf->num);
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
            g_rammap_len = (mmap->tag.size - sizeof(mb2_tag_mmap_t)) / mmap->entry_size;
            g_rammap = early_alloc_ro(g_rammap_len * sizeof(ram_range_t));
            for (size_t i = 0; i < g_rammap_len; ++i) {
                mb2_mmap_entry_t *ent = &mmap->entries[i];
                switch (ent->type) {
                case MB2_MEMORY_AVAILABLE:        g_rammap[i].type = RAM_AVAILABLE;   break;
                case MB2_MEMORY_ACPI_RECLAIMABLE: g_rammap[i].type = RAM_RECLAIMABLE; break;
                default:                          g_rammap[i].type = RAM_RESERVED;    break;
                }
                g_rammap[i].addr = ent->addr;
                g_rammap[i].end  = ent->addr + ent->len;
            }
            break;
        }
        case MB2_TAG_TYPE_ELF_SECTIONS: {
            mb2_tag_elf_sections_t *elf = (mb2_tag_elf_sections_t *)tag;
            symtab_init(elf->sections, elf->entsize, elf->num);
            break;
        }
        default:
            break;
        }
    }
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

    rammap_show();

end:
    emu_exit(0);
    while (1) {}
}
